//! The player's ROMs (Settings, "ROMs"): a library of the Pokemon Black and
//! White cartridges the player has added.  Each file is told apart by its
//! SHA-1 against the dumps the source builds (aletheia docs/ROMS.md, the
//! No-Intro entries): the game, its language and whether it is a good dump.
//! The launcher remembers where each file is and copies nothing.  The game
//! is given the English Black ROM the way it remembers its own (`rom=` in
//! its `config.txt`): when the game data is missing or made by another
//! version, the game makes it again from that file without asking.

use std::fs;
use std::io::{self, Read};
use std::path::{Path, PathBuf};
use std::time::UNIX_EPOCH;

use serde::{Deserialize, Serialize};
use sha1::{Digest, Sha1};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "lowercase")]
pub enum Game {
    Black,
    White,
}

impl Game {
    /// From the first three letters of the game code (the fourth is the
    /// language).
    fn from_code(code: &[u8]) -> Option<Game> {
        match &code[..3] {
            b"IRB" => Some(Game::Black),
            b"IRA" => Some(Game::White),
            _ => None,
        }
    }
}

/// Every cartridge is this size; a trimmed dump is padded with 0xFF to it.
const CART_SIZE: u64 = 268_435_456;

/// A known dump: game code, SHA-1, the No-Intro name.
struct Known {
    code: &'static [u8; 4],
    sha1: &'static str,
    name: &'static str,
}

const KNOWN: &[Known] = &[
    Known { code: b"IRBO", sha1: "26ad0b9967aa279c4a266ee69f52b9b2332399a5", name: "Pokemon - Black Version (USA, Europe)" },
    Known { code: b"IRAO", sha1: "bc696a0dfb448c7b3a8a206f0f8214411a039208", name: "Pokemon - White Version (USA, Europe)" },
    Known { code: b"IRBF", sha1: "a559da54839663d8f8f3f03382e5665a61873d22", name: "Pokemon - Version Noire (France)" },
    Known { code: b"IRAF", sha1: "28f075915dd445cbfd902f1e1a94d5048b03a095", name: "Pokemon - Version Blanche (France)" },
    Known { code: b"IRBD", sha1: "f5688bd617be7e67a150732f42cfae6ae104b2cb", name: "Pokemon - Schwarze Edition (Germany)" },
    Known { code: b"IRAD", sha1: "fb752b211351f8480e3e1949e11a1d7741f05f5a", name: "Pokemon - Weisse Edition (Germany)" },
    Known { code: b"IRBI", sha1: "699f8c6a8390b5e30236b6c386bc3c4bd7bdc44e", name: "Pokemon - Versione Nera (Italy)" },
    Known { code: b"IRAI", sha1: "2e33164d26236dd499f75d1de3123d85e822d502", name: "Pokemon - Versione Bianca (Italy)" },
    Known { code: b"IRBS", sha1: "68b55b82f5a033f9e9666c232f429e31ed658e54", name: "Pokemon - Edicion Negra (Spain)" },
    Known { code: b"IRAS", sha1: "4cc963ca925409ff0f12f3c417d1cbe1094bf934", name: "Pokemon - Edicion Blanca (Spain)" },
    Known { code: b"IRBJ", sha1: "4ce46933ab795fc8bfc94acb7a853206b0c20b60", name: "Pocket Monsters - Black (Japan)" },
    Known { code: b"IRAJ", sha1: "e392548c1a59e4cafd7ba7a502770369410b2358", name: "Pocket Monsters - White (Japan)" },
];

/// The language of a game code's last letter.
fn language(code: u8) -> &'static str {
    match code {
        b'O' | b'E' | b'P' => "English",
        b'F' => "French",
        b'D' => "German",
        b'I' => "Italian",
        b'S' => "Spanish",
        b'J' => "Japanese",
        b'K' => "Korean",
        _ => "Unknown",
    }
}

/// One file of the library as settings.json keeps it: where it is, and its
/// SHA-1 with the size and time it was taken at, so an unchanged file is
/// not read again.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Entry {
    pub path: PathBuf,
    pub size: u64,
    pub modified: u64,
    pub sha1: String,
    /// The game code at 0x0C of the header, e.g. "IRBO".
    pub code: String,
}

/// What the page shows of a file.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct View {
    pub path: String,
    pub game: Option<Game>,
    pub language: &'static str,
    /// "verified": a known good dump; "modified": Black or White, but not a
    /// dump the source builds (edited, damaged or another revision);
    /// "missing": the file is gone from where it was.
    pub status: &'static str,
    /// The No-Intro name of a verified dump.
    pub name: Option<&'static str>,
    /// The ROM the game is given (the first verified English Black).
    pub in_use: bool,
}

fn stamp(path: &Path) -> io::Result<(u64, u64)> {
    let m = fs::metadata(path)?;
    let t = m
        .modified()
        .ok()
        .and_then(|t| t.duration_since(UNIX_EPOCH).ok())
        .map(|d| d.as_secs())
        .unwrap_or(0);
    Ok((m.len(), t))
}

/// The four-letter game code at 0x0C of a DS cartridge header.
fn game_code(path: &Path) -> io::Result<[u8; 4]> {
    let mut head = [0u8; 16];
    fs::File::open(path)?.read_exact(&mut head)?;
    Ok([head[12], head[13], head[14], head[15]])
}

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

/// The SHA-1 of the file, and of it padded with 0xFF to the cartridge size
/// when it is shorter (a trimmed dump).
fn hashes(path: &Path) -> io::Result<(String, Option<String>)> {
    let mut f = fs::File::open(path)?;
    let mut sha = Sha1::new();
    let mut buf = vec![0u8; 1 << 20];
    let mut total = 0u64;
    loop {
        let n = f.read(&mut buf)?;
        if n == 0 {
            break;
        }
        sha.update(&buf[..n]);
        total += n as u64;
    }
    let padded = if total < CART_SIZE {
        let mut p = sha.clone();
        buf.fill(0xFF);
        let mut left = CART_SIZE - total;
        while left > 0 {
            let n = left.min(buf.len() as u64) as usize;
            p.update(&buf[..n]);
            left -= n as u64;
        }
        Some(hex(&p.finalize()))
    } else {
        None
    };
    Ok((hex(&sha.finalize()), padded))
}

/// Reads a file into a library entry: refused unless it is Pokemon Black
/// or White.  A good trimmed dump keeps the SHA-1 of its padded form.
pub fn read(path: &Path) -> Result<Entry, String> {
    if !path.is_file() {
        return Err(format!("{} is not a file.", path.display()));
    }
    let code = game_code(path).map_err(|e| format!("Could not read {}: {e}", path.display()))?;
    if Game::from_code(&code).is_none() {
        return Err("That file is not Pokemon Black or White.".to_string());
    }
    let (size, modified) = stamp(path).map_err(|e| e.to_string())?;
    let (sha1, padded) = hashes(path).map_err(|e| format!("Could not read {}: {e}", path.display()))?;
    let sha1 = match padded {
        Some(p) if KNOWN.iter().any(|k| k.sha1 == p) => p,
        _ => sha1,
    };
    Ok(Entry { path: path.to_path_buf(), size, modified, sha1, code: String::from_utf8_lossy(&code).into_owned() })
}

/// The entry again if its file changed since (or is gone: None).
pub fn refresh(e: &Entry) -> Option<Entry> {
    match stamp(&e.path) {
        Ok((size, modified)) if size == e.size && modified == e.modified => Some(e.clone()),
        Ok(_) => read(&e.path).ok(),
        Err(_) => None,
    }
}

fn known(e: &Entry) -> Option<&'static Known> {
    KNOWN.iter().find(|k| k.sha1 == e.sha1 && k.code == e.code.as_bytes())
}

/// The ROM the game plays: the first good English Black dump present.
pub fn for_game(lib: &[Entry]) -> Option<&Entry> {
    lib.iter().find(|e| e.code == "IRBO" && known(e).is_some() && e.path.is_file())
}

/// The page's view of the library, in its order.
pub fn views(lib: &[Entry]) -> Vec<View> {
    let playing = for_game(lib).map(|e| e.path.clone());
    lib.iter()
        .map(|e| {
            let code = e.code.as_bytes();
            let k = known(e);
            View {
                path: e.path.to_string_lossy().into_owned(),
                game: if code.len() == 4 { Game::from_code(code) } else { None },
                language: if code.len() == 4 { language(code[3]) } else { "Unknown" },
                status: if !e.path.is_file() {
                    "missing"
                } else if k.is_some() {
                    "verified"
                } else {
                    "modified"
                },
                name: k.map(|k| k.name),
                in_use: playing.as_ref() == Some(&e.path),
            }
        })
        .collect()
}

/// The `.nds` files a path holds: itself, or those in a folder (not in its
/// subfolders).
pub fn files(path: &Path) -> Vec<PathBuf> {
    if path.is_dir() {
        let mut out: Vec<PathBuf> = fs::read_dir(path)
            .map(|rd| {
                rd.filter_map(|d| d.ok().map(|d| d.path()))
                    .filter(|p| {
                        p.is_file()
                            && p.extension().is_some_and(|x| x.eq_ignore_ascii_case("nds"))
                    })
                    .collect()
            })
            .unwrap_or_default();
        out.sort();
        out
    } else {
        vec![path.to_path_buf()]
    }
}

/// Writes `rom=` into the game's `config.txt` in `home`, keeping its other
/// lines; None removes the line.
pub fn remember(home: &Path, rom: Option<&Path>) -> io::Result<()> {
    let file = home.join("config.txt");
    let old = fs::read_to_string(&file).unwrap_or_default();
    let mut text: String = old
        .lines()
        .filter(|l| !l.trim_start().starts_with("rom="))
        .map(|l| format!("{l}\n"))
        .collect();
    if let Some(p) = rom {
        if text.is_empty() {
            text.push_str("# eidos: where the game data is and which ROM it came from.\n");
        }
        text.push_str(&format!("rom={}\n", p.display()));
    }
    if text == old {
        return Ok(());
    }
    fs::create_dir_all(home)?;
    let tmp = file.with_extension("tmp");
    fs::write(&tmp, text)?;
    fs::rename(tmp, file)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn rom(dir: &Path, name: &str, code: &[u8; 4]) -> PathBuf {
        let mut head = vec![0u8; 512];
        head[12..16].copy_from_slice(code);
        let p = dir.join(name);
        fs::write(&p, head).unwrap();
        p
    }

    #[test]
    fn reads_black_and_white_only() {
        let t = tempfile::tempdir().unwrap();
        let b = rom(t.path(), "b.nds", b"IRBF");
        let x = rom(t.path(), "x.nds", b"ADAE");
        let e = read(&b).unwrap();
        assert_eq!(e.code, "IRBF");
        assert_eq!(read(&x).unwrap_err(), "That file is not Pokemon Black or White.");
        assert!(read(&t.path().join("none.nds")).is_err());
        let v = views(&[e.clone()]);
        assert_eq!(v[0].game, Some(Game::Black));
        assert_eq!(v[0].language, "French");
        assert_eq!(v[0].status, "modified");    // a header alone is no good dump
        assert!(!v[0].in_use);
        fs::remove_file(&b).unwrap();
        assert_eq!(views(&[e.clone()])[0].status, "missing");
        assert!(refresh(&e).is_none());
    }

    #[test]
    fn a_known_dump_is_verified_and_given_to_the_game() {
        let e = Entry {
            path: std::env::current_exe().unwrap(),
            size: 0,
            modified: 0,
            sha1: "26ad0b9967aa279c4a266ee69f52b9b2332399a5".into(),
            code: "IRBO".into(),
        };
        let v = views(&[e.clone()]);
        assert_eq!(v[0].status, "verified");
        assert_eq!(v[0].name, Some("Pokemon - Black Version (USA, Europe)"));
        assert!(v[0].in_use);
        assert_eq!(for_game(&[e]).map(|e| e.code.as_str()), Some("IRBO"));
    }

    /// Real dumps, when EIDOS_TEST_ROMS names a folder holding some: each
    /// is read and checked as the page would.
    #[test]
    #[ignore]
    fn real_dumps() {
        let dir = std::env::var("EIDOS_TEST_ROMS").expect("EIDOS_TEST_ROMS");
        let lib: Vec<Entry> = files(Path::new(&dir)).iter().filter_map(|p| read(p).ok()).collect();
        assert!(!lib.is_empty());
        for v in views(&lib) {
            println!("{} {:?} {} {} {}", v.path, v.game, v.language, v.status, v.in_use);
            assert_eq!(v.status, "verified");
        }
    }

    #[test]
    fn a_folder_gives_its_nds_files() {
        let t = tempfile::tempdir().unwrap();
        rom(t.path(), "a.nds", b"IRBO");
        rom(t.path(), "b.NDS", b"IRAO");
        fs::write(t.path().join("c.txt"), "x").unwrap();
        assert_eq!(files(t.path()).len(), 2);
    }

    #[test]
    fn remember_keeps_the_other_lines() {
        let t = tempfile::tempdir().unwrap();
        let cfg = t.path().join("config.txt");
        fs::write(&cfg, "# x\ndata_root=/d\nrom=/old.nds\n").unwrap();
        remember(t.path(), Some(Path::new("/new.nds"))).unwrap();
        assert_eq!(fs::read_to_string(&cfg).unwrap(), "# x\ndata_root=/d\nrom=/new.nds\n");
        remember(t.path(), None).unwrap();
        assert_eq!(fs::read_to_string(&cfg).unwrap(), "# x\ndata_root=/d\n");
        let fresh = t.path().join("fresh");
        remember(&fresh, Some(Path::new("/b.nds"))).unwrap();
        assert!(fs::read_to_string(fresh.join("config.txt")).unwrap().ends_with("rom=/b.nds\n"));
    }
}
