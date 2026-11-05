import { invoke } from "@tauri-apps/api/core";

export type Profile = { name: string; version: string; dataRoot: string; language: string; game: string };

export type Overview = {
  profiles: Profile[];
  installed: string[];
  localBuild: string | null;
  localOk: boolean;
  running: string[];
  launcherVersion: string;
  root: string;
};

export type Remote = {
  tag: string;
  id: string | null;
  name: string;
  prerelease: boolean;
  published: string | null;
  size: number | null;
  /// The release notes (Markdown), when the release list gave them.
  notes: string | null;
  available: boolean;
  installed: boolean;
};

export type ModInfo = {
  folder: string;
  name: string;
  order: number;
  enabled: boolean;
  hasManifest: boolean;
  catalogId: string | null;
  version: string | null;
  icon: ModIcon | null;
};

/// A mod's icon: a game item's or a species' picture from the player's ROM,
/// or the mod's own PNG.
export type ModIcon = { item?: number; species?: number; png?: number[] };

export type CatalogMod = {
  id: string;
  name: string;
  version: string;
  author: string;
  description: string;
  official: boolean;
  multiplayer: boolean;
  hasCode: boolean;
  games: string[];
  tags: string[];
  size: number;
  icon: ModIcon | null;
  index: number;
};

export type Catalog = { mods: CatalogMod[]; errors: { entry: string; error: string }[] };

export type PackageInfo = { name: string; files: number; bytes: number };

export type SettingsView = {
  repo: string;
  tokenSet: boolean;
  indexUrl: string;
  localBuild: string | null;
};

/** A file of the ROM library (src-tauri/src/rom.rs, View). */
export type RomView = {
  path: string;
  game: "black" | "white" | null;
  language: string;
  status: "verified" | "modified" | "missing";
  name: string | null;
  inUse: boolean;
};

export const LATEST = "latest";
export const LOCAL = "local";

export const overview = () => invoke<Overview>("overview");

/// One save of a profile, read from its save file (src-tauri/src/saves.rs).
export type Save = {
  /// 1 to 8: the game's save slot.
  slot: number;
  /// The slot the game opens on (slot.txt).
  current: boolean;
  trainer: string;
  female: boolean;
  /// Play time in seconds.
  seconds: number;
  /// How many badges.
  badges: number;
  /// One bit per badge, in the order the game awards them.
  badgeBits: number;
  /// The party, in order.
  party: { species: number; egg: boolean; name: string; level: number; hp: number; maxHp: number }[];
  /// The zone the player saved in, and its name from a ROM of the game
  /// ("" without one).
  zone: number;
  place: string;
  /// When the file was last written, seconds since 1970.
  saved: number;
  game: "black" | "white";
  language: number;
};
export const saves = (profile: string) => invoke<Save[]>("saves", { profile });
export const setCurrentSave = (profile: string, slot: number) => invoke<void>("set_current_save", { profile, slot });

/// A species' battle sprite, drawn from a ROM of the game in the library.
const speciesCache = new Map<string, Promise<string | null>>();
export function speciesArt(species: number, game: string): Promise<string | null> {
  const key = `${game}:${species}`;
  let p = speciesCache.get(key);
  if (!p) {
    p = invoke<number[] | null>("species_art", { species, game })
      .then((b) => (b ? URL.createObjectURL(new Blob([new Uint8Array(b)], { type: "image/png" })) : null))
      .catch(() => null);
    speciesCache.set(key, p);
  }
  return p;
}
/// An item's bag icon, drawn from a ROM in the library.
const itemCache = new Map<number, Promise<string | null>>();
export function itemArt(item: number): Promise<string | null> {
  let p = itemCache.get(item);
  if (!p) {
    p = invoke<number[] | null>("item_art", { item })
      .then((b) => (b ? URL.createObjectURL(new Blob([new Uint8Array(b)], { type: "image/png" })) : null))
      .catch(() => null);
    itemCache.set(item, p);
  }
  return p;
}

/// The picture URL of a mod's icon; null when there is none to draw.
export function modIconArt(icon: ModIcon | null, game: string): Promise<string | null> {
  if (!icon) return Promise.resolve(null);
  if (icon.png) return Promise.resolve(URL.createObjectURL(new Blob([new Uint8Array(icon.png)], { type: "image/png" })));
  if (icon.item != null) return itemArt(icon.item);
  if (icon.species != null) return speciesArt(icon.species, game);
  return Promise.resolve(null);
}

/// The player's trainer picture (Hilbert or Hilda), drawn from a ROM of the
/// game, as a picture URL; null without such a ROM.
const playerCache = new Map<string, Promise<string | null>>();
export function playerArt(game: string, female: boolean): Promise<string | null> {
  const key = `${game}:${female}`;
  let p = playerCache.get(key);
  if (!p) {
    p = invoke<number[] | null>("player_art", { game, female })
      .then((b) => (b ? URL.createObjectURL(new Blob([new Uint8Array(b)], { type: "image/png" })) : null))
      .catch(() => null);
    playerCache.set(key, p);
  }
  return p;
}

/// The badges and their empty slots, drawn from the badge case of a ROM of
/// the game, as picture URLs; null without such a ROM.
export type BadgeArt = { badges: string[]; outlines: string[] };
const badgeCache = new Map<string, Promise<BadgeArt | null>>();
export function badgeArt(game: string): Promise<BadgeArt | null> {
  let p = badgeCache.get(game);
  if (!p) {
    const url = (b: number[]) => URL.createObjectURL(new Blob([new Uint8Array(b)], { type: "image/png" }));
    p = invoke<{ badges: number[][]; outlines: number[][] } | null>("badge_art", { game })
      .then((a) => (a ? { badges: a.badges.map(url), outlines: a.outlines.map(url) } : null))
      .catch(() => null);
    badgeCache.set(game, p);
  }
  return p;
}

/// The box legendary of a game ("black" or "white"), drawn from a ROM of
/// that game in the library, as a picture URL; null without such a ROM.
export async function coverArt(game: string): Promise<string | null> {
  const bytes = await invoke<number[] | null>("cover_art", { game });
  return bytes ? URL.createObjectURL(new Blob([new Uint8Array(bytes)], { type: "image/png" })) : null;
}
export const createProfile = (name: string, version: string) => invoke<Profile>("create_profile", { name, version });
export const renameProfile = (from: string, to: string) => invoke<void>("rename_profile", { from, to });
export const duplicateProfile = (name: string) => invoke<Profile>("duplicate_profile", { name });
export const deleteProfile = (name: string) => invoke<void>("delete_profile", { name });
export const setProfileVersion = (name: string, version: string) =>
  invoke<void>("set_profile_version", { name, version });
export const moveProfileData = (name: string, to: string) => invoke<string>("move_profile_data", { name, to });
export const setProfileDataRoot = (name: string, path: string) =>
  invoke<void>("set_profile_data_root", { name, path });
/// The release list, kept for the session: pages show it at once when they
/// open again, and it is fetched again when asked (`fresh`, after an
/// install or a removal) or when older than ten minutes.
let remoteKept: Remote[] | null = null;
let remoteAt = 0;
const REMOTE_AGE = 10 * 60 * 1000;
export const knownRemote = () => remoteKept;
export async function listRemote(fresh = false): Promise<Remote[]> {
  if (!fresh && remoteKept && Date.now() - remoteAt < REMOTE_AGE) return remoteKept;
  const list = await invoke<Remote[]>("list_remote");
  remoteKept = list;
  remoteAt = Date.now();
  return list;
}
export const installVersion = (tag: string) => invoke<void>("install_version", { tag });
export const removeVersion = (id: string) => invoke<void>("remove_version", { id });
export type Layout = "classic" | "one";

// The first released version that has the one-screen layout; null while no
// version does. Versions are numbers like 0.1.4 (or a tag with a leading v).
export const ONE_SCREEN_MIN_VERSION: string | null = "0.1.6";

const parts = (v: string) => v.replace(/^v/i, "").split(/[.-]/).map((n) => parseInt(n, 10) || 0);

/** Whether version a (0.1.7, or a tag with a leading v) comes after b. */
export function newerThan(a: string, b: string): boolean {
  const x = parts(a);
  const y = parts(b);
  for (let i = 0; i < Math.max(x.length, y.length); i++) {
    const d = (x[i] ?? 0) - (y[i] ?? 0);
    if (d !== 0) return d > 0;
  }
  return false;
}

// Whether a resolved version (an id, or LOCAL for the local build) can start
// in the one-screen layout. A local build always can, so it can be tried.
export function supportsOneScreen(version: string | null): boolean {
  if (!version) return false;
  if (version === LOCAL) return true;
  // the browser preview (lib/mock.ts) has no real versions
  if (import.meta.env.DEV && "__EIDOS_MOCK__" in window) return true;
  if (ONE_SCREEN_MIN_VERSION === null) return false;
  const a = parts(version);
  const b = parts(ONE_SCREEN_MIN_VERSION);
  for (let i = 0; i < Math.max(a.length, b.length); i++) {
    const d = (a[i] ?? 0) - (b[i] ?? 0);
    if (d !== 0) return d > 0;
  }
  return true;
}

export const play = (name: string, join?: string, layout: Layout = "classic") =>
  invoke<{ version: string }>("play", { name, join: join ?? null, layout });
export const modsList = (profile: string) => invoke<ModInfo[]>("mods_list", { profile });
export const modsSetEnabled = (profile: string, folder: string, enabled: boolean) =>
  invoke<void>("mods_set_enabled", { profile, folder, enabled });
export const modsRemove = (profile: string, folder: string) => invoke<void>("mods_remove", { profile, folder });
export const modsInspect = (path: string) => invoke<PackageInfo>("mods_inspect", { path });
export const modsAdd = (profile: string, path: string) => invoke<string>("mods_add", { profile, path });
export const catalogList = () => invoke<Catalog>("catalog_list");
export const catalogInstall = (profile: string, index: number) => invoke<string>("catalog_install", { profile, index });
export const openFolder = (what: "root" | "profile" | "mods" | "saves", profile?: string) =>
  invoke<void>("open_folder", { what, profile: profile ?? null });
export const getSettings = () => invoke<SettingsView>("get_settings");
export const saveSettings = (s: { repo: string; token: string | null; indexUrl: string; localBuild: string | null }) =>
  invoke<SettingsView>("save_settings", s);
export const romLibrary = () => invoke<RomView[]>("rom_library");
export const setProfileLanguage = (name: string, language: string) =>
  invoke<void>("set_profile_language", { name, language });
export const setProfileGame = (name: string, game: string) => invoke<void>("set_profile_game", { name, game });

/** The games the player's verified ROMs cover, Black first. */
export function gamesOf(roms: RomView[]): ("black" | "white")[] {
  const have = new Set(roms.filter((r) => r.status === "verified").map((r) => r.game));
  return (["black", "white"] as const).filter((g) => have.has(g));
}

/** A language to play in, as the picker shows it. */
export type LanguageChoice = { code: string; name: string; note?: string };

const LANGUAGE_OF: Record<string, LanguageChoice[]> = {
  English: [{ code: "en", name: "English" }],
  French: [{ code: "fr", name: "Français" }],
  German: [{ code: "de", name: "Deutsch" }],
  Italian: [{ code: "it", name: "Italiano" }],
  Spanish: [{ code: "es", name: "Español" }],
  Japanese: [
    { code: "ja-kana", name: "日本語", note: "ひらがな" },
    { code: "ja-kanji", name: "日本語", note: "漢字" },
  ],
};

/** A language code as the picker names it (for a profile set to a
 * language no ROM covers any more). */
export function languageName(code: string): string {
  for (const l of Object.values(LANGUAGE_OF).flat()) {
    if (l.code === code) return l.note ? `${l.name} (${l.note})` : l.name;
  }
  return code;
}

/** The languages the player's verified ROMs cover, English first. */
export function languagesOf(roms: RomView[]): LanguageChoice[] {
  const have = new Set(roms.filter((r) => r.status === "verified").map((r) => r.language));
  return Object.keys(LANGUAGE_OF)
    .filter((l) => have.has(l))
    .flatMap((l) => LANGUAGE_OF[l]);
}
export const addRoms = (paths: string[]) =>
  invoke<{ library: RomView[]; added: number; refused: string[] }>("add_roms", { paths });
export const removeRom = (path: string) => invoke<RomView[]>("remove_rom", { path });
export const locateRom = (path: string, to: string) => invoke<RomView[]>("locate_rom", { path, to });
export const takePendingJoin = () => invoke<string | null>("take_pending_join");

/** The text of a failed command. */
export const message = (e: unknown) => (typeof e === "string" ? e : e instanceof Error ? e.message : String(e));

/** "Latest" and "Local build" for the version pickers. */
export function versionLabel(v: string): string {
  return v === LATEST ? "Latest" : v === LOCAL ? "Local build" : v;
}
