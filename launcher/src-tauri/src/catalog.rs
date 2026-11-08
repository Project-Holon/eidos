//! The katalogos mod index: the list of mods the launcher can install, and
//! the install itself (docs in the katalogos repository).
//!
//! The index is one JSON file; each mod in it has a release with `mod.json`
//! (what the list shows) and `mod.bw` (the package). An install downloads
//! both, checks the package against `mod.json` (size, SHA-256, the package
//! limits) and unpacks it into the profile's mods, with a small
//! `katalogos.json` beside the manifest that says which mod and version it
//! is, so the list can show what is installed and what has an update.

use std::fs;
use std::io::Read;
use std::path::Path;
use std::time::Duration;

use katalogos::{release_url, verify, Index, IconRef, ModJson, FORMAT, ICON_FILE, PACKAGE};
use serde::{Deserialize, Serialize};

use crate::mods;

/// The index the launcher reads when the settings name none.
pub const DEFAULT_INDEX: &str = "https://raw.githubusercontent.com/Project-Holon/katalogos/main/index.json";
/// The file an installed mod's folder carries.
pub const INSTALLED: &str = "katalogos.json";
/// The largest mod.json the launcher reads.
const MAX_JSON: u64 = 1 << 20;
/// The largest icon.png (the packer's own limit).
const MAX_ICON: u64 = 64 * 1024;

/// A mod's icon as the page draws it: a game item's or a species' picture,
/// drawn from the player's own ROM, or the mod's own PNG.
#[derive(Debug, Clone, Serialize, Default, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct UiIcon {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub item: Option<u16>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub species: Option<u16>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub png: Option<Vec<u8>>,
}

impl UiIcon {
    /// From a manifest's icon, the file read from the mod's folder.
    pub fn from_manifest(icon: &eidos_mod::manifest::Icon, dir: &Path) -> Option<UiIcon> {
        use eidos_mod::manifest::Icon;
        match icon {
            Icon::Item(n) => Some(UiIcon { item: Some(*n), ..Default::default() }),
            Icon::Species(n) => Some(UiIcon { species: Some(*n), ..Default::default() }),
            Icon::File(f) => {
                let bytes = fs::read(dir.join(f)).ok()?;
                eidos_mod::manifest::check_icon_png(&bytes).ok()?;
                Some(UiIcon { png: Some(bytes), ..Default::default() })
            }
        }
    }
}

/// One mod of the index, as the Mods page shows it.
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct CatalogMod {
    pub id: String,
    pub name: String,
    pub version: String,
    pub author: String,
    pub description: String,
    pub official: bool,
    pub multiplayer: bool,
    pub has_code: bool,
    pub games: Vec<String>,
    pub tags: Vec<String>,
    pub size: u64,
    pub icon: Option<UiIcon>,
    /// The index entry's position, to install it again by.
    pub index: usize,
}

/// A mod of the index that could not be read.
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct CatalogError {
    pub entry: String,
    pub error: String,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Catalog {
    pub mods: Vec<CatalogMod>,
    pub errors: Vec<CatalogError>,
}

/// What an installed mod's `katalogos.json` holds.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct Installed {
    pub id: String,
    pub version: String,
}

fn agent() -> ureq::Agent {
    ureq::AgentBuilder::new().timeout(Duration::from_secs(30)).build()
}

fn get(agent: &ureq::Agent, url: &str, max: u64) -> Result<Vec<u8>, String> {
    // a release file the download service fails to serve comes from the API
    // (crate::ghdownload: only on a server error, within the hour's limit)
    let resp = crate::ghdownload::get(agent, url)?;
    let mut buf = Vec::new();
    resp.into_reader()
        .take(max + 1)
        .read_to_end(&mut buf)
        .map_err(|e| format!("{url}: {e}"))?;
    if buf.len() as u64 > max {
        return Err(format!("{url}: larger than {max} bytes"));
    }
    Ok(buf)
}

fn index_url(setting: &str) -> &str {
    if setting.trim().is_empty() {
        DEFAULT_INDEX
    } else {
        setting.trim()
    }
}

fn read_index(agent: &ureq::Agent, url: &str) -> Result<Index, String> {
    let idx: Index = serde_json::from_slice(&get(agent, url, MAX_JSON)?)
        .map_err(|e| format!("The mod index does not read: {e}"))?;
    if idx.format != FORMAT {
        return Err(format!(
            "The mod index is format {}; this launcher reads format {FORMAT}. Update the launcher.",
            idx.format
        ));
    }
    Ok(idx)
}

/// A release file of a mod, kept on disk by its address.  A release's
/// files never change once its tag is out (a new version is a new tag, so
/// a new address), so a kept copy is the file: it is read from disk and
/// GitHub is not asked again until the index names another release.  That
/// is also what keeps the list showing while GitHub's downloads fail.
fn get_kept(agent: &ureq::Agent, cache: &Path, url: &str, max: u64) -> Result<Vec<u8>, String> {
    use sha2::Digest;
    let key: String = sha2::Sha256::digest(url.as_bytes()).iter().take(16).map(|b| format!("{b:02x}")).collect();
    let path = cache.join(key);
    if let Ok(bytes) = fs::read(&path) {
        if bytes.len() as u64 <= max {
            return Ok(bytes);
        }
    }
    let bytes = get(agent, url, max)?;
    if fs::create_dir_all(cache).is_ok() {
        let tmp = path.with_extension("part");
        if fs::write(&tmp, &bytes).is_ok() {
            let _ = fs::rename(&tmp, &path);
        }
    }
    Ok(bytes)
}

/// The mods of the index, with their mod.json read (`cache`: where release
/// files are kept, [`get_kept`]).  A mod that cannot be read is listed
/// among the errors and the others still show.
pub fn list(index_setting: &str, cache: &Path) -> Result<Catalog, String> {
    let agent = agent();
    let idx = read_index(&agent, index_url(index_setting))?;
    let mut out = Catalog { mods: Vec::new(), errors: Vec::new() };
    for (i, e) in idx.mods.iter().enumerate() {
        let url = release_url(e, "mod.json");
        let r = if url.is_empty() {
            Err("the entry names no mod".to_string())
        } else {
            get_kept(&agent, cache, &url, MAX_JSON).and_then(|b| {
                serde_json::from_slice::<ModJson>(&b).map_err(|err| format!("mod.json: {err}"))
            })
        };
        match r {
            Ok(j) if j.id.to_lowercase() == e.label().to_lowercase() => out.mods.push(CatalogMod {
                icon: match &j.icon {
                    Some(IconRef::Item(n)) => Some(UiIcon { item: Some(*n), ..Default::default() }),
                    Some(IconRef::Species(n)) => Some(UiIcon { species: Some(*n), ..Default::default() }),
                    // a mod's own picture comes beside its mod.json
                    Some(IconRef::File(_)) => get_kept(&agent, cache, &release_url(e, ICON_FILE), MAX_ICON)
                        .ok()
                        .filter(|b| eidos_mod::manifest::check_icon_png(b).is_ok())
                        .map(|b| UiIcon { png: Some(b), ..Default::default() }),
                    None => None,
                },
                id: j.id,
                name: j.name,
                version: j.version,
                author: j.author,
                description: j.description,
                official: j.official,
                multiplayer: j.multiplayer.is_some(),
                has_code: j.has_code,
                games: j.games,
                tags: j.tags,
                size: j.package.size,
                index: i,
            }),
            Ok(j) => out.errors.push(CatalogError {
                entry: e.label(),
                error: format!("its mod.json is another mod's ({})", j.id),
            }),
            Err(err) => out.errors.push(CatalogError { entry: e.label(), error: err }),
        }
    }
    // official mods first, then by name
    out.mods.sort_by_key(|m| (!m.official, m.name.to_lowercase()));
    Ok(out)
}

/// What a profile's mod folder says it was installed as.
pub fn installed(dir: &Path) -> Option<Installed> {
    let text = fs::read_to_string(dir.join(INSTALLED)).ok()?;
    serde_json::from_str(&text).ok()
}

/// Installs (or updates) the index's mod at `index` into the profile.
/// Returns the mod's folder.
pub fn install(profile_dir: &Path, index_setting: &str, index: usize) -> Result<String, String> {
    let agent = agent();
    let idx = read_index(&agent, index_url(index_setting))?;
    let e = idx.mods.get(index).ok_or("That mod is no longer in the index.")?;
    let j: ModJson = serde_json::from_slice(&get(&agent, &release_url(e, "mod.json"), MAX_JSON)?)
        .map_err(|err| format!("mod.json: {err}"))?;
    if j.id.to_lowercase() != e.label().to_lowercase() {
        return Err(format!("The index's mod {} points at another mod ({}).", e.label(), j.id));
    }
    let bytes = get(&agent, &release_url(e, PACKAGE), eidos_mod::package::MAX_TOTAL)?;
    let icon = match &j.icon {
        Some(IconRef::File(_)) => Some(get(&agent, &release_url(e, ICON_FILE), MAX_ICON)?),
        _ => None,
    };
    let tmp = tempdir_in(profile_dir)?;
    let r = (|| {
        verify(&j, &bytes, icon.as_deref(), &tmp)
            .map_err(|err| format!("The download does not match its mod.json: {err}"))?;
        let bw = tmp.join(PACKAGE);
        fs::write(&bw, &bytes).map_err(|err| err.to_string())?;
        let info = Installed { id: j.id.clone(), version: j.version.clone() };
        mods::install_package(profile_dir, &bw, &j.name, &info)
    })();
    let _ = fs::remove_dir_all(&tmp);
    r
}

fn tempdir_in(profile_dir: &Path) -> Result<std::path::PathBuf, String> {
    let d = profile_dir.join(".katalogos-download");
    let _ = fs::remove_dir_all(&d);
    fs::create_dir_all(&d).map_err(|e| e.to_string())?;
    Ok(d)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn default_index() {
        assert_eq!(index_url(""), DEFAULT_INDEX);
        assert_eq!(index_url("  https://x/i.json "), "https://x/i.json");
    }

    #[test]
    fn installed_file() {
        let t = tempfile::tempdir().unwrap();
        assert!(installed(t.path()).is_none());
        let i = Installed { id: "katalogos/nuzlocke".into(), version: "1.0.0".into() };
        fs::write(t.path().join(INSTALLED), serde_json::to_string(&i).unwrap()).unwrap();
        assert_eq!(installed(t.path()), Some(i));
    }
}
