//! A profile's mods: the folders in `mods/`. A disabled mod waits in
//! `mods-disabled/` beside it, so eidos never sees it.

use std::fs;
use std::path::{Path, PathBuf};

use eidos_mod::manifest::{Manifest, MANIFEST};
use eidos_mod::package;
use serde::Serialize;

use crate::profile::copy_dir;

const ON: &str = "mods";
const OFF: &str = "mods-disabled";

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ModInfo {
    /// The folder name; it identifies the mod in the profile.
    pub folder: String,
    pub name: String,
    pub order: i32,
    pub enabled: bool,
    pub has_manifest: bool,
    /// The katalogos mod it was installed as, and its version.
    pub catalog_id: Option<String>,
    pub version: Option<String>,
    pub icon: Option<crate::catalog::UiIcon>,
}

/// What a `.bw` package holds, for the confirmation before adding it.
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PackageInfo {
    pub name: String,
    pub files: usize,
    pub bytes: u64,
}

fn read_info(dir: &Path, enabled: bool) -> ModInfo {
    let folder = dir
        .file_name()
        .map(|n| n.to_string_lossy().into_owned())
        .unwrap_or_default();
    let text = fs::read_to_string(dir.join(MANIFEST)).ok();
    let m = Manifest::parse(text.as_deref().unwrap_or(""), &folder);
    let installed = crate::catalog::installed(dir);
    ModInfo {
        folder,
        name: m.name,
        order: m.order,
        enabled,
        has_manifest: text.is_some(),
        icon: m.icon.as_ref().and_then(|i| crate::catalog::UiIcon::from_manifest(i, dir)),
        catalog_id: installed.as_ref().map(|i| i.id.clone()),
        version: installed.map(|i| i.version).or_else(|| (!m.version.is_empty()).then(|| m.version.clone())),
    }
}

fn subdirs(dir: &Path) -> Vec<PathBuf> {
    let Ok(rd) = fs::read_dir(dir) else {
        return Vec::new();
    };
    rd.flatten()
        .filter(|e| e.file_type().is_ok_and(|t| t.is_dir()))
        .map(|e| e.path())
        .collect()
}

/// Every mod of the profile, enabled ones first, then by name.
pub fn list(profile_dir: &Path) -> Vec<ModInfo> {
    let mut out: Vec<ModInfo> = subdirs(&profile_dir.join(ON))
        .iter()
        .map(|d| read_info(d, true))
        .chain(
            subdirs(&profile_dir.join(OFF))
                .iter()
                .map(|d| read_info(d, false)),
        )
        .collect();
    out.sort_by_key(|m| (!m.enabled, m.name.to_lowercase()));
    out
}

/// A mod folder name must be one plain path component.
fn check_folder(folder: &str) -> Result<(), String> {
    let ok = !folder.is_empty()
        && folder != "."
        && folder != ".."
        && !folder.contains(['/', '\\', ':'])
        && !folder.chars().any(|c| c.is_control());
    ok.then_some(()).ok_or_else(|| "That is not a mod folder name.".to_string())
}

pub fn set_enabled(profile_dir: &Path, folder: &str, enabled: bool) -> Result<(), String> {
    check_folder(folder)?;
    let (from, to) = if enabled { (OFF, ON) } else { (ON, OFF) };
    let src = profile_dir.join(from).join(folder);
    if !src.is_dir() {
        return Err(format!("The mod {folder} is not {}.", if enabled { "disabled" } else { "enabled" }));
    }
    let dst = profile_dir.join(to).join(folder);
    if dst.exists() {
        return Err(format!("A mod folder named {folder} is already there."));
    }
    fs::create_dir_all(profile_dir.join(to)).map_err(|e| e.to_string())?;
    fs::rename(src, dst).map_err(|e| e.to_string())
}

pub fn remove(profile_dir: &Path, folder: &str) -> Result<(), String> {
    check_folder(folder)?;
    for sub in [ON, OFF] {
        let dir = profile_dir.join(sub).join(folder);
        if dir.is_dir() {
            return fs::remove_dir_all(dir).map_err(|e| e.to_string());
        }
    }
    Err(format!("There is no mod named {folder}."))
}

fn taken(profile_dir: &Path, folder: &str) -> bool {
    [ON, OFF]
        .iter()
        .any(|s| profile_dir.join(s).join(folder).exists())
}

/// A folder name made from a mod's name.
fn folder_name(name: &str) -> String {
    let s: String = name
        .trim()
        .chars()
        .map(|c| if c.is_control() || "\\/:*?\"<>|".contains(c) { '_' } else { c })
        .collect();
    let s = s.trim_matches(['.', ' ']).to_string();
    if s.is_empty() {
        "mod".into()
    } else {
        s.chars().take(60).collect()
    }
}

/// Reads a `.bw` file with the package limits, without unpacking.
pub fn inspect_package(bw: &Path) -> Result<PackageInfo, String> {
    let info = package::inspect(bw).map_err(|e| e.to_string())?;
    Ok(PackageInfo {
        name: info.manifest.name,
        files: info.files.len(),
        bytes: info.files.iter().map(|f| f.size).sum(),
    })
}

/// Unpacks a `.bw` into the profile's `mods/`. The folder is named after the
/// package's file name.
pub fn add_package(profile_dir: &Path, bw: &Path) -> Result<String, String> {
    package::inspect(bw).map_err(|e| e.to_string())?;
    let stem = bw
        .file_stem()
        .map(|s| s.to_string_lossy().into_owned())
        .unwrap_or_default();
    let folder = folder_name(&stem);
    if taken(profile_dir, &folder) {
        return Err(format!("A mod named {folder} is already installed."));
    }
    let dest = profile_dir.join(ON).join(&folder);
    if let Err(e) = package::unpack(bw, &dest) {
        let _ = fs::remove_dir_all(&dest);
        return Err(e.to_string());
    }
    Ok(folder)
}

/// The folder that holds the katalogos mod `id` in this profile, on or off.
fn catalog_folder(profile_dir: &Path, id: &str) -> Option<(PathBuf, bool)> {
    for (sub, on) in [(ON, true), (OFF, false)] {
        for d in subdirs(&profile_dir.join(sub)) {
            if crate::catalog::installed(&d).is_some_and(|i| i.id == id) {
                return Some((d, on));
            }
        }
    }
    None
}

/// Unpacks a katalogos mod's package into the profile, with its
/// `katalogos.json`.  An installed copy of the same mod is replaced in place
/// (an update), and keeps its on or off switch.
pub fn install_package(
    profile_dir: &Path,
    bw: &Path,
    name: &str,
    info: &crate::catalog::Installed,
) -> Result<String, String> {
    package::inspect(bw).map_err(|e| e.to_string())?;
    let (dest, folder) = match catalog_folder(profile_dir, &info.id) {
        Some((dir, _)) => {
            let folder = dir.file_name().map(|s| s.to_string_lossy().into_owned()).unwrap_or_default();
            (dir, folder)
        }
        None => {
            let folder = folder_name(name);
            if taken(profile_dir, &folder) {
                return Err(format!("A mod named {folder} is already installed."));
            }
            (profile_dir.join(ON).join(&folder), folder)
        }
    };
    // unpack beside it first, so a failed update leaves the old copy
    let staging = dest.with_file_name(format!(".{folder}.new"));
    let _ = fs::remove_dir_all(&staging);
    if let Err(e) = package::unpack(bw, &staging) {
        let _ = fs::remove_dir_all(&staging);
        return Err(e.to_string());
    }
    let text = serde_json::to_string_pretty(info).map_err(|e| e.to_string())?;
    fs::write(staging.join(crate::catalog::INSTALLED), text).map_err(|e| e.to_string())?;
    if dest.exists() {
        fs::remove_dir_all(&dest).map_err(|e| e.to_string())?;
    }
    fs::rename(&staging, &dest).map_err(|e| e.to_string())?;
    Ok(folder)
}

/// Copies a mod folder into the profile's `mods/`.
pub fn add_folder(profile_dir: &Path, src: &Path) -> Result<String, String> {
    if !src.is_dir() {
        return Err("That is not a folder.".into());
    }
    let name = src
        .file_name()
        .map(|s| s.to_string_lossy().into_owned())
        .unwrap_or_default();
    let folder = folder_name(&name);
    if taken(profile_dir, &folder) {
        return Err(format!("A mod named {folder} is already installed."));
    }
    let dest = profile_dir.join(ON).join(&folder);
    if let Err(e) = copy_dir(src, &dest) {
        let _ = fs::remove_dir_all(&dest);
        return Err(e.to_string());
    }
    Ok(folder)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn mod_dir(profile: &Path, sub: &str, folder: &str, name: Option<&str>) {
        let d = profile.join(sub).join(folder);
        fs::create_dir_all(&d).unwrap();
        if let Some(n) = name {
            fs::write(d.join(MANIFEST), format!("name = {n}\norder = 5\n")).unwrap();
        }
    }

    #[test]
    fn lists_and_toggles() {
        let t = tempfile::tempdir().unwrap();
        let p = t.path();
        mod_dir(p, "mods", "zeta", Some("Zeta pack"));
        mod_dir(p, "mods", "plain", None);
        mod_dir(p, "mods-disabled", "off", Some("Off one"));
        let l = list(p);
        let names: Vec<_> = l.iter().map(|m| (m.name.as_str(), m.enabled)).collect();
        assert_eq!(names, [("plain", true), ("Zeta pack", true), ("Off one", false)]);
        assert_eq!(l[1].order, 5);
        assert!(!l[0].has_manifest);
        set_enabled(p, "off", true).unwrap();
        assert!(p.join("mods/off").is_dir());
        set_enabled(p, "zeta", false).unwrap();
        assert!(p.join("mods-disabled/zeta").is_dir());
        assert!(set_enabled(p, "zeta", false).is_err());
        assert!(set_enabled(p, "../x", true).is_err());
        remove(p, "zeta").unwrap();
        assert!(remove(p, "zeta").is_err());
        assert!(remove(p, "..").is_err());
    }

    #[test]
    fn adds_a_package_and_a_folder() {
        let t = tempfile::tempdir().unwrap();
        let src = t.path().join("My Mod");
        fs::create_dir_all(src.join("data")).unwrap();
        fs::write(src.join(MANIFEST), "name = Shown name\n").unwrap();
        fs::write(src.join("data/a.bin"), b"abc").unwrap();
        let bw = t.path().join("mymod.bw");
        package::pack(&src, &bw).unwrap();
        let info = inspect_package(&bw).unwrap();
        assert_eq!(info.name, "Shown name");
        assert_eq!(info.files, 2);

        let profile = t.path().join("profile");
        assert_eq!(add_package(&profile, &bw).unwrap(), "mymod");
        assert!(profile.join("mods/mymod/data/a.bin").is_file());
        assert!(add_package(&profile, &bw).is_err());
        assert_eq!(add_folder(&profile, &src).unwrap(), "My Mod");
        assert!(add_folder(&profile, &src).is_err());
        assert_eq!(list(&profile).len(), 2);

        let junk = t.path().join("junk.bw");
        fs::write(&junk, b"not a zip").unwrap();
        assert!(add_package(&profile, &junk).is_err());
        assert!(!profile.join("mods/junk").exists());
    }

    #[test]
    fn installs_and_updates_a_catalog_mod() {
        use crate::catalog::Installed;
        let t = tempfile::tempdir().unwrap();
        let src = t.path().join("src");
        fs::create_dir_all(&src).unwrap();
        fs::write(src.join(MANIFEST), "name = Nuzlocke\nversion = 1.0.0\n").unwrap();
        let bw = t.path().join("mod.bw");
        package::pack(&src, &bw).unwrap();
        let profile = t.path().join("profile");
        let v1 = Installed { id: "katalogos/nuzlocke".into(), version: "1.0.0".into() };
        assert_eq!(install_package(&profile, &bw, "Nuzlocke", &v1).unwrap(), "Nuzlocke");
        let l = list(&profile);
        assert_eq!(l[0].catalog_id.as_deref(), Some("katalogos/nuzlocke"));
        assert_eq!(l[0].version.as_deref(), Some("1.0.0"));
        // switched off, then updated: it stays off and in the same folder
        set_enabled(&profile, "Nuzlocke", false).unwrap();
        let v2 = Installed { id: "katalogos/nuzlocke".into(), version: "1.1.0".into() };
        assert_eq!(install_package(&profile, &bw, "Renamed", &v2).unwrap(), "Nuzlocke");
        let l = list(&profile);
        assert_eq!(l.len(), 1);
        assert!(!l[0].enabled);
        assert_eq!(l[0].version.as_deref(), Some("1.1.0"));
        // another mod with the same folder name is refused
        let other = Installed { id: "someone/nuzlocke".into(), version: "1.0.0".into() };
        assert!(install_package(&profile, &bw, "Nuzlocke", &other).is_err());
    }
}
