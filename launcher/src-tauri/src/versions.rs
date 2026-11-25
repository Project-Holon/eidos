//! eidos versions: installed under `versions/<id>/`, plus the optional
//! "Local build" folder from the settings.

use std::cmp::Ordering;
use std::fs;
use std::io;
use std::path::{Path, PathBuf};

use serde::Serialize;

use crate::layout::Layout;
use crate::profile::LATEST;

/// The id of the local build entry.
pub const LOCAL: &str = "local";

/// The executables looked for in a version folder, best first.
const EXES: [&str; 5] = [
    "eidos.exe",
    "eidos",
    "eidos.AppImage",
    "eidos_boot.exe",
    "eidos_boot",
];

/// The release asset name pattern for a platform
/// (`eidos-<version>-windows.zip`, ...; docs/distribution.md).
pub fn asset_pattern(os: &str) -> &'static str {
    match os {
        "windows" => "eidos-*-windows.zip",
        "macos" => "eidos-*-macos.zip",
        _ => "eidos-*-linux-x86_64.AppImage",
    }
}

/// A release tag as a folder name: `v0.2.0` becomes `0.2.0`. None when the
/// result would not be a safe folder name.
pub fn version_id(tag: &str) -> Option<String> {
    let id = tag.trim().trim_start_matches(['v', 'V']);
    let ok = !id.is_empty()
        && id.len() <= 64
        && id != LOCAL
        && !id.starts_with('.')
        && id
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || "._+-".contains(c));
    ok.then(|| id.to_string())
}

fn numbers(id: &str) -> Vec<u64> {
    // Only the release part counts; the pre-release tag is ordered below.
    id.split('-')
        .next()
        .unwrap_or("")
        .split(|c: char| !c.is_ascii_digit())
        .filter(|s| !s.is_empty())
        .filter_map(|s| s.parse().ok())
        .collect()
}

/// Orders version ids by their numbers (`0.10.0` after `0.9.0`); a plain
/// release comes after its own pre-release (`1.0.0` after `1.0.0-rc1`).
pub fn compare(a: &str, b: &str) -> Ordering {
    numbers(a).cmp(&numbers(b)).then_with(|| {
        let pre = |s: &str| s.contains('-');
        pre(b).cmp(&pre(a)).then_with(|| a.cmp(b))
    })
}

/// The eidos executable inside a folder (or one folder below it).
pub fn find_exe(dir: &Path) -> Option<PathBuf> {
    let in_dir = |d: &Path| EXES.iter().map(|n| d.join(n)).find(|p| p.is_file());
    if let Some(p) = in_dir(dir) {
        return Some(p);
    }
    let mut subs: Vec<_> = fs::read_dir(dir)
        .ok()?
        .flatten()
        .filter(|e| e.file_type().is_ok_and(|t| t.is_dir()))
        .map(|e| e.path())
        .collect();
    subs.sort();
    subs.iter().find_map(|d| in_dir(d))
}

/// Installed version ids, newest first.
pub fn installed(layout: &Layout) -> Vec<String> {
    let Ok(rd) = fs::read_dir(layout.versions()) else {
        return Vec::new();
    };
    let mut ids: Vec<String> = rd
        .flatten()
        .filter(|e| e.file_type().is_ok_and(|t| t.is_dir()))
        .map(|e| e.file_name().to_string_lossy().into_owned())
        .filter(|n| !n.ends_with(".partial") && find_exe(&layout.version(n)).is_some())
        .collect();
    ids.sort_by(|a, b| compare(b, a));
    ids
}

/// What a profile's version setting means right now.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct Resolved {
    pub id: String,
    pub dir: PathBuf,
    pub exe: PathBuf,
}

/// `latest` is the newest installed version, or the local build when
/// nothing is installed.
pub fn resolve(
    layout: &Layout,
    local_build: Option<&Path>,
    wanted: &str,
) -> Result<Resolved, String> {
    let local = || -> Result<Resolved, String> {
        let dir = local_build.ok_or("No local build folder is set.")?;
        let exe = find_exe(dir).ok_or("The local build folder has no eidos program.")?;
        Ok(Resolved {
            id: LOCAL.into(),
            dir: dir.to_path_buf(),
            exe,
        })
    };
    let from_disk = |id: &str| -> Result<Resolved, String> {
        let dir = layout.version(id);
        let exe = find_exe(&dir).ok_or_else(|| format!("Version {id} is not installed."))?;
        Ok(Resolved {
            id: id.into(),
            dir,
            exe,
        })
    };
    match wanted {
        LOCAL => local(),
        LATEST => match installed(layout).first() {
            Some(id) => from_disk(id),
            None if local_build.is_some() => local(),
            None => Err("No eidos version is installed. Install one on the Versions page.".into()),
        },
        id => from_disk(id),
    }
}

/// Unpacks a zip into `dest`, refusing paths that leave it. Nothing stays at
/// `dest` on failure.
pub fn unzip(zip_path: &Path, dest: &Path) -> io::Result<()> {
    let bad = |m: String| io::Error::new(io::ErrorKind::InvalidData, m);
    let tmp = dest.with_extension("partial");
    let _ = fs::remove_dir_all(&tmp);
    let result = (|| -> io::Result<()> {
        let mut zip =
            zip::ZipArchive::new(fs::File::open(zip_path)?).map_err(|e| bad(e.to_string()))?;
        fs::create_dir_all(&tmp)?;
        for i in 0..zip.len() {
            let mut entry = zip.by_index(i).map_err(|e| bad(e.to_string()))?;
            let rel = entry
                .enclosed_name()
                .ok_or_else(|| bad(format!("unsafe path in the zip: {}", entry.name())))?;
            let out = tmp.join(rel);
            if entry.is_dir() {
                fs::create_dir_all(&out)?;
                continue;
            }
            if let Some(dir) = out.parent() {
                fs::create_dir_all(dir)?;
            }
            let mut file = fs::File::create(&out)?;
            io::copy(&mut entry, &mut file)?;
            #[cfg(unix)]
            if let Some(mode) = entry.unix_mode() {
                use std::os::unix::fs::PermissionsExt;
                fs::set_permissions(&out, fs::Permissions::from_mode(mode))?;
            }
        }
        let _ = fs::remove_dir_all(dest);
        fs::rename(&tmp, dest)
    })();
    if result.is_err() {
        let _ = fs::remove_dir_all(&tmp);
    }
    result
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;

    fn touch(p: &Path) {
        fs::create_dir_all(p.parent().unwrap()).unwrap();
        fs::write(p, b"x").unwrap();
    }

    #[test]
    fn ids_and_order() {
        assert_eq!(version_id("v0.2.0").as_deref(), Some("0.2.0"));
        assert_eq!(version_id("1.0.0-rc1").as_deref(), Some("1.0.0-rc1"));
        assert_eq!(version_id("../x"), None);
        assert_eq!(version_id("local"), None);
        assert_eq!(version_id(""), None);
        assert_eq!(compare("0.10.0", "0.9.0"), Ordering::Greater);
        assert_eq!(compare("1.0.0", "1.0.0-rc1"), Ordering::Greater);
        assert_eq!(compare("1.0.0", "1.0.0"), Ordering::Equal);
    }

    #[test]
    fn asset_names() {
        assert_eq!(asset_pattern("windows"), "eidos-*-windows.zip");
        assert!(releases::matches(asset_pattern("windows"), "eidos-0.2.0-windows.zip"));
        assert!(!releases::matches(
            asset_pattern("windows"),
            "eidos-launcher-setup.exe"
        ));
        assert!(releases::matches(
            asset_pattern("linux"),
            "eidos-0.2.0-linux-x86_64.AppImage"
        ));
        // from 0.2.7: the game's files are named "standalone" and still fit;
        // the launcher's own files do not, on any system (the old Linux
        // launcher file, eidos-launcher-linux-x86_64.AppImage, did)
        assert!(releases::matches(asset_pattern("windows"), "eidos-standalone-0.2.7-windows.zip"));
        assert!(releases::matches(asset_pattern("macos"), "eidos-standalone-0.2.7-macos.zip"));
        assert!(releases::matches(
            asset_pattern("linux"),
            "eidos-standalone-0.2.7-linux-x86_64.AppImage"
        ));
        assert!(!releases::matches(asset_pattern("linux"), "eidos-linux-x86_64.AppImage"));
        assert!(!releases::matches(asset_pattern("windows"), "eidos-setup-windows.exe"));
        assert!(!releases::matches(asset_pattern("macos"), "eidos-macos.dmg"));
        assert!(!releases::matches(asset_pattern("macos"), "eidos-macos.app.tar.gz"));
    }

    #[test]
    fn layout_and_latest() {
        let t = tempfile::tempdir().unwrap();
        let l = Layout::new(t.path());
        assert!(resolve(&l, None, LATEST).is_err());
        touch(&l.version("0.9.0").join("eidos.exe"));
        touch(&l.version("0.10.0").join("eidos_boot.exe"));
        // A folder with no program is not a version; a half unpack is not either.
        fs::create_dir_all(l.version("0.11.0")).unwrap();
        touch(&l.version("0.12.0.partial").join("eidos.exe"));
        assert_eq!(installed(&l), ["0.10.0", "0.9.0"]);
        let r = resolve(&l, None, LATEST).unwrap();
        assert_eq!(r.id, "0.10.0");
        assert!(r.exe.ends_with("eidos_boot.exe"));
        assert_eq!(resolve(&l, None, "0.9.0").unwrap().id, "0.9.0");
        assert!(resolve(&l, None, "0.8.0").is_err());
        assert!(resolve(&l, None, LOCAL).is_err());
    }

    #[test]
    fn local_build_and_nested_exe() {
        let t = tempfile::tempdir().unwrap();
        let l = Layout::new(t.path().join("u"));
        let local = t.path().join("build");
        touch(&local.join("eidos_boot.exe"));
        assert_eq!(resolve(&l, Some(&local), LOCAL).unwrap().id, LOCAL);
        // With nothing installed, latest is the local build.
        assert_eq!(resolve(&l, Some(&local), LATEST).unwrap().id, LOCAL);
        touch(&l.version("1.0.0").join("eidos-1.0.0/eidos.exe"));
        assert_eq!(resolve(&l, Some(&local), LATEST).unwrap().id, "1.0.0");
    }

    #[test]
    fn unzip_refuses_escapes() {
        let t = tempfile::tempdir().unwrap();
        let make = |name: &str, entry: &str| {
            let p = t.path().join(name);
            let mut z = zip::ZipWriter::new(fs::File::create(&p).unwrap());
            z.start_file(entry, zip::write::SimpleFileOptions::default())
                .unwrap();
            z.write_all(b"data").unwrap();
            z.finish().unwrap();
            p
        };
        let good = make("good.zip", "eidos/eidos.exe");
        let dest = t.path().join("v/1.0.0");
        unzip(&good, &dest).unwrap();
        assert!(dest.join("eidos/eidos.exe").is_file());
        assert!(find_exe(&dest).is_some());
        let evil = make("evil.zip", "../escape.txt");
        let dest2 = t.path().join("v/2.0.0");
        assert!(unzip(&evil, &dest2).is_err());
        assert!(!dest2.exists() && !t.path().join("v/escape.txt").exists());
        assert!(!t.path().join("v/2.0.0.partial").exists());
    }
}
