//! The release list, gentle on GitHub's limits.
//!
//! Without a token GitHub answers 60 API requests an hour per internet
//! connection, and every program on that connection shares them. So:
//!
//! - the releases feed (`releases.atom`, not rate limited) is asked first:
//!   when every tag in it is in the saved list, that list is the answer and
//!   the API is not asked at all;
//! - otherwise (a new release, no saved list, or a token for a private
//!   repository, whose feed needs a login) the API is asked; the last list
//!   is kept on disk with its ETag, and each request asks "changed since?",
//!   an answer that does not count against the limit;
//! - when the API refuses (403, 429) or cannot be reached, each tag of the
//!   feed the saved list does not know gets the asset names the release
//!   workflow always uploads (docs/release.md);
//! - when both fail, the saved list is shown as it is.

use std::fs;
use std::time::Duration;

use releases::{Asset, Release};
use serde::{Deserialize, Serialize};

use crate::layout::Layout;

const API: &str = "https://api.github.com";
const SITE: &str = "https://github.com";
const AGENT: &str = "eidos-launcher";
const CACHE: &str = "releases-cache.json";

#[derive(Serialize, Deserialize, Default)]
struct Cache {
    etag: Option<String>,
    body: String,
}

fn agent() -> ureq::Agent {
    ureq::AgentBuilder::new()
        .timeout(Duration::from_secs(20))
        .build()
}

/// The repository's releases, newest first, drafts left out.
pub fn list(layout: &Layout, repo: &str, token: Option<&str>) -> Result<Vec<Release>, String> {
    let path = layout.launcher().join(CACHE);
    let cache: Option<Cache> = fs::read_to_string(&path)
        .ok()
        .and_then(|s| serde_json::from_str(&s).ok());
    let saved = || {
        cache
            .as_ref()
            .and_then(|c| releases::parse_releases(&c.body).ok())
    };
    let agent = agent();
    let known = saved().unwrap_or_default();

    // the feed first: no quota, and enough when nothing is new
    let tags = if token.is_none() { feed(&agent, repo).ok() } else { None };
    // (the feed holds only the newest releases, so the answer is the whole
    // saved list, older versions included)
    if let Some(tags) = tags.as_ref().filter(|t| !t.is_empty()) {
        if tags.iter().all(|(tag, _, _)| known.iter().any(|r| &r.tag_name == tag)) {
            return Ok(known);
        }
    }

    let mut req = agent
        .get(&format!("{API}/repos/{repo}/releases?per_page=100"))
        .set("User-Agent", AGENT)
        .set("Accept", "application/vnd.github+json");
    if let Some(t) = token {
        req = req.set("Authorization", &format!("Bearer {t}"));
    }
    if let Some(e) = cache.as_ref().and_then(|c| c.etag.as_deref()) {
        req = req.set("If-None-Match", e);
    }
    let api_error = match req.call() {
        Ok(resp) if resp.status() == 304 => match saved() {
            Some(list) => return Ok(list),
            None => "the saved release list could not be read".to_string(),
        },
        Ok(resp) => {
            let etag = resp.header("ETag").map(str::to_string);
            match resp.into_string() {
                Ok(body) => match releases::parse_releases(&body) {
                    Ok(list) => {
                        let _ = fs::create_dir_all(layout.launcher());
                        if let Ok(text) = serde_json::to_string(&Cache { etag, body }) {
                            let _ = fs::write(&path, text);
                        }
                        return Ok(list);
                    }
                    Err(e) => e.to_string(),
                },
                Err(e) => e.to_string(),
            }
        }
        Err(ureq::Error::Status(code, _)) => format!("the server answered {code}"),
        Err(e) => e.to_string(),
    };

    let tags = match tags {
        Some(t) => Ok(t),
        None => feed(&agent, repo),
    };
    if let Ok(tags) = tags {
        if !tags.is_empty() {
            let mut noted = false;
            return Ok(tags
                .into_iter()
                .map(|(tag, title, updated)| {
                    known.iter().find(|r| r.tag_name == tag).cloned().unwrap_or_else(|| {
                        let mut r = guessed(repo, &tag, title, updated);
                        // the feed has no notes: the newest unknown release
                        // takes its section of the changelog at its tag
                        // (raw files are not rate limited, the API is)
                        if !noted {
                            noted = true;
                            r.body = changelog_notes(&agent, repo, &tag);
                        }
                        r
                    })
                })
                .collect());
        }
    }
    if !known.is_empty() {
        return Ok(known);
    }
    Err(api_error)
}

/// A release from the feed: the files the release workflow uploads for
/// every version, at their download addresses.
fn guessed(repo: &str, tag: &str, title: String, updated: String) -> Release {
    let ver = tag.trim_start_matches(['v', 'V']);
    // a test build's tag (v0.2.0-pre.1) is a pre-release: the feed does
    // not say so, the tag does; its files carry the version alone
    let prerelease = ver.contains('-');
    let ver = ver.split('-').next().unwrap_or(ver);
    let names = [
        format!("eidos-{ver}-windows.zip"),
        format!("eidos-{ver}-macos.zip"),
        format!("eidos-{ver}-linux-x86_64.AppImage"),
        "SHA256SUMS".to_string(),
    ];
    Release {
        tag_name: tag.to_string(),
        name: Some(title),
        draft: false,
        prerelease,
        published_at: Some(updated),
        body: None,
        assets: names
            .into_iter()
            .map(|name| {
                let url = format!("{SITE}/{repo}/releases/download/{tag}/{name}");
                Asset {
                    name,
                    size: 0,
                    url: url.clone(),
                    browser_download_url: url,
                }
            })
            .collect(),
    }
}

/// A release's notes when the API cannot be asked: its version's section of
/// CHANGELOG.md at its tag (the section the release workflow's notes start
/// from), without the section's own heading.
fn changelog_notes(agent: &ureq::Agent, repo: &str, tag: &str) -> Option<String> {
    let text = agent
        .get(&format!("https://raw.githubusercontent.com/{repo}/{tag}/CHANGELOG.md"))
        .set("User-Agent", AGENT)
        .call()
        .ok()?
        .into_string()
        .ok()?;
    changelog_section(&text, tag)
}

/// The `## <version>` section of a changelog (a test build's tag reads its
/// version's section), without its heading; None when there is none.
pub fn changelog_section(text: &str, tag: &str) -> Option<String> {
    let ver = tag.trim_start_matches(['v', 'V']);
    let ver = ver.split('-').next().unwrap_or(ver);
    let mut out: Vec<&str> = Vec::new();
    let mut inside = false;
    for line in text.lines() {
        let t = line.trim();
        if let Some(head) = t.strip_prefix("## ") {
            if inside {
                break;
            }
            inside = head.trim() == ver;
            continue;
        }
        if inside {
            out.push(line);
        }
    }
    let notes = out.join("\n").trim().to_string();
    if notes.is_empty() {
        None
    } else {
        Some(notes)
    }
}

/// (tag, title, updated) of each entry of the releases feed, newest first.
fn feed(agent: &ureq::Agent, repo: &str) -> Result<Vec<(String, String, String)>, String> {
    let body = agent
        .get(&format!("{SITE}/{repo}/releases.atom"))
        .set("User-Agent", AGENT)
        .call()
        .map_err(|e| e.to_string())?
        .into_string()
        .map_err(|e| e.to_string())?;
    Ok(parse_feed(&body))
}

fn between<'a>(s: &'a str, open: &str, close: &str) -> Option<&'a str> {
    let a = s.find(open)? + open.len();
    let b = s[a..].find(close)? + a;
    Some(&s[a..b])
}

fn unescape(s: &str) -> String {
    s.replace("&lt;", "<")
        .replace("&gt;", ">")
        .replace("&quot;", "\"")
        .replace("&#39;", "'")
        .replace("&amp;", "&")
}

pub fn parse_feed(xml: &str) -> Vec<(String, String, String)> {
    xml.split("<entry>")
        .skip(1)
        .filter_map(|entry| {
            let href = entry
                .split("<link")
                .skip(1)
                .filter_map(|l| between(l, "href=\"", "\""))
                .find(|h| h.contains("/releases/tag/"))?;
            let tag = unescape(href.rsplit("/releases/tag/").next()?);
            let title = between(entry, "<title>", "</title>")
                .map(unescape)
                .unwrap_or_else(|| tag.clone());
            let updated = between(entry, "<updated>", "</updated>")
                .unwrap_or("")
                .to_string();
            (!tag.is_empty()).then_some((tag, title, updated))
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn changelog_sections() {
        let text = "# Changelog\n\nintro\n\n## 0.2.0\n\n### Fixes\n- **A**: one.\n\n## 0.1.9\n\n- old\n";
        assert_eq!(changelog_section(text, "v0.2.0").as_deref(), Some("### Fixes\n- **A**: one."));
        assert_eq!(changelog_section(text, "v0.2.0-pre.1").as_deref(), Some("### Fixes\n- **A**: one."));
        assert_eq!(changelog_section(text, "v0.1.9").as_deref(), Some("- old"));
        assert_eq!(changelog_section(text, "v0.3.0"), None);
    }

    #[test]
    fn feed_entries() {
        let xml = r#"<feed><title>x</title>
<entry><id>a</id><updated>2026-10-23T10:25:16Z</updated>
<link rel="alternate" type="text/html" href="https://github.com/o/r/releases/tag/v0.1.2"/>
<title>eidos 0.1.2</title><content>&lt;p&gt;</content></entry>
<entry><updated>2026-10-22T00:00:00Z</updated>
<link rel="alternate" type="text/html" href="https://github.com/o/r/releases/tag/v0.1.1"/>
<title>eidos &amp; 0.1.1</title></entry></feed>"#;
        let e = parse_feed(xml);
        assert_eq!(e.len(), 2);
        assert_eq!(e[0].0, "v0.1.2");
        assert_eq!(e[0].1, "eidos 0.1.2");
        assert_eq!(e[1].1, "eidos & 0.1.1");
    }

    #[test]
    fn guessed_prerelease() {
        let r = guessed("o/r", "v0.2.0-pre.1", "t".into(), "u".into());
        assert!(r.prerelease);
        assert!(releases::pick_asset(&r, "eidos-*-windows.zip")
            .unwrap()
            .browser_download_url
            .ends_with("/v0.2.0-pre.1/eidos-0.2.0-windows.zip"));
        assert!(!guessed("o/r", "v0.2.0", "t".into(), "u".into()).prerelease);
    }

    #[test]
    fn guessed_assets() {
        let r = guessed("o/r", "v0.1.2", "t".into(), "u".into());
        let a = releases::pick_asset(&r, "eidos-*-windows.zip").unwrap();
        assert_eq!(
            a.browser_download_url,
            "https://github.com/o/r/releases/download/v0.1.2/eidos-0.1.2-windows.zip"
        );
        assert!(releases::pick_asset(&r, "SHA256SUMS").is_some());
    }
}
