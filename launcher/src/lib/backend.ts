import { invoke } from "@tauri-apps/api/core";

export type Profile = { name: string; version: string; dataRoot: string; language: string };

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
  available: boolean;
  installed: boolean;
};

export type ModInfo = {
  folder: string;
  name: string;
  order: number;
  enabled: boolean;
  hasManifest: boolean;
};

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
export const createProfile = (name: string, version: string) => invoke<Profile>("create_profile", { name, version });
export const renameProfile = (from: string, to: string) => invoke<void>("rename_profile", { from, to });
export const duplicateProfile = (name: string) => invoke<Profile>("duplicate_profile", { name });
export const deleteProfile = (name: string) => invoke<void>("delete_profile", { name });
export const setProfileVersion = (name: string, version: string) =>
  invoke<void>("set_profile_version", { name, version });
export const moveProfileData = (name: string, to: string) => invoke<string>("move_profile_data", { name, to });
export const setProfileDataRoot = (name: string, path: string) =>
  invoke<void>("set_profile_data_root", { name, path });
export const listRemote = () => invoke<Remote[]>("list_remote");
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
export const openFolder = (what: "root" | "profile" | "mods", profile?: string) =>
  invoke<void>("open_folder", { what, profile: profile ?? null });
export const getSettings = () => invoke<SettingsView>("get_settings");
export const saveSettings = (s: { repo: string; token: string | null; indexUrl: string; localBuild: string | null }) =>
  invoke<SettingsView>("save_settings", s);
export const romLibrary = () => invoke<RomView[]>("rom_library");
export const setProfileLanguage = (name: string, language: string) =>
  invoke<void>("set_profile_language", { name, language });

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
