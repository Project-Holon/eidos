//! The eidos launcher's shell: profiles, versions, mods, Play and invite
//! links. The pages are in the front end; this file holds the state and the
//! commands the pages call.
//!
//! TODO (later, docs/distribution.md "Joining a game with other versions or
//! mods"): "Get and join" for an invite link. It needs the host's version
//! and mod list in the invite, and the mods index. Today an invite link only
//! launches the chosen profile with the join argument.

mod catalog;
mod datamove;
mod ghdownload;
mod launch;
mod layout;
mod mods;
#[cfg(desktop)]
mod pad;
mod profile;
mod remote;
mod rom;
mod saves;
mod settings;
mod versions;

use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};

use serde::Serialize;
use tauri::{Emitter, Manager, State};
use tauri_plugin_deep_link::DeepLinkExt;

use layout::Layout;
use settings::Settings;

struct App {
    layout: Layout,
    settings: Mutex<Settings>,
    /// Profile name to process id, for the games that run now.
    running: Arc<Mutex<HashMap<String, u32>>>,
    /// An invite link that arrived before the page asked for it.
    pending_join: Mutex<Option<String>>,
}

impl App {
    fn settings(&self) -> Settings {
        self.settings.lock().unwrap().clone()
    }

    fn is_running(&self, name: &str) -> bool {
        self.running.lock().unwrap().contains_key(name)
    }

    fn refuse_if_running(&self, name: &str) -> Result<(), String> {
        if self.is_running(name) {
            Err(format!("{name} is running. Close the game first."))
        } else {
            Ok(())
        }
    }
}

type R<T> = Result<T, String>;

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct Overview {
    profiles: Vec<profile::Profile>,
    installed: Vec<String>,
    local_build: Option<String>,
    local_ok: bool,
    running: Vec<String>,
    launcher_version: &'static str,
    root: String,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct SettingsView {
    repo: String,
    token_set: bool,
    index_url: String,
    local_build: Option<String>,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct Remote {
    tag: String,
    id: Option<String>,
    name: String,
    prerelease: bool,
    published: Option<String>,
    size: Option<u64>,
    /// The release notes (Markdown), when the release list gave them.
    notes: Option<String>,
    /// The release has a file for this platform and a SHA256SUMS file.
    available: bool,
    installed: bool,
}

#[tauri::command]
fn overview(app: State<App>) -> R<Overview> {
    let s = app.settings();
    for p in profile::list(&app.layout).map_err(|e| e.to_string())? {
        fit_profile(&app, &p.name);
    }
    let profiles = profile::list(&app.layout).map_err(|e| e.to_string())?;
    let local_ok = s
        .local_build
        .as_deref()
        .is_some_and(|d| versions::find_exe(d).is_some());
    let mut running: Vec<String> = app.running.lock().unwrap().keys().cloned().collect();
    running.sort();
    Ok(Overview {
        profiles,
        installed: versions::installed(&app.layout),
        local_build: s.local_build.map(|p| p.to_string_lossy().into_owned()),
        local_ok,
        running,
        launcher_version: env!("CARGO_PKG_VERSION"),
        root: app.layout.root.to_string_lossy().into_owned(),
    })
}

#[tauri::command]
fn create_profile(app: State<App>, name: String, version: String) -> R<profile::Profile> {
    profile::create(&app.layout, &name, &version)?;
    fit_profile(&app, &name);
    profile::get(&app.layout, &name)
}

/// A profile whose game and language have no ROM plays the one the library
/// has, when there is only one to play (rom::fit): a library of Italian
/// Black alone makes every profile Italian Black, with nothing to choose.
fn fit_profile(app: &App, name: &str) {
    let Ok(p) = profile::get(&app.layout, name) else {
        return;
    };
    if app.running.lock().unwrap().contains_key(name) {
        return;
    }
    if let Some((game, language)) = rom::fit_in(&app.settings().roms, &p.game, &p.language, profile::is_game_profile(name)) {
        let _ = profile::set_game(&app.layout, name, game);
        let _ = profile::set_language(&app.layout, name, language);
    }
}

#[tauri::command]
fn rename_profile(app: State<App>, from: String, to: String) -> R<()> {
    app.refuse_if_running(&from)?;
    profile::rename(&app.layout, &from, &to)
}

#[tauri::command]
fn duplicate_profile(app: State<App>, name: String) -> R<profile::Profile> {
    app.refuse_if_running(&name)?;
    profile::duplicate(&app.layout, &name)
}

#[tauri::command]
fn delete_profile(app: State<App>, name: String) -> R<()> {
    app.refuse_if_running(&name)?;
    profile::delete(&app.layout, &name)
}

#[tauri::command]
fn set_profile_version(app: State<App>, name: String, version: String) -> R<()> {
    profile::set_version(&app.layout, &name, &version)
}

#[tauri::command]
fn set_profile_game(app: State<App>, name: String, game: String) -> R<()> {
    app.refuse_if_running(&name)?;
    profile::set_game(&app.layout, &name, &game)
}

#[tauri::command]
fn set_profile_language(app: State<App>, name: String, language: String) -> R<()> {
    app.refuse_if_running(&name)?;
    profile::set_language(&app.layout, &name, &language)
}

#[tauri::command]
fn set_profile_data_root(app: State<App>, name: String, path: String) -> R<()> {
    app.refuse_if_running(&name)?;
    profile::set_data_root(&app.layout, &name, &path)
}

/// Moves the profile's game data folder; the answer says what happened.
#[tauri::command]
async fn move_profile_data(app: State<'_, App>, name: String, to: String) -> R<String> {
    app.refuse_if_running(&name)?;
    let layout = app.layout.clone();
    tauri::async_runtime::spawn_blocking(move || datamove::move_profile_data(&layout, &name, &to))
        .await
        .map_err(|e| e.to_string())?
}

/// The releases of the eidos repository, with what this platform can install.
#[tauri::command]
async fn list_remote(app: State<'_, App>) -> R<Vec<Remote>> {
    let s = app.settings();
    // test builds only when the player asked for them (Settings > Updates)
    let s2_pre = s.pre_releases;
    let installed = versions::installed(&app.layout);
    let pattern = versions::asset_pattern(std::env::consts::OS);
    let layout = app.layout.clone();
    let list = tauri::async_runtime::spawn_blocking(move || {
        remote::list(&layout, &s.repo, s.token())
    })
    .await
    .map_err(|e| e.to_string())??;
    Ok(list
        .iter()
        .filter(|r| s2_pre || !r.prerelease)
        .map(|r| {
            let asset = releases::pick_asset(r, pattern);
            let sums = releases::pick_asset(r, "SHA256SUMS");
            let id = versions::version_id(&r.tag_name);
            Remote {
                tag: r.tag_name.clone(),
                installed: id.as_ref().is_some_and(|i| installed.contains(i)),
                available: asset.is_some() && sums.is_some() && id.is_some(),
                id,
                name: r.name.clone().filter(|n| !n.is_empty()).unwrap_or_else(|| r.tag_name.clone()),
                prerelease: r.prerelease,
                published: r.published_at.clone(),
                // 0: a release known from the feed only, size unknown
                size: asset.map(|a| a.size).filter(|&n| n > 0),
                notes: r.body.clone().filter(|b| !b.trim().is_empty()),
            }
        })
        .collect())
}

fn install_blocking(
    layout: &Layout,
    s: &Settings,
    tag: &str,
    stage: &dyn Fn(&str),
) -> R<()> {
    let id = versions::version_id(tag).ok_or("That version name cannot be installed.")?;
    let dest = layout.version(&id);
    if dest.exists() {
        return Err(format!("Version {id} is already installed."));
    }
    stage("Looking up the release");
    let list = remote::list(layout, &s.repo, s.token())?;
    let release = list
        .iter()
        .find(|r| r.tag_name == tag)
        .ok_or("The release is not in the list.")?;
    let asset = releases::pick_asset(release, versions::asset_pattern(std::env::consts::OS))
        .ok_or("The release has no file for this system.")?;
    let sums_asset = releases::pick_asset(release, "SHA256SUMS")
        .ok_or("The release has no SHA256SUMS file, so the download cannot be checked.")?;

    let tmp = layout.launcher().join("downloads").join(&id);
    let _ = std::fs::remove_dir_all(&tmp);
    std::fs::create_dir_all(&tmp).map_err(|e| e.to_string())?;
    let result = (|| -> R<()> {
        stage("Downloading the checksums");
        let sums_path = tmp.join("SHA256SUMS");
        ghdownload::download(sums_asset, s.token(), &sums_path, None)?;
        let sums = std::fs::read_to_string(&sums_path).map_err(|e| e.to_string())?;
        let want = releases::checksum_for(&sums, &asset.name)
            .ok_or_else(|| format!("SHA256SUMS has no entry for {}.", asset.name))?;
        stage("Downloading eidos");
        let file = tmp.join(&asset.name);
        ghdownload::download(asset, s.token(), &file, Some(&want))?;
        stage("Unpacking");
        if asset.name.to_lowercase().ends_with(".zip") {
            versions::unzip(&file, &dest).map_err(|e| e.to_string())
        } else {
            std::fs::create_dir_all(&dest).map_err(|e| e.to_string())?;
            let target = dest.join("eidos.AppImage");
            std::fs::copy(&file, &target).map_err(|e| e.to_string())?;
            #[cfg(unix)]
            {
                use std::os::unix::fs::PermissionsExt;
                std::fs::set_permissions(&target, std::fs::Permissions::from_mode(0o755))
                    .map_err(|e| e.to_string())?;
            }
            Ok(())
        }
    })();
    let _ = std::fs::remove_dir_all(&tmp);
    result?;
    if versions::find_exe(&dest).is_none() {
        let _ = std::fs::remove_dir_all(&dest);
        return Err("The download has no eidos program in it.".into());
    }
    Ok(())
}

#[tauri::command]
async fn install_version(app: State<'_, App>, handle: tauri::AppHandle, tag: String) -> R<()> {
    let layout = app.layout.clone();
    let s = app.settings();
    tauri::async_runtime::spawn_blocking(move || {
        let t = tag.clone();
        let emit = move |stage: &str| {
            let _ = handle.emit("install-progress", (t.clone(), stage.to_string()));
        };
        install_blocking(&layout, &s, &tag, &emit)
    })
    .await
    .map_err(|e| e.to_string())?
}

#[tauri::command]
fn remove_version(app: State<App>, id: String) -> R<()> {
    if versions::version_id(&id).as_deref() != Some(id.as_str()) {
        return Err("That is not a version name.".into());
    }
    let used: Vec<String> = profile::list(&app.layout)
        .map_err(|e| e.to_string())?
        .into_iter()
        .filter(|p| p.version == id)
        .map(|p| p.name)
        .collect();
    if let Some(name) = used.first() {
        return Err(format!("The profile {name} uses version {id}. Change it first."));
    }
    let dir = app.layout.version(&id);
    if !dir.is_dir() {
        return Err(format!("Version {id} is not installed."));
    }
    std::fs::remove_dir_all(dir).map_err(|e| e.to_string())
}

/// What `play` started: the version it ran.
#[derive(Serialize)]
struct Started {
    version: String,
}

#[tauri::command]
fn play(
    app: State<App>,
    handle: tauri::AppHandle,
    name: String,
    join: Option<String>,
    layout: Option<String>,
) -> R<Started> {
    app.refuse_if_running(&name)?;
    fit_profile(&app, &name);
    let p = profile::get(&app.layout, &name)?;
    let s = app.settings();
    let v = versions::resolve(&app.layout, s.local_build.as_deref(), &p.version)?;
    let join = match join {
        Some(code) => Some(
            launch::parse_invite(&launch::invite_url(&code)).ok_or("That join code is not valid.")?,
        ),
        None => None,
    };
    let layout = match layout.as_deref() {
        None => launch::Layout::Classic,
        Some(text) => launch::Layout::parse(text).ok_or("That screen layout is not known.")?,
    };
    // the profile's game and language: their ROM to the game, and their own
    // game data folder (English Black keeps the default one)
    let code = rom::code_for(&p.game, &p.language)
        .ok_or("eidos does not play that language yet.")?;
    let chosen = rom::with_code(&s.roms, &code).ok_or_else(|| {
        let have: Vec<String> = rom::playable(&s.roms).iter().map(|e| rom::describe(&e.code)).collect();
        if have.is_empty() {
            format!("This profile plays {}, and there is no ROM yet. Add one in Settings, ROMs.", rom::describe(&code))
        } else {
            format!(
                "This profile plays {}, and your ROMs are {}. Change the profile's game or language in Profiles, or add that ROM in Settings, ROMs.",
                rom::describe(&code),
                have.join("; ")
            )
        }
    })?;
    let home = game_home();
    if let Some(home) = &home {
        let _ = rom::remember(home, Some(&chosen.path));
    }
    let mut data_root = p.data_root.clone();
    if data_root.is_empty() && code != "IRBO" {
        if let Some(home) = &home {
            data_root = home.join(format!("extract-{code}")).to_string_lossy().into_owned();
        }
    }
    let dir = app.layout.profile(&name);
    let args = launch::eidos_args(&dir, &data_root, join.as_deref(), layout);
    let mut child = launch::spawn(&v.exe, &args).map_err(|e| format!("Could not start eidos: {e}"))?;
    app.running.lock().unwrap().insert(name.clone(), child.id());
    let running = app.running.clone();
    std::thread::spawn(move || {
        let _ = child.wait();
        running.lock().unwrap().remove(&name);
        let _ = handle.emit("running-changed", ());
    });
    Ok(Started { version: v.id })
}

fn profile_dir(app: &App, name: &str) -> R<PathBuf> {
    profile::get(&app.layout, name)?;
    Ok(app.layout.profile(name))
}

#[tauri::command]
fn mods_list(app: State<App>, profile: String) -> R<Vec<mods::ModInfo>> {
    Ok(mods::list(&profile_dir(&app, &profile)?))
}

#[tauri::command]
fn mods_set_enabled(app: State<App>, profile: String, folder: String, enabled: bool) -> R<()> {
    app.refuse_if_running(&profile)?;
    mods::set_enabled(&profile_dir(&app, &profile)?, &folder, enabled)
}

#[tauri::command]
fn mods_remove(app: State<App>, profile: String, folder: String) -> R<()> {
    app.refuse_if_running(&profile)?;
    mods::remove(&profile_dir(&app, &profile)?, &folder)
}

#[tauri::command]
fn mods_inspect(path: String) -> R<mods::PackageInfo> {
    mods::inspect_package(&PathBuf::from(path))
}

#[tauri::command]
fn mods_add(app: State<App>, profile: String, path: String) -> R<String> {
    app.refuse_if_running(&profile)?;
    let dir = profile_dir(&app, &profile)?;
    let src = PathBuf::from(path);
    if src.is_dir() {
        mods::add_folder(&dir, &src)
    } else {
        mods::add_package(&dir, &src)
    }
}

/// The mods of the katalogos index (or the index the settings name).  Runs
/// off the main thread: it downloads one small file per mod.
#[tauri::command]
async fn catalog_list(app: State<'_, App>) -> R<catalog::Catalog> {
    let url = app.settings().index_url;
    let cache = app.layout.launcher().join("katalogos-cache");
    tauri::async_runtime::spawn_blocking(move || catalog::list(&url, &cache))
        .await
        .map_err(|e| e.to_string())?
}

/// Installs or updates the index's mod at `index` in a profile.
#[tauri::command]
async fn catalog_install(app: State<'_, App>, profile: String, index: usize) -> R<String> {
    app.refuse_if_running(&profile)?;
    let dir = profile_dir(&app, &profile)?;
    let url = app.settings().index_url;
    tauri::async_runtime::spawn_blocking(move || catalog::install(&dir, &url, index))
        .await
        .map_err(|e| e.to_string())?
}

/// Opens the user folder, or a profile's folder, mods folder or saves folder.
#[tauri::command]
fn open_folder(app: State<App>, what: String, profile: Option<String>) -> R<()> {
    let path = match (what.as_str(), profile) {
        ("root", _) => app.layout.root.clone(),
        ("profile", Some(p)) => profile_dir(&app, &p)?,
        ("mods", Some(p)) => profile_dir(&app, &p)?.join("mods"),
        ("saves", Some(p)) => profile_dir(&app, &p)?.join("saves"),
        _ => return Err("Unknown folder.".into()),
    };
    std::fs::create_dir_all(&path).map_err(|e| e.to_string())?;
    let opener = match std::env::consts::OS {
        "windows" => "explorer",
        "macos" => "open",
        _ => "xdg-open",
    };
    std::process::Command::new(opener)
        .arg(&path)
        .spawn()
        .map(|_| ())
        .map_err(|e| e.to_string())
}

fn view(s: &Settings) -> SettingsView {
    SettingsView {
        repo: s.repo.clone(),
        token_set: s.token().is_some(),
        index_url: s.index_url.clone(),
        local_build: s
            .local_build
            .as_ref()
            .map(|p| p.to_string_lossy().into_owned()),
    }
}

/// The game's own folder, where its `config.txt` is.
fn game_home() -> Option<PathBuf> {
    datamove::game_home(std::env::consts::OS, &|k| std::env::var(k).ok())
}

/// Saves the library and gives the game its ROM (or none).
fn save_roms(app: &App, s: &Settings) -> R<()> {
    s.save(&app.layout.settings_file())
        .map_err(|e| format!("Could not save the settings: {e}"))?;
    if let Some(home) = game_home() {
        rom::remember(&home, rom::for_game(&s.roms).map(|e| e.path.as_path()))
            .map_err(|e| format!("Could not give the ROM to the game: {e}"))?;
    }
    Ok(())
}

/// The legendary on a game's box (Reshiram on Black, Zekrom on White), its
/// front battle sprite drawn from a ROM of that game in the library, as PNG.
/// None when the library holds no ROM of that game: the page then shows the
/// eidos dragon. Nothing is stored; the picture comes from the player's ROM.
#[tauri::command]
async fn cover_art(app: State<'_, App>, game: String) -> R<Option<Vec<u8>>> {
    let (prefix, species) = match game.as_str() {
        "white" => ("IRA", nds::sprite::ZEKROM),
        _ => ("IRB", nds::sprite::RESHIRAM),
    };
    let roms = app.settings().roms;
    for e in roms.iter().filter(|e| e.code.starts_with(prefix)) {
        let Ok(rom) = nds::rom::Rom::open(&e.path) else {
            continue;
        };
        if let Ok(png) = nds::sprite::battle_front_png(&rom, species) {
            return Ok(Some(png));
        }
    }
    Ok(None)
}

/// The library's ROMs of a game ("black" or "white"), the one in the
/// wanted language first. Any file that opens will do: the pictures and
/// names drawn from it are not the game eidos plays.
fn roms_of(app: &App, game: &str, language: Option<char>) -> Vec<PathBuf> {
    let prefix = if game == "white" { "IRA" } else { "IRB" };
    let mut list: Vec<rom::Entry> = app
        .settings()
        .roms
        .into_iter()
        .filter(|e| e.code.starts_with(prefix) && e.path.is_file())
        .collect();
    list.sort_by_key(|e| Some(e.code.chars().nth(3).unwrap_or(' ')) != language);
    list.into_iter().map(|e| e.path).collect()
}

/// The game code letter of a save's language (MyStatus language).
fn language_letter(language: u8) -> Option<char> {
    Some(match language {
        1 => 'J',
        2 => 'O',
        3 => 'F',
        4 => 'I',
        5 => 'D',
        7 => 'S',
        8 => 'K',
        _ => return None,
    })
}

/// Place names per ROM file, read once.
static PLACES: Mutex<Option<HashMap<PathBuf, Arc<Vec<String>>>>> = Mutex::new(None);

fn place_names(path: &Path) -> Option<Arc<Vec<String>>> {
    if let Some(n) = PLACES
        .lock()
        .unwrap()
        .get_or_insert_with(HashMap::new)
        .get(path)
    {
        return Some(n.clone());
    }
    let rom = nds::rom::Rom::open(path).ok()?;
    let names = Arc::new(nds::place::place_names(&rom).ok()?);
    PLACES
        .lock()
        .unwrap()
        .get_or_insert_with(HashMap::new)
        .insert(path.to_path_buf(), names.clone());
    Some(names)
}

/// A profile's saves, read from its save files; each place named from a
/// ROM of the save's game in the library.
#[tauri::command]
async fn saves(app: State<'_, App>, profile: String) -> R<Vec<saves::SaveView>> {
    let dir = profile_dir(&app, &profile)?.join("saves");
    let mut list = saves::list(&dir);
    for s in &mut list {
        let place = roms_of(&app, s.game, language_letter(s.language))
            .iter()
            .find_map(|p| place_names(p))
            .and_then(|n| n.get(s.zone as usize).cloned());
        s.place = place.unwrap_or_default();
    }
    Ok(list)
}

/// Makes a save the one the game opens on (slot.txt, as eidos's title menu
/// writes it).
#[tauri::command]
fn set_current_save(app: State<App>, profile: String, slot: u8) -> R<()> {
    app.refuse_if_running(&profile)?;
    saves::set_current(&profile_dir(&app, &profile)?.join("saves"), slot)
}

/// A species' battle sprite from a ROM of the game, cut to the square
/// around the Pokemon so it fills the card's frame. None without a ROM.
#[tauri::command]
async fn species_art(app: State<'_, App>, species: u16, game: String) -> R<Option<Vec<u8>>> {
    for path in roms_of(&app, &game, None) {
        let Ok(rom) = nds::rom::Rom::open(&path) else {
            continue;
        };
        if let Ok(img) = nds::sprite::battle_front(&rom, species) {
            return Ok(Some(nds::image::encode_png(&square_crop(&img))));
        }
    }
    Ok(None)
}

/// An item's bag icon from any ROM in the library (a mod's icon). None
/// without a ROM.
#[tauri::command]
async fn item_art(app: State<'_, App>, item: u16) -> R<Option<Vec<u8>>> {
    for game in ["black", "white"] {
        for path in roms_of(&app, game, None) {
            let Ok(rom) = nds::rom::Rom::open(&path) else {
                continue;
            };
            if let Ok(img) = nds::item::item_icon(&rom, item) {
                return Ok(Some(nds::image::encode_png(&centre(&img))));
            }
        }
    }
    Ok(None)
}

#[derive(Serialize)]
struct BadgeArt {
    /// The eight badges, in award order, as PNG.
    badges: Vec<Vec<u8>>,
    /// The empty slot of each badge, as PNG.
    outlines: Vec<Vec<u8>>,
}

/// The badges and their empty slots from the badge case of a ROM of the
/// game. None without one.
#[tauri::command]
async fn badge_art(app: State<'_, App>, game: String) -> R<Option<BadgeArt>> {
    for path in roms_of(&app, &game, None) {
        let Ok(rom) = nds::rom::Rom::open(&path) else {
            continue;
        };
        if let Ok(art) = nds::badges::badge_art(&rom) {
            let png = |v: &[nds::image::Rgba]| v.iter().map(nds::image::encode_png).collect();
            return Ok(Some(BadgeArt {
                badges: png(&art.badges),
                outlines: png(&art.outlines),
            }));
        }
    }
    Ok(None)
}

/// The player's trainer picture (Hilbert or Hilda) from a ROM of the game,
/// cut to the figure. None without one.
#[tauri::command]
async fn player_art(app: State<'_, App>, game: String, female: bool) -> R<Option<Vec<u8>>> {
    let graphic = if female {
        nds::sprite::PLAYER_FEMALE
    } else {
        nds::sprite::PLAYER_MALE
    };
    for path in roms_of(&app, &game, None) {
        let Ok(rom) = nds::rom::Rom::open(&path) else {
            continue;
        };
        if let Ok(img) = nds::sprite::trainer_front(&rom, graphic) {
            return Ok(Some(nds::image::encode_png(&tight_crop(&img))));
        }
    }
    Ok(None)
}

/// The picture cut to its opaque pixels (the whole picture when empty).
fn tight_crop(img: &nds::image::Rgba) -> nds::image::Rgba {
    let (w, h) = (img.width, img.height);
    let opaque = |x: usize, y: usize| img.pixels[(y * w + x) * 4 + 3] > 0;
    let (mut x0, mut y0, mut x1, mut y1) = (w, h, 0, 0);
    for y in 0..h {
        for x in 0..w {
            if opaque(x, y) {
                x0 = x0.min(x);
                y0 = y0.min(y);
                x1 = x1.max(x + 1);
                y1 = y1.max(y + 1);
            }
        }
    }
    if x1 <= x0 {
        return img.clone();
    }
    let cw = x1 - x0;
    let mut out = nds::image::Rgba::new(cw, y1 - y0);
    for y in y0..y1 {
        let s = (y * w + x0) * 4;
        let d = (y - y0) * cw * 4;
        out.pixels[d..d + cw * 4].copy_from_slice(&img.pixels[s..s + cw * 4]);
    }
    out
}

/// The smallest square around the picture's opaque pixels, the picture
/// centred in it (at least 8x8; the whole picture when it is empty).
fn square_crop(img: &nds::image::Rgba) -> nds::image::Rgba {
    let w = img.width;
    let Some((x0, y0, x1, y1)) = opaque_box(img) else {
        return img.clone();
    };
    let side = (x1 - x0).max(y1 - y0).max(8);
    let mut out = nds::image::Rgba::new(side, side);
    let (dx, dy) = ((side - (x1 - x0)) / 2, (side - (y1 - y0)) / 2);
    for y in y0..y1 {
        for x in x0..x1 {
            let s = (y * w + x) * 4;
            let d = ((y - y0 + dy) * side + (x - x0 + dx)) * 4;
            out.pixels[d..d + 4].copy_from_slice(&img.pixels[s..s + 4]);
        }
    }
    out
}

/// The box around a picture's opaque pixels (x0, y0, x1, y1, ends
/// exclusive); None when it has none.
fn opaque_box(img: &nds::image::Rgba) -> Option<(usize, usize, usize, usize)> {
    let (w, h) = (img.width, img.height);
    let (mut x0, mut y0, mut x1, mut y1) = (w, h, 0, 0);
    for y in 0..h {
        for x in 0..w {
            if img.pixels[(y * w + x) * 4 + 3] > 0 {
                x0 = x0.min(x);
                y0 = y0.min(y);
                x1 = x1.max(x + 1);
                y1 = y1.max(y + 1);
            }
        }
    }
    (x1 > x0).then_some((x0, y0, x1, y1))
}

/// The picture moved to the middle of its own canvas, the size kept: the
/// bag icons are drawn off centre in their 32x32 cells, and cropping them
/// instead would show small items as large as big ones.
fn centre(img: &nds::image::Rgba) -> nds::image::Rgba {
    let (w, h) = (img.width, img.height);
    let Some((x0, y0, x1, y1)) = opaque_box(img) else {
        return img.clone();
    };
    let (dx, dy) = ((w - (x1 - x0)) / 2, (h - (y1 - y0)) / 2);
    let mut out = nds::image::Rgba::new(w, h);
    for y in y0..y1 {
        for x in x0..x1 {
            let s = (y * w + x) * 4;
            let d = ((y - y0 + dy) * w + (x - x0 + dx)) * 4;
            out.pixels[d..d + 4].copy_from_slice(&img.pixels[s..s + 4]);
        }
    }
    out
}

/// The library, each file read again if it changed since.
#[tauri::command]
async fn rom_library(app: State<'_, App>) -> R<Vec<rom::View>> {
    let old = app.settings().roms;
    let fresh: Vec<rom::Entry> =
        old.iter().map(|e| rom::refresh(e).unwrap_or_else(|| e.clone())).collect();
    let mut s = app.settings.lock().unwrap();
    if fresh != old && s.roms == old {
        s.roms = fresh;
        save_roms(&app, &s)?;
    }
    Ok(rom::views(&s.roms))
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct RomsAdded {
    library: Vec<rom::View>,
    added: u32,
    /// Files not added, each "name: why".
    refused: Vec<String>,
}

/// Adds files, or the `.nds` files of folders, to the library.  A file
/// already in it, or the same dump at another place, is not added twice.
#[tauri::command]
async fn add_roms(app: State<'_, App>, paths: Vec<String>) -> R<RomsAdded> {
    let have = app.settings().roms;
    let mut new: Vec<rom::Entry> = Vec::new();
    let mut refused = Vec::new();
    for p in paths.iter().flat_map(|p| rom::files(Path::new(p.trim()))) {
        let name = p.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
        if have.iter().chain(new.iter()).any(|e| e.path == p) {
            continue;
        }
        match rom::read(&p) {
            Ok(e) if have.iter().chain(new.iter()).any(|o| o.sha1 == e.sha1 && o.path.is_file()) => {
                refused.push(format!("{name}: the same dump is already in the list"))
            }
            Ok(e) => new.push(e),
            Err(why) => refused.push(format!("{name}: {why}")),
        }
    }
    let mut s = app.settings.lock().unwrap();
    let added = new.len() as u32;
    // a file found again where a missing one was takes its place
    for e in new {
        match s.roms.iter().position(|o| o.sha1 == e.sha1 && !o.path.is_file()) {
            Some(i) => s.roms[i] = e,
            None => s.roms.push(e),
        }
    }
    save_roms(&app, &s)?;
    Ok(RomsAdded { library: rom::views(&s.roms), added, refused })
}

/// Takes a file out of the library (the file itself stays).
#[tauri::command]
fn remove_rom(app: State<App>, path: String) -> R<Vec<rom::View>> {
    let mut s = app.settings.lock().unwrap();
    s.roms.retain(|e| e.path != Path::new(&path));
    save_roms(&app, &s)?;
    Ok(rom::views(&s.roms))
}

/// A missing file found at `to`: it takes the old one's place in the list.
#[tauri::command]
async fn locate_rom(app: State<'_, App>, path: String, to: String) -> R<Vec<rom::View>> {
    let e = rom::read(Path::new(to.trim()))?;
    let mut s = app.settings.lock().unwrap();
    match s.roms.iter().position(|o| o.path == Path::new(&path)) {
        Some(i) => s.roms[i] = e,
        None => s.roms.push(e),
    }
    save_roms(&app, &s)?;
    Ok(rom::views(&s.roms))
}

#[tauri::command]
fn get_settings(app: State<App>) -> SettingsView {
    view(&app.settings())
}

/// Saves the settings. `token` of None keeps the saved one; an empty string
/// clears it.
#[tauri::command]
fn save_settings(
    app: State<App>,
    repo: String,
    token: Option<String>,
    index_url: String,
    local_build: Option<String>,
) -> R<SettingsView> {
    let mut s = app.settings.lock().unwrap();
    let repo = repo.trim();
    s.repo = if repo.is_empty() {
        settings::DEFAULT_REPO.to_string()
    } else {
        repo.to_string()
    };
    if let Some(t) = token {
        s.token = t.trim().to_string();
    }
    s.index_url = index_url.trim().to_string();
    s.local_build = local_build
        .map(|p| p.trim().to_string())
        .filter(|p| !p.is_empty())
        .map(PathBuf::from);
    s.save(&app.layout.settings_file())
        .map_err(|e| format!("Could not save the settings: {e}"))?;
    Ok(view(&s))
}

/// Whether the launcher updates itself when it starts.
#[tauri::command]
fn get_auto_update(app: State<App>) -> bool {
    app.settings().auto_update
}

#[tauri::command]
fn get_pre_releases(app: State<App>) -> bool {
    app.settings().pre_releases
}

#[tauri::command]
fn set_pre_releases(app: State<App>, on: bool) -> R<()> {
    let mut s = app.settings.lock().unwrap();
    s.pre_releases = on;
    s.save(&app.layout.settings_file())
        .map_err(|e| format!("Could not save the settings: {e}"))
}

#[tauri::command]
fn set_auto_update(app: State<App>, on: bool) -> R<()> {
    let mut s = app.settings.lock().unwrap();
    s.auto_update = on;
    s.save(&app.layout.settings_file())
        .map_err(|e| format!("Could not save the settings: {e}"))
}

/// The invite code that arrived before the page was ready, once.
#[tauri::command]
fn take_pending_join(app: State<App>) -> Option<String> {
    app.pending_join.lock().unwrap().take()
}

/// Takes the invite links among some arguments or URLs: remembers the last
/// one, tells the page, and brings the window up.
fn handle_urls<'a>(handle: &tauri::AppHandle, urls: impl IntoIterator<Item = &'a str>) {
    let Some(code) = urls.into_iter().find_map(launch::parse_invite) else {
        return;
    };
    if let Some(app) = handle.try_state::<App>() {
        *app.pending_join.lock().unwrap() = Some(code.clone());
    }
    let _ = handle.emit("invite", code);
    if let Some(w) = handle.get_webview_window("main") {
        let _ = w.unminimize();
        let _ = w.show();
        let _ = w.set_focus();
    }
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        // First: a second start (an invite link click) reaches this one.
        .plugin(tauri_plugin_single_instance::init(|handle, argv, _cwd| {
            handle_urls(handle, argv.iter().map(String::as_str));
        }))
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_deep_link::init())
        // The launcher's own updates (docs/launcher.md, "Updates").
        .plugin(tauri_plugin_updater::Builder::new().build())
        .plugin(tauri_plugin_process::init())
        .setup(|app| {
            let layout = Layout::system().ok_or("The user folder cannot be found.")?;
            let mut settings = Settings::load(&layout.settings_file());
            // the Black and White ROMs of an older launcher join the library
            let old = settings.take_old_roms();
            if !old.is_empty() {
                for p in old {
                    if let Ok(e) = rom::read(&p) {
                        settings.roms.push(e);
                    }
                }
                let _ = settings.save(&layout.settings_file());
            }
            profile::ensure_default(&layout)?;
            app.manage(App {
                layout,
                settings: Mutex::new(settings),
                running: Arc::new(Mutex::new(HashMap::new())),
                pending_join: Mutex::new(None),
            });
            // Game controllers move the page's focus (#31).
            #[cfg(desktop)]
            pad::start(app.handle().clone());
            // The first start through an invite link has it in its arguments.
            let args: Vec<String> = std::env::args().collect();
            handle_urls(app.handle(), args.iter().map(String::as_str));
            let handle = app.handle().clone();
            app.deep_link().on_open_url(move |event| {
                let urls: Vec<String> = event.urls().iter().map(|u| u.to_string()).collect();
                handle_urls(&handle, urls.iter().map(String::as_str));
            });
            // The installer registers eidos:// links; a build run in place
            // registers them itself.
            #[cfg(any(windows, target_os = "linux"))]
            if cfg!(debug_assertions) {
                let _ = app.deep_link().register_all();
            }
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            overview,
            create_profile,
            rename_profile,
            duplicate_profile,
            delete_profile,
            set_profile_version,
            set_profile_data_root,
            rom_library,
            cover_art,
            saves,
            set_current_save,
            species_art,
            item_art,
            badge_art,
            player_art,
            get_auto_update,
            set_auto_update,
            get_pre_releases,
            set_pre_releases,
            set_profile_language,
            set_profile_game,
            add_roms,
            remove_rom,
            locate_rom,
            move_profile_data,
            list_remote,
            install_version,
            remove_version,
            play,
            mods_list,
            mods_set_enabled,
            mods_remove,
            mods_inspect,
            mods_add,
            catalog_list,
            catalog_install,
            open_folder,
            get_settings,
            save_settings,
            take_pending_join,
        ])
        .run(tauri::generate_context!())
        .expect("the launcher could not start");
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn square_crop_centres_the_picture() {
        let mut img = nds::image::Rgba::new(96, 96);
        // a 10x4 block at (40, 50)
        for y in 50..54 {
            for x in 40..50 {
                img.pixels[(y * 96 + x) * 4 + 3] = 255;
            }
        }
        let out = square_crop(&img);
        assert_eq!((out.width, out.height), (10, 10));
        assert_eq!(out.pixels[(3 * 10) * 4 + 3], 255);
        assert_eq!(out.pixels[(2 * 10) * 4 + 3], 0);
        let empty = nds::image::Rgba::new(96, 96);
        assert_eq!(square_crop(&empty).width, 96);
    }

    #[test]
    fn centre_keeps_the_size_and_centres() {
        let mut img = nds::image::Rgba::new(32, 32);
        // a 4x2 block in the top left corner
        for y in 0..2 {
            for x in 0..4 {
                img.pixels[(y * 32 + x) * 4 + 3] = 255;
            }
        }
        let out = centre(&img);
        assert_eq!((out.width, out.height), (32, 32));
        assert_eq!(out.pixels[(15 * 32 + 14) * 4 + 3], 255);
        assert_eq!(out.pixels[(16 * 32 + 17) * 4 + 3], 255);
        assert_eq!(out.pixels[(14 * 32 + 14) * 4 + 3], 0);
        assert_eq!(out.pixels[3], 0);
    }

    /// Writes the official mods' item icons as item_art draws them, for the
    /// browser preview: NDS_ROM a Black or White ROM, EIDOS_MOCK_OUT the
    /// output folder (public/mock).
    #[test]
    fn preview_items() {
        let (Some(rom), Some(out)) = (std::env::var_os("NDS_ROM"), std::env::var_os("EIDOS_MOCK_OUT"))
        else {
            return;
        };
        let rom = nds::rom::Rom::open(Path::new(&rom)).unwrap();
        for item in [4u16, 216, 267, 280] {
            let img = nds::item::item_icon(&rom, item).unwrap();
            std::fs::write(
                PathBuf::from(&out).join(format!("item_{item}.png")),
                nds::image::encode_png(&centre(&img)),
            )
            .unwrap();
        }
    }

    /// Writes what the Home page shows for a real saves folder, for the
    /// browser preview (npm run dev): EIDOS_SAVES the folder, NDS_ROM a
    /// Black or White ROM, EIDOS_MOCK_OUT the output folder (public/mock).
    #[test]
    fn preview_data() {
        let (Some(saves_dir), Some(rom), Some(out)) = (
            std::env::var_os("EIDOS_SAVES"),
            std::env::var_os("NDS_ROM"),
            std::env::var_os("EIDOS_MOCK_OUT"),
        ) else {
            return;
        };
        let out = PathBuf::from(out);
        let rom = nds::rom::Rom::open(Path::new(&rom)).unwrap();
        let names = nds::place::place_names(&rom).unwrap();
        let mut list = saves::list(Path::new(&saves_dir));
        for s in &mut list {
            s.place = names.get(s.zone as usize).cloned().unwrap_or_default();
            for m in &s.party {
                let img = nds::sprite::battle_front(&rom, m.species).unwrap();
                std::fs::write(
                    out.join(format!("{}.png", m.species)),
                    nds::image::encode_png(&square_crop(&img)),
                )
                .unwrap();
            }
        }
        for (f, g) in [(0, nds::sprite::PLAYER_MALE), (1, nds::sprite::PLAYER_FEMALE)] {
            let img = nds::sprite::trainer_front(&rom, g).unwrap();
            std::fs::write(out.join(format!("player_{f}.png")), nds::image::encode_png(&tight_crop(&img))).unwrap();
        }
        std::fs::write(
            out.join("saves.json"),
            serde_json::to_string_pretty(&list).unwrap(),
        )
        .unwrap();
    }
}
