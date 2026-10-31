//! The eidos launcher's shell: profiles, versions, mods, Play and invite
//! links. The pages are in the front end; this file holds the state and the
//! commands the pages call.
//!
//! TODO (later, docs/distribution.md "Joining a game with other versions or
//! mods"): "Get and join" for an invite link. It needs the host's version
//! and mod list in the invite, and the mods index. Today an invite link only
//! launches the chosen profile with the join argument.

mod datamove;
mod launch;
mod layout;
mod mods;
mod profile;
mod remote;
mod rom;
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
    /// The release has a file for this platform and a SHA256SUMS file.
    available: bool,
    installed: bool,
}

#[tauri::command]
fn overview(app: State<App>) -> R<Overview> {
    let s = app.settings();
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
    profile::create(&app.layout, &name, &version)
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
        releases::download(sums_asset, s.token(), &sums_path, None).map_err(|e| e.to_string())?;
        let sums = std::fs::read_to_string(&sums_path).map_err(|e| e.to_string())?;
        let want = releases::checksum_for(&sums, &asset.name)
            .ok_or_else(|| format!("SHA256SUMS has no entry for {}.", asset.name))?;
        stage("Downloading eidos");
        let file = tmp.join(&asset.name);
        releases::download(asset, s.token(), &file, Some(&want)).map_err(|e| e.to_string())?;
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
    let p = profile::get(&app.layout, &name)?;
    app.refuse_if_running(&name)?;
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
        format!("There is no ROM for this profile's game and language ({code}). Add it in Settings, ROMs.")
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

/// Opens the user folder, a profile's folder or its mods folder.
#[tauri::command]
fn open_folder(app: State<App>, what: String, profile: Option<String>) -> R<()> {
    let path = match (what.as_str(), profile) {
        ("root", _) => app.layout.root.clone(),
        ("profile", Some(p)) => profile_dir(&app, &p)?,
        ("mods", Some(p)) => profile_dir(&app, &p)?.join("mods"),
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
            open_folder,
            get_settings,
            save_settings,
            take_pending_join,
        ])
        .run(tauri::generate_context!())
        .expect("the launcher could not start");
}
