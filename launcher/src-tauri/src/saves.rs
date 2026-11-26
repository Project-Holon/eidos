//! A profile's saves, read from the files eidos writes (sdk/save_slots.c):
//! slot 1 is `<saves>/pokeblack.sav`, slots 2 and on are
//! `<saves>/slots/save<n>.sav` (there is no limit), and `<saves>/slot.txt`
//! holds the slot the game opens on. The Multiplayer screen's games are
//! in `<saves>/games/` and are not the player's own saves. The launcher
//! reads the files with morphe's save reader and writes only `slot.txt`,
//! the way eidos's own title menu does.

use std::fs;
use std::io;
use std::path::{Path, PathBuf};
use std::time::UNIX_EPOCH;

use nds::save::{Game, Save};
use serde::Serialize;

/// The slots of the player's own that have a file, in order: slot 1 and
/// each `slots/save<n>.sav`.
fn slot_numbers(saves: &Path) -> Vec<u32> {
    let mut out: Vec<u32> = fs::read_dir(saves.join("slots"))
        .into_iter()
        .flatten()
        .flatten()
        .filter_map(|e| {
            let name = e.file_name().into_string().ok()?;
            let n = name.strip_prefix("save")?.strip_suffix(".sav")?;
            (!n.starts_with('0') && n.bytes().all(|b| b.is_ascii_digit())).then(|| n.parse::<u32>().ok())?
        })
        .filter(|&n| n >= 2)
        .collect();
    if saves.join("pokeblack.sav").is_file() {
        out.push(1);
    }
    out.sort_unstable();
    out.dedup();
    out
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PartyMember {
    pub species: u16,
    pub egg: bool,
    /// The nickname, or the species name in the save's language.
    pub name: String,
    pub level: u8,
    pub hp: u16,
    pub max_hp: u16,
}

/// One save as the Home page lists it.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SaveView {
    pub slot: u32,
    /// The slot the game opens on.
    pub current: bool,
    pub trainer: String,
    pub female: bool,
    /// Play time in seconds.
    pub seconds: u32,
    /// How many badges.
    pub badges: u8,
    /// One bit per badge, in the order the game awards them.
    pub badge_bits: u8,
    pub party: Vec<PartyMember>,
    /// The zone the player saved in; `place` is its name, from a ROM of
    /// the save's game ("" when the library has none).
    pub zone: u16,
    pub place: String,
    /// When the file was last written, seconds since 1970.
    pub saved: u64,
    /// "black" or "white".
    pub game: &'static str,
    /// MyStatus language: 1 Japanese, 2 English, 3 French, 4 Italian,
    /// 5 German, 7 Spanish, 8 Korean.
    pub language: u8,
}

pub fn slot_path(saves: &Path, n: u32) -> PathBuf {
    if n == 1 {
        saves.join("pokeblack.sav")
    } else {
        saves.join("slots").join(format!("save{n}.sav"))
    }
}

/// The slot in slot.txt, 1 when the file is missing or wrong (as eidos
/// reads it, bw_slots_current); a game of `games/` (-k) is none of the
/// player's own: None.
pub fn slot_in_file(saves: &Path) -> Option<u32> {
    let n = fs::read_to_string(saves.join("slot.txt"))
        .ok()
        .and_then(|t| t.split_whitespace().next()?.parse::<i64>().ok())
        .filter(|&n| n != 0)
        .unwrap_or(1);
    u32::try_from(n).ok()
}

fn modified(path: &Path) -> u64 {
    fs::metadata(path)
        .and_then(|m| m.modified())
        .ok()
        .and_then(|t| t.duration_since(UNIX_EPOCH).ok())
        .map_or(0, |d| d.as_secs())
}

fn view(slot: u32, s: Save, saved: u64) -> SaveView {
    SaveView {
        slot,
        current: false,
        trainer: s.trainer,
        female: s.female,
        seconds: s.seconds,
        badges: s.badges.count_ones() as u8,
        badge_bits: s.badges,
        party: s
            .party
            .into_iter()
            .map(|m| PartyMember {
                species: m.species,
                egg: m.egg,
                name: m.name,
                level: m.level,
                hp: m.hp,
                max_hp: m.max_hp,
            })
            .collect(),
        zone: s.zone,
        place: String::new(),
        saved,
        game: if s.game == Some(Game::White) {
            "white"
        } else {
            "black"
        },
        language: s.language,
    }
}

/// The slots an older eidos's Multiplayer screen cards made (a mod's mode:
/// a solo randomizer, a world made or joined) among the player's own:
/// `<saves>/games.ini`, one line a slot, `<slot> <kind> <mod> <mode> <role>
/// <code>`. eidos keeps them out of the title's Continue card, and its tidy
/// step moves them to `games/` at the next start (sdk/save_tidy.h); until
/// then they are not the player's own saves here either.
pub fn mode_slots(saves: &Path) -> Vec<u32> {
    fs::read_to_string(saves.join("games.ini"))
        .unwrap_or_default()
        .lines()
        .filter(|l| !l.starts_with('#') && l.split_whitespace().count() >= 6)
        .filter_map(|l| l.split_whitespace().next()?.parse::<u32>().ok())
        .filter(|&n| n >= 1)
        .collect()
}

/// Every slot that holds one of the player's own saves, in slot order. A
/// file that is not a save the game would load (a new game left before its
/// first save, a damaged file) is left out, as the title menu leaves it
/// out, and so is a save a Multiplayer screen card made (`mode_slots`).
pub fn list(saves: &Path) -> Vec<SaveView> {
    let modes = mode_slots(saves);
    let mut out: Vec<SaveView> = slot_numbers(saves)
        .into_iter()
        .filter(|n| !modes.contains(n))
        .filter_map(|n| {
            let path = slot_path(saves, n);
            let bytes = fs::read(&path).ok()?;
            let save = nds::save::read(&bytes).ok()??;
            Some(view(n, save, modified(&path)))
        })
        .collect();
    // The slot the game opens on: slot.txt's, or when that slot holds a
    // file the game cannot load or a mode's save (or names a game of
    // games/), the newest save (bw_slots_settle; the Continue card offers
    // no mode save).
    let in_file = slot_in_file(saves);
    let used = in_file.is_none_or(|n| slot_path(saves, n).is_file() || modes.contains(&n));
    let current = if out.iter().any(|s| Some(s.slot) == in_file) || !used {
        in_file
    } else {
        out.iter().max_by_key(|s| s.saved).map(|s| s.slot)
    };
    for s in &mut out {
        s.current = Some(s.slot) == current;
    }
    out
}

/// Makes `slot` the one the game opens on. The slot must hold a save.
pub fn set_current(saves: &Path, slot: u32) -> Result<(), String> {
    if slot < 1 {
        return Err(format!("There is no save slot {slot}."));
    }
    if !list(saves).iter().any(|s| s.slot == slot) {
        return Err(format!("Slot {slot} holds no save."));
    }
    write_atomic(&saves.join("slot.txt"), format!("{slot}\n").as_bytes())
        .map_err(|e| format!("Could not choose the save: {e}"))
}

fn write_atomic(path: &Path, bytes: &[u8]) -> io::Result<()> {
    let tmp = path.with_extension("txt.tmp");
    fs::write(&tmp, bytes)?;
    fs::rename(&tmp, path).inspect_err(|_| {
        let _ = fs::remove_file(&tmp);
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn dir(name: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!(
            "eidos-launcher-saves-{name}-{}",
            std::process::id()
        ));
        let _ = fs::remove_dir_all(&d);
        fs::create_dir_all(d.join("slots")).unwrap();
        d
    }

    #[test]
    fn no_files_no_saves_slot_one() {
        let d = dir("empty");
        assert!(list(&d).is_empty());
        assert_eq!(slot_in_file(&d), Some(1));
        assert!(set_current(&d, 1).is_err());
        assert!(set_current(&d, 9).is_err());
    }

    #[test]
    fn slot_file_rules() {
        let d = dir("slotfile");
        fs::write(d.join("slot.txt"), "3\n").unwrap();
        assert_eq!(slot_in_file(&d), Some(3));
        // no limit on the slots
        fs::write(d.join("slot.txt"), "12").unwrap();
        assert_eq!(slot_in_file(&d), Some(12));
        // a game of games/ is none of the player's own
        fs::write(d.join("slot.txt"), "-2\n").unwrap();
        assert_eq!(slot_in_file(&d), None);
        fs::write(d.join("slot.txt"), "0").unwrap();
        assert_eq!(slot_in_file(&d), Some(1));
        // a file that is not a save is not listed
        fs::write(slot_path(&d, 2), vec![0u8; nds::save::IMAGE_SIZE]).unwrap();
        assert!(list(&d).is_empty());
        assert!(set_current(&d, 2).is_err());
    }

    #[test]
    fn mode_saves_are_not_listed() {
        let d = dir("modes");
        fs::write(
            d.join("games.ini"),
            "# comment\n3 solo arkhipelagos solo host -\n5 world arkhipelagos multiworld joined ABCD\nbad line\n12 solo x y host -\n",
        )
        .unwrap();
        assert_eq!(mode_slots(&d), vec![3, 5, 12]);
        assert!(set_current(&d, 3).is_err());
        assert!(mode_slots(&dir("nomodes")).is_empty());
    }

    #[test]
    fn slots_past_eight_are_found() {
        let d = dir("many");
        for n in [2u32, 9, 12, 40] {
            fs::write(slot_path(&d, n), b"x").unwrap();
        }
        fs::write(d.join("slots/save09.sav"), b"x").unwrap(); // not a slot's name
        fs::write(d.join("slots/notes.txt"), b"x").unwrap();
        fs::create_dir_all(d.join("games")).unwrap();
        fs::write(d.join("games/game1.sav"), b"x").unwrap(); // a Multiplayer screen game
        assert_eq!(slot_numbers(&d), vec![2, 9, 12, 40]);
        fs::write(d.join("pokeblack.sav"), b"x").unwrap();
        assert_eq!(slot_numbers(&d), vec![1, 2, 9, 12, 40]);
    }

    /// A real profile's saves folder in EIDOS_SAVES; without it, nothing.
    #[test]
    fn real_profile() {
        let Some(d) = std::env::var_os("EIDOS_SAVES") else {
            return;
        };
        for s in list(Path::new(&d)) {
            eprintln!("{s:?}");
        }
    }
}
