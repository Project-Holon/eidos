//! The saves of a plain eidos from before the game profiles.
//!
//! A game started without the launcher kept its saves in the user folder
//! itself (`<root>/saves`).  From 0.2.7 it plays from the two game profiles,
//! as the launcher does, and copies those old saves into their game's
//! profile at its first start (the game's `install/profiles.c`: into the
//! profile, into its free save slots when it has saves of its own, or into
//! a profile of their own).  A player who goes straight to the launcher
//! never makes that start, so the launcher asks the game to do the same
//! work: `eidos --import-old-saves` copies and ends, and says in one line
//! what it did.  The rules stay in one place, the game's.

use std::fs;
use std::path::Path;
use std::process::Command;

use crate::versions;

/// The file the game leaves in the old saves folder once they are copied.
const COPIED: &str = "copied-to-profiles.txt";
/// The first version whose game knows `--import-old-saves`.
const FIRST: &str = "0.2.7";

fn has_entries(dir: &Path) -> bool {
    fs::read_dir(dir).map(|mut rd| rd.next().is_some()).unwrap_or(false)
}

/// Whether a saves folder holds a game: slot 1's file, another slot's, a
/// Multiplayer screen game or a multiplayer game (the game's
/// `bw_profile_has_saves`).
fn has_saves(saves: &Path) -> bool {
    saves.join("pokeblack.sav").is_file()
        || has_entries(&saves.join("slots"))
        || has_entries(&saves.join("games"))
        || has_entries(&saves.join("multiplayer"))
}

/// Whether the user folder holds old saves that were not copied yet.
pub fn pending(root: &Path) -> bool {
    let saves = root.join("saves");
    has_saves(&saves) && !saves.join(COPIED).exists()
}

/// Whether a version's game can do the import: a local build, or 0.2.7 and
/// later.
pub fn can_import(id: &str) -> bool {
    id == versions::LOCAL || versions::compare(id, FIRST) != std::cmp::Ordering::Less
}

/// The game's own line about the import, without its "eidos: " prefix: the
/// line that names the saves.  Empty when the game said nothing about them.
pub fn note_of(output: &str) -> String {
    output
        .lines()
        .filter_map(|l| l.trim().strip_prefix("eidos: "))
        .find(|l| l.starts_with("The saves in ") || l.starts_with("no old saves"))
        .unwrap_or("")
        .to_string()
}

/// The first version whose game knows `--tidy-saves`.
const FIRST_TIDY: &str = "0.2.8";

/// Whether a version's game can put a saves folder in order: a local
/// build, or 0.2.8 and later.
pub fn can_tidy(id: &str) -> bool {
    id == versions::LOCAL || versions::compare(id, FIRST_TIDY) != std::cmp::Ordering::Less
}

/// Runs the game's tidy step on one saves folder (the game's rules,
/// sdk/save_tidy.h: Multiplayer screen games to games/, files with no save
/// set aside in unreadable/, the saves with no gap) and returns its line,
/// empty when the folder was in order.
pub fn tidy(exe: &Path, saves: &Path) -> Result<String, String> {
    if !saves.is_dir() {
        return Ok(String::new());
    }
    let out = Command::new(exe)
        .arg("--tidy-saves")
        .arg(saves)
        .output()
        .map_err(|e| format!("Could not start eidos to put the saves in order: {e}"))?;
    let text = String::from_utf8_lossy(&out.stderr).into_owned();
    let line = text
        .lines()
        .filter_map(|l| l.trim().strip_prefix("eidos: "))
        .find(|l| l.starts_with("The saves"))
        .unwrap_or("")
        .to_string();
    if !out.status.success() {
        return Err(if line.is_empty() { "eidos could not put the saves in order.".into() } else { line });
    }
    Ok(if line.contains("in order already") { String::new() } else { line })
}

/// Runs the game's import and returns its line.
pub fn import(exe: &Path) -> Result<String, String> {
    let out = Command::new(exe)
        .arg("--import-old-saves")
        .output()
        .map_err(|e| format!("Could not start eidos to import the old saves: {e}"))?;
    if !out.status.success() {
        return Err("eidos could not import the old saves.".into());
    }
    let mut text = String::from_utf8_lossy(&out.stderr).into_owned();
    text.push('\n');
    text.push_str(&String::from_utf8_lossy(&out.stdout));
    Ok(note_of(&text))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn pending_saves() {
        let t = tempfile::tempdir().unwrap();
        let root = t.path();
        assert!(!pending(root));
        fs::create_dir_all(root.join("saves/slots")).unwrap();
        fs::create_dir_all(root.join("saves/multiplayer")).unwrap();
        // empty folders are no saves
        assert!(!pending(root));
        fs::write(root.join("saves/slots/save3.sav"), b"x").unwrap();
        assert!(pending(root));
        fs::remove_file(root.join("saves/slots/save3.sav")).unwrap();
        fs::create_dir_all(root.join("saves/multiplayer/Friends")).unwrap();
        assert!(pending(root));
        fs::write(root.join("saves/pokeblack.sav"), b"x").unwrap();
        assert!(pending(root));
        // copied once: never again
        fs::write(root.join("saves").join(COPIED), b"x").unwrap();
        assert!(!pending(root));
    }

    #[test]
    fn versions_that_import() {
        assert!(can_import("0.2.7"));
        assert!(can_import("0.3.0"));
        assert!(can_import(versions::LOCAL));
        assert!(!can_import("0.2.6"));
        assert!(!can_import("0.1.9"));
    }

    #[test]
    fn the_games_line() {
        let out = "eidos: The saves in C:/u/saves were copied to free save slots of C:/u/profiles/Black/saves.\n\
                   eidos: Pokemon Black (IRBO) from C:/x; saves in C:/y\n";
        assert_eq!(
            note_of(out),
            "The saves in C:/u/saves were copied to free save slots of C:/u/profiles/Black/saves."
        );
        assert_eq!(note_of("eidos: Pokemon Black (IRBO) from C:/x\n"), "");
        assert_eq!(note_of(""), "");
    }
}
