//! Moving a profile's game data (eidos' extraction folder) to another place.
//! Same volume: one rename. Otherwise: copy into `<target>.moving`, compare
//! every byte, rename it into place, set the profile, and only then delete
//! the old folder. Any failure leaves the old data and the setting alone.
//! When another profile uses the same folder (the default one, the game's
//! remembered one, or a duplicate's), the data is copied and the old folder
//! stays for the others.

use std::fs::{self, File};
use std::io::{self, Read};
use std::path::{Path, PathBuf};

use crate::layout::Layout;
use crate::profile;

const MANIFEST: &str = "eidos_manifest.json";
const PARTIAL: &str = ".eidos_partial";

/// The folder where the game keeps its settings and its default data folder.
pub fn game_home(os: &str, env: &dyn Fn(&str) -> Option<String>) -> Option<PathBuf> {
    let get = |k: &str| env(k).filter(|v| !v.is_empty());
    match os {
        "windows" => get("LOCALAPPDATA").map(|p| Path::new(&p).join("eidos")),
        _ => crate::layout::root_for(os, env),
    }
}

/// Where a profile's data is now: its own folder when set, else the folder
/// the game remembers (`data_root=` in `config.txt`), else `<home>/extract`.
pub fn source_for(home: &Path, data_root: &str) -> PathBuf {
    if !data_root.is_empty() {
        return PathBuf::from(data_root);
    }
    if let Ok(text) = fs::read_to_string(home.join("config.txt")) {
        for line in text.lines() {
            if let Some(v) = line.trim().strip_prefix("data_root=") {
                if !v.trim().is_empty() {
                    return PathBuf::from(v.trim());
                }
            }
        }
    }
    home.join("extract")
}

/// What the tests change: a step between the copy and the check.
pub struct Options<'a> {
    pub no_rename: bool,
    pub after_copy: &'a dyn Fn(&Path),
}

/// Moves the data of a profile. The text says what happened.
pub fn move_profile_data(layout: &Layout, name: &str, to: &str) -> Result<String, String> {
    let home = game_home(std::env::consts::OS, &|k| std::env::var(k).ok())
        .ok_or("Could not find the game's folder.")?;
    let opts = Options {
        no_rename: false,
        after_copy: &|_| {},
    };
    move_with(layout, &home, name, to, &opts)
}

pub fn move_with(
    layout: &Layout,
    home: &Path,
    name: &str,
    to: &str,
    opts: &Options,
) -> Result<String, String> {
    let current = profile::get(layout, name)?.data_root;
    let target = PathBuf::from(to.trim());
    if !target.is_absolute() {
        return Err("The folder must be a full path.".into());
    }
    if target.is_file() {
        return Err("That path is a file, not a folder.".into());
    }
    let source = source_for(home, &current);
    if !source.join(MANIFEST).is_file() {
        return Err("There is no game data to move.".into());
    }
    let (s, t) = (key(&source), key(&target));
    if s == t {
        return Ok("The data is already there.".into());
    }
    if t.starts_with(&s) {
        return Err("The new folder is inside the old one.".into());
    }
    if s.starts_with(&t) {
        return Err("The new folder contains the old one.".into());
    }
    if target.is_dir() && fs::read_dir(&target).map_or(true, |mut d| d.next().is_some()) {
        return Err("The new folder is not empty.".into());
    }
    let target_text = target.to_string_lossy().into_owned();
    let shared = profile::list(layout)
        .map_err(|e| format!("Could not read the profiles: {e}"))?
        .iter()
        .any(|p| p.name != name && key(&source_for(home, &p.data_root)) == s);

    if !shared && !opts.no_rename && try_rename(&source, &target) {
        if let Err(e) = profile::set_data_root(layout, name, &target_text) {
            let _ = fs::rename(&target, &source);
            return Err(format!("Could not save the setting: {e} The old data is untouched."));
        }
        return Ok(format!("The data is now at {target_text}."));
    }

    let staging = staging_path(&target);
    let fail = |e: String| {
        let _ = fs::remove_dir_all(&staging);
        format!("Could not copy the game data: {e}. The old data is untouched.")
    };
    if staging.exists() {
        if !staging.join(PARTIAL).is_file() {
            return Err(format!("{} exists. Remove it first.", staging.display()));
        }
        fs::remove_dir_all(&staging)
            .map_err(|e| format!("Could not clear {}: {e}", staging.display()))?;
    }
    fs::create_dir_all(&staging).map_err(|e| fail(e.to_string()))?;
    fs::write(staging.join(PARTIAL), b"").map_err(|e| fail(e.to_string()))?;
    copy_tree(&source, &staging).map_err(|e| fail(e.to_string()))?;
    (opts.after_copy)(&staging);
    verify(&source, &staging, &staging).map_err(|e| fail(format!("the copy did not match ({e})")))?;
    fs::remove_file(staging.join(PARTIAL)).map_err(|e| fail(e.to_string()))?;
    if target.is_dir() {
        fs::remove_dir(&target).map_err(|e| fail(e.to_string()))?;
    }
    fs::rename(&staging, &target).map_err(|e| fail(e.to_string()))?;
    if let Err(e) = profile::set_data_root(layout, name, &target_text) {
        let _ = fs::remove_dir_all(&target);
        return Err(format!("Could not save the setting: {e} The old data is untouched."));
    }
    if shared {
        return Ok(format!(
            "The data is now at {target_text}. The old copy at {} stays: other profiles use it.",
            source.display()
        ));
    }
    match fs::remove_dir_all(&source) {
        Ok(()) => Ok(format!("The data is now at {target_text}.")),
        Err(_) => Ok(format!(
            "The data is now at {target_text}. The old copy at {} could not be deleted.",
            source.display()
        )),
    }
}

/// A comparable form of a path: the nearest existing part resolved, case
/// folded where the file system ignores case.
fn key(p: &Path) -> PathBuf {
    let mut rest = Vec::new();
    let mut base = p.to_path_buf();
    let resolved = loop {
        if let Ok(c) = base.canonicalize() {
            break c;
        }
        match (base.file_name().map(|n| n.to_os_string()), base.parent()) {
            (Some(n), Some(parent)) => {
                rest.push(n);
                base = parent.to_path_buf();
            }
            _ => break p.to_path_buf(),
        }
    };
    let mut out = resolved;
    for n in rest.into_iter().rev() {
        out.push(n);
    }
    if cfg!(any(windows, target_os = "macos")) {
        PathBuf::from(out.to_string_lossy().to_lowercase())
    } else {
        out
    }
}

fn staging_path(target: &Path) -> PathBuf {
    let mut name = target.file_name().unwrap_or_default().to_os_string();
    name.push(".moving");
    target.with_file_name(name)
}

/// One rename; true when it worked. An empty target folder is replaced.
fn try_rename(source: &Path, target: &Path) -> bool {
    if let Some(parent) = target.parent() {
        if fs::create_dir_all(parent).is_err() {
            return false;
        }
    }
    let existed = target.is_dir();
    if existed && fs::remove_dir(target).is_err() {
        return false;
    }
    if fs::rename(source, target).is_ok() {
        return true;
    }
    if existed {
        let _ = fs::create_dir(target);
    }
    false
}

fn is_link(meta: &fs::Metadata) -> bool {
    #[cfg(windows)]
    {
        use std::os::windows::fs::MetadataExt;
        if meta.file_attributes() & 0x400 != 0 {
            return true;
        }
    }
    meta.file_type().is_symlink()
}

/// Copies a folder tree. A link inside is an error: it is never followed.
fn copy_tree(from: &Path, to: &Path) -> io::Result<()> {
    for entry in fs::read_dir(from)? {
        let entry = entry?;
        let meta = fs::symlink_metadata(entry.path())?;
        let dest = to.join(entry.file_name());
        if is_link(&meta) {
            return Err(io::Error::other(format!("{} is a link", entry.path().display())));
        }
        if meta.is_dir() {
            fs::create_dir(&dest)?;
            copy_tree(&entry.path(), &dest)?;
        } else {
            fs::copy(entry.path(), dest)?;
        }
    }
    Ok(())
}

/// The same entries with the same sizes and bytes (the marker in the
/// staging root is ignored).
fn verify(a: &Path, b: &Path, staging: &Path) -> Result<(), String> {
    let list = |dir: &Path| -> io::Result<Vec<(String, fs::Metadata)>> {
        let mut v = Vec::new();
        for e in fs::read_dir(dir)? {
            let e = e?;
            let name = e.file_name().to_string_lossy().into_owned();
            if !(name == PARTIAL && dir == staging) {
                v.push((name, fs::symlink_metadata(e.path())?));
            }
        }
        v.sort_by(|x, y| x.0.cmp(&y.0));
        Ok(v)
    };
    let (la, lb) = (list(a).map_err(|e| e.to_string())?, list(b).map_err(|e| e.to_string())?);
    if la.len() != lb.len() || la.iter().zip(&lb).any(|(x, y)| x.0 != y.0) {
        return Err(format!("different files in {}", a.display()));
    }
    for ((name, ma), (_, mb)) in la.iter().zip(&lb) {
        let (pa, pb) = (a.join(name), b.join(name));
        if ma.is_dir() != mb.is_dir() {
            return Err(format!("{name} differs"));
        }
        if ma.is_dir() {
            verify(&pa, &pb, staging)?;
        } else if ma.len() != mb.len() || !same_bytes(&pa, &pb).map_err(|e| e.to_string())? {
            return Err(format!("{name} differs"));
        }
    }
    Ok(())
}

fn same_bytes(a: &Path, b: &Path) -> io::Result<bool> {
    let (mut fa, mut fb) = (File::open(a)?, File::open(b)?);
    let (mut ba, mut bb) = (vec![0u8; 1 << 16], vec![0u8; 1 << 16]);
    loop {
        let n = read_full(&mut fa, &mut ba)?;
        let m = read_full(&mut fb, &mut bb)?;
        if n != m || ba[..n] != bb[..m] {
            return Ok(false);
        }
        if n == 0 {
            return Ok(true);
        }
    }
}

fn read_full(f: &mut File, buf: &mut [u8]) -> io::Result<usize> {
    let mut got = 0;
    while got < buf.len() {
        let n = f.read(&mut buf[got..])?;
        if n == 0 {
            break;
        }
        got += n;
    }
    Ok(got)
}

#[cfg(test)]
mod tests {
    use super::*;

    struct Env {
        _t: tempfile::TempDir,
        layout: Layout,
        home: PathBuf,
        dir: PathBuf,
    }

    fn env() -> Env {
        let t = tempfile::tempdir().unwrap();
        let layout = Layout::new(t.path().join("user"));
        profile::create(&layout, "p", "latest").unwrap();
        // the Black and White profiles elsewhere: "p" alone uses the data moved
        profile::set_data_root(&layout, profile::BLACK, &t.path().join("other").to_string_lossy()).unwrap();
        profile::set_data_root(&layout, profile::WHITE, &t.path().join("other").to_string_lossy()).unwrap();
        let home = t.path().join("home");
        let src = home.join("extract");
        fs::create_dir_all(src.join("sub")).unwrap();
        fs::write(src.join(MANIFEST), b"{}").unwrap();
        fs::write(src.join("sub/a.bin"), vec![7u8; 100_000]).unwrap();
        let dir = t.path().to_path_buf();
        Env { _t: t, layout, home, dir }
    }

    fn run(e: &Env, to: &Path, no_rename: bool, hook: &dyn Fn(&Path)) -> Result<String, String> {
        let o = Options { no_rename, after_copy: hook };
        move_with(&e.layout, &e.home, "p", &to.to_string_lossy(), &o)
    }

    fn root(e: &Env) -> String {
        profile::get(&e.layout, "p").unwrap().data_root
    }

    #[test]
    fn rename_path() {
        let e = env();
        let to = e.dir.join("new/data");
        run(&e, &to, false, &|_| {}).unwrap();
        assert!(to.join("sub/a.bin").is_file());
        assert!(!e.home.join("extract").exists());
        assert_eq!(root(&e), to.to_string_lossy());
    }

    #[test]
    fn copy_path_verifies_and_deletes_the_old_folder() {
        let e = env();
        let to = e.dir.join("copied");
        fs::create_dir(&to).unwrap();
        run(&e, &to, true, &|_| {}).unwrap();
        assert_eq!(fs::read(to.join("sub/a.bin")).unwrap().len(), 100_000);
        assert!(!to.join(PARTIAL).exists());
        assert!(!e.dir.join("copied.moving").exists());
        assert!(!e.home.join("extract").exists());
        assert_eq!(root(&e), to.to_string_lossy());
        // moving again to the same place does nothing
        assert!(run(&e, &to, true, &|_| {}).is_ok());
    }

    #[test]
    fn a_bad_copy_leaves_everything_alone() {
        let e = env();
        let to = e.dir.join("bad");
        let hook = |staging: &Path| fs::write(staging.join("sub/a.bin"), vec![8u8; 100_000]).unwrap();
        let err = run(&e, &to, true, &hook).unwrap_err();
        assert!(err.contains("The old data is untouched."), "{err}");
        assert!(e.home.join("extract/sub/a.bin").is_file());
        assert!(!to.exists() && !e.dir.join("bad.moving").exists());
        assert_eq!(root(&e), "");
    }

    #[test]
    fn a_target_under_a_file_fails_cleanly() {
        let e = env();
        let blocker = e.dir.join("file");
        fs::write(&blocker, b"x").unwrap();
        assert!(run(&e, &blocker.join("data"), true, &|_| {}).is_err());
        assert!(run(&e, &blocker, true, &|_| {}).is_err());
        assert!(e.home.join("extract").join(MANIFEST).is_file());
        assert_eq!(root(&e), "");
    }

    #[test]
    fn nested_and_non_empty_targets_are_refused() {
        let e = env();
        let src = e.home.join("extract");
        assert!(run(&e, &src.join("inside"), false, &|_| {}).is_err());
        assert!(run(&e, &e.home, false, &|_| {}).is_err());
        let full = e.dir.join("full");
        fs::create_dir(&full).unwrap();
        fs::write(full.join("x"), b"x").unwrap();
        assert!(run(&e, &full, false, &|_| {}).is_err());
        assert!(run(&e, Path::new("relative"), false, &|_| {}).is_err());
        assert!(src.join(MANIFEST).is_file());
    }

    #[test]
    fn a_folder_other_profiles_use_is_copied_not_moved() {
        let e = env();
        profile::create(&e.layout, "q", "").unwrap();
        let to = e.dir.join("mine");
        let msg = run(&e, &to, false, &|_| {}).unwrap();
        assert!(msg.contains("other profiles use it"), "{msg}");
        assert!(to.join("sub/a.bin").is_file());
        assert!(e.home.join("extract").join(MANIFEST).is_file());
        assert_eq!(root(&e), to.to_string_lossy());
        assert_eq!(profile::get(&e.layout, "q").unwrap().data_root, "");
    }

    #[test]
    fn no_data_is_an_error() {
        let e = env();
        fs::remove_file(e.home.join("extract").join(MANIFEST)).unwrap();
        let err = run(&e, &e.dir.join("t"), false, &|_| {}).unwrap_err();
        assert_eq!(err, "There is no game data to move.");
    }

    #[test]
    fn the_source_follows_the_setting_and_config() {
        let home = Path::new("/h");
        assert_eq!(source_for(home, "/d"), PathBuf::from("/d"));
        assert_eq!(source_for(home, ""), home.join("extract"));
        let e = env();
        fs::write(e.home.join("config.txt"), "a=1\ndata_root=/elsewhere\n").unwrap();
        assert_eq!(source_for(&e.home, ""), PathBuf::from("/elsewhere"));
        assert_eq!(source_for(&e.home, "/d"), PathBuf::from("/d"));
    }

    #[test]
    fn the_game_home_per_system() {
        let env = |k: &str| match k {
            "LOCALAPPDATA" => Some("C:\\L".to_string()),
            "HOME" => Some("/home/u".to_string()),
            _ => None,
        };
        assert_eq!(game_home("windows", &env), Some(Path::new("C:\\L").join("eidos")));
        assert_eq!(game_home("linux", &env), Some(PathBuf::from("/home/u/.local/share/eidos")));
    }
}
