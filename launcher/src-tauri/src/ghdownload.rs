//! Release files from GitHub, with a way round its download service.
//!
//! A release file's address (`github.com/OWNER/REPO/releases/download/TAG/
//! NAME`) is a plain web download: no rate limit, no token.  When that
//! service fails (it answered 504 for every file for a while, 2026-11-08,
//! while the API served the same files), the file is asked of the API
//! instead (`api.github.com/repos/OWNER/REPO/releases/assets/ID`, with
//! `Accept: application/octet-stream`).
//!
//! The API is limited: without a token, 60 requests an hour per internet
//! connection, shared by every program on it.  So it is only the fallback:
//!
//! - only after the download service answered with a server error (5xx);
//!   a 404 means the file is gone, and the API would say the same;
//! - only while GitHub's last answer said enough requests remain
//!   (`X-RateLimit-Remaining`, kept here from every API answer): below
//!   [`FLOOR`] the fallback is not tried and the error says to try later.

use std::fs;
use std::io::{Read, Write};
use std::path::Path;
use std::sync::atomic::{AtomicI64, Ordering};

use serde::Deserialize;

const API: &str = "https://api.github.com";
const AGENT: &str = "eidos-launcher";
/// The API requests left that the fallback never goes below.
pub const FLOOR: i64 = 10;

/// GitHub's last word on the API requests left this hour (-1: not known).
static REMAINING: AtomicI64 = AtomicI64::new(-1);

fn note_limit(resp: &ureq::Response) {
    if let Some(n) = resp.header("X-RateLimit-Remaining").and_then(|v| v.trim().parse::<i64>().ok()) {
        REMAINING.store(n, Ordering::Relaxed);
    }
}

/// The fallback may spend an API request.
fn api_allowed() -> bool {
    let n = REMAINING.load(Ordering::Relaxed);
    n < 0 || n > FLOOR
}

/// `(owner/repo, tag, name)` of a release file's download address.
pub fn parse_release_url(url: &str) -> Option<(String, String, String)> {
    let rest = url
        .strip_prefix("https://github.com/")
        .or_else(|| url.strip_prefix("http://github.com/"))?;
    let parts: Vec<&str> = rest.split('/').collect();
    if parts.len() != 6 || parts[2] != "releases" || parts[3] != "download" {
        return None;
    }
    if parts.iter().any(|p| p.is_empty()) {
        return None;
    }
    Some((format!("{}/{}", parts[0], parts[1]), parts[4].to_string(), parts[5].to_string()))
}

/// A server error (5xx) from the download service: the fallback's case.
pub fn is_server_error(e: &ureq::Error) -> bool {
    matches!(e, ureq::Error::Status(code, _) if *code >= 500)
}

fn api_get(agent: &ureq::Agent, url: &str, accept: &str) -> Result<ureq::Response, String> {
    if !api_allowed() {
        return Err("GitHub is not answering and its API has few requests left this hour; try again later".into());
    }
    match agent.get(url).set("User-Agent", AGENT).set("Accept", accept).call() {
        Ok(resp) => {
            note_limit(&resp);
            Ok(resp)
        }
        Err(ureq::Error::Status(code, resp)) => {
            note_limit(&resp);
            Err(if code == 403 || code == 429 {
                "GitHub is not answering and its API refused (the hour's requests are used up); try again later"
                    .to_string()
            } else {
                format!("GitHub's API answered {code}")
            })
        }
        Err(e) => Err(e.to_string()),
    }
}

#[derive(Deserialize)]
struct ApiAsset {
    name: String,
    url: String,
}

#[derive(Deserialize)]
struct ApiRelease {
    #[serde(default)]
    assets: Vec<ApiAsset>,
}

/// The API address of a release file, looked up by its release's tag (one
/// API request).
fn asset_api_url(agent: &ureq::Agent, repo: &str, tag: &str, name: &str) -> Result<String, String> {
    let resp = api_get(agent, &format!("{API}/repos/{repo}/releases/tags/{tag}"), "application/vnd.github+json")?;
    let rel: ApiRelease = serde_json::from_reader(resp.into_reader()).map_err(|e| e.to_string())?;
    rel.assets
        .into_iter()
        .find(|a| a.name == name)
        .map(|a| a.url)
        .ok_or_else(|| format!("the release {tag} has no {name}"))
}

/// The file at a release download address, through the API (`api_url`: its
/// API address when known, else looked up).
fn via_api(agent: &ureq::Agent, url: &str, api_url: Option<&str>) -> Result<ureq::Response, String> {
    let api_url = match api_url.filter(|u| u.starts_with(API)) {
        Some(u) => u.to_string(),
        None => {
            let (repo, tag, name) = parse_release_url(url).ok_or("not a release file")?;
            asset_api_url(agent, &repo, &tag, &name)?
        }
    };
    // the API answers with a redirect to the file, which ureq follows
    api_get(agent, &api_url, "application/octet-stream")
}

/// GET `url`; a release file the download service fails to serve comes from
/// the API instead.  Errors name the address.
pub fn get(agent: &ureq::Agent, url: &str) -> Result<ureq::Response, String> {
    match agent.get(url).set("User-Agent", AGENT).call() {
        Ok(resp) => Ok(resp),
        Err(e) if is_server_error(&e) && parse_release_url(url).is_some() => {
            let first = match &e {
                ureq::Error::Status(code, _) => format!("the server answered {code}"),
                _ => String::new(),
            };
            via_api(agent, url, None).map_err(|api| format!("{url}: {first}; {api}"))
        }
        Err(ureq::Error::Status(code, _)) => Err(format!("{url}: the server answered {code}")),
        Err(e) => Err(format!("{url}: {e}")),
    }
}

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

/// [`releases::download`] for a public repository, with the fallback: when
/// the download service fails with a server error, the file comes through
/// the API (`asset.url`, the API address the release list gives, or looked
/// up).  With a token (a private repository) the crate's own path is taken
/// as before.
pub fn download(
    asset: &releases::Asset,
    token: Option<&str>,
    dest: &Path,
    expected: Option<&str>,
) -> Result<String, String> {
    let first = match releases::download(asset, token, dest, expected) {
        Ok(h) => return Ok(h),
        Err(e) => e.to_string(),
    };
    let private = token.is_some_and(|t| !t.is_empty());
    if private || !first.starts_with("the server answered 5") {
        return Err(first);
    }
    let agent = ureq::AgentBuilder::new()
        .timeout_connect(std::time::Duration::from_secs(15))
        .timeout_read(std::time::Duration::from_secs(60))
        .build();
    let resp = via_api(&agent, &asset.browser_download_url, Some(&asset.url))
        .map_err(|api| format!("{first}; {api}"))?;
    let tmp = dest.with_extension("part");
    let mut hasher = <sha2::Sha256 as sha2::Digest>::new();
    let r = (|| -> Result<(), String> {
        let mut file = fs::File::create(&tmp).map_err(|e| e.to_string())?;
        let mut reader = resp.into_reader();
        let mut buf = [0u8; 64 * 1024];
        loop {
            let n = reader.read(&mut buf).map_err(|e| e.to_string())?;
            if n == 0 {
                break;
            }
            sha2::Digest::update(&mut hasher, &buf[..n]);
            file.write_all(&buf[..n]).map_err(|e| e.to_string())?;
        }
        Ok(())
    })();
    if let Err(e) = r {
        let _ = fs::remove_file(&tmp);
        return Err(e);
    }
    let actual = hex(&sha2::Digest::finalize(hasher));
    if let Some(want) = expected {
        if !want.eq_ignore_ascii_case(&actual) {
            let _ = fs::remove_file(&tmp);
            return Err(format!("the download does not match its checksum (expected {want}, got {actual})"));
        }
    }
    fs::rename(&tmp, dest).map_err(|e| e.to_string())?;
    Ok(actual)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn release_urls() {
        assert_eq!(
            parse_release_url("https://github.com/Project-Holon/katalogos/releases/download/nuzlocke-v1.1.0/mod.json"),
            Some(("Project-Holon/katalogos".into(), "nuzlocke-v1.1.0".into(), "mod.json".into()))
        );
        assert_eq!(parse_release_url("https://raw.githubusercontent.com/a/b/main/index.json"), None);
        assert_eq!(parse_release_url("https://github.com/a/b/releases/latest"), None);
        assert_eq!(parse_release_url("https://github.com/a/b/releases/download//x"), None);
    }

    /// Network: a katalogos mod.json through `get` (cargo test -- --ignored).
    #[test]
    #[ignore]
    fn live_mod_json() {
        let agent = ureq::AgentBuilder::new().timeout(std::time::Duration::from_secs(30)).build();
        let url = "https://github.com/Project-Holon/katalogos/releases/download/nuzlocke-v1.1.0/mod.json";
        let direct = agent.get(url).set("User-Agent", AGENT).call().map(|r| r.status());
        eprintln!("direct: {:?}", direct.map_err(|e| e.to_string()));
        let body = get(&agent, url).unwrap().into_string().unwrap();
        eprintln!("through get: {} bytes, API requests left {}", body.len(), REMAINING.load(Ordering::Relaxed));
        assert!(body.contains("katalogos/nuzlocke"));
        // the fallback's own path, looked up by tag (two API requests)
        let api = via_api(&agent, url, None).unwrap().into_string().unwrap();
        eprintln!("through the API: {} bytes, API requests left {}", api.len(), REMAINING.load(Ordering::Relaxed));
        assert_eq!(api, body);
    }

    #[test]
    fn floor() {
        REMAINING.store(-1, Ordering::Relaxed);
        assert!(api_allowed());
        REMAINING.store(FLOOR + 1, Ordering::Relaxed);
        assert!(api_allowed());
        REMAINING.store(FLOOR, Ordering::Relaxed);
        assert!(!api_allowed());
        REMAINING.store(-1, Ordering::Relaxed);
    }
}
