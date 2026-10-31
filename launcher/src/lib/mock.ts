// A stand-in for the Tauri backend, for designing the pages in a plain
// browser (`npm run dev`, then open http://localhost:1430/?page=versions).
// Loaded only by the development server outside Tauri (main.ts); the built
// app never contains it.

const overview = {
  profiles: [
    { name: "Default", version: "latest", dataRoot: "", language: "", game: "" },
    { name: "Nuzlocke", version: "0.1.4", dataRoot: "E:\\Games\\eidos", language: "fr", game: "white" },
    { name: "Randomizer", version: "local", dataRoot: "", language: "ja-kana", game: "" },
  ],
  installed: ["0.1.5", "0.1.4"],
  localBuild: "C:\\eidos-game",
  localOk: true,
  running: [] as string[],
  launcherVersion: "0.1.6",
  root: "C:\\Users\\player\\AppData\\Roaming\\eidos",
};

const remote = [
  { tag: "v0.1.6", id: "0.1.6", name: "eidos 0.1.6", prerelease: false, published: "2026-10-27T18:40:02Z", size: 7450000, available: true, installed: false },
  { tag: "v0.1.5", id: "0.1.5", name: "eidos 0.1.5", prerelease: false, published: "2026-10-23T13:07:48Z", size: 7400000, available: true, installed: true },
  { tag: "v0.1.4", id: "0.1.4", name: "eidos 0.1.4", prerelease: false, published: "2026-10-23T12:28:22Z", size: 7390000, available: true, installed: true },
  { tag: "v0.1.3", id: "0.1.3", name: "eidos 0.1.3", prerelease: false, published: "2026-10-23T11:50:44Z", size: 7380000, available: true, installed: false },
  { tag: "v0.1.2", id: "0.1.2", name: "eidos 0.1.2", prerelease: false, published: "2026-10-23T10:25:16Z", size: 7350000, available: true, installed: false },
  { tag: "v0.1.1", id: "0.1.1", name: "eidos 0.1.1", prerelease: false, published: "2026-10-23T02:05:31Z", size: 7330000, available: true, installed: false },
];

const mods = [
  { folder: "hd-sprites", name: "HD Sprites", order: 0, enabled: true, hasManifest: true },
  { folder: "fast-bike", name: "Fast Bike", order: 1, enabled: false, hasManifest: true },
  { folder: "my-test", name: "my-test", order: 2, enabled: true, hasManifest: false },
];

const settings = {
  repo: "Project-Holon/eidos",
  tokenSet: false,
  indexUrl: "",
  localBuild: "C:\\eidos-game",
};

const roms = [
  { path: "D:\\ROMs\\Pokemon - Black Version (USA, Europe) (NDSi Enhanced).nds", game: "black", language: "English", status: "verified", name: "Pokemon - Black Version (USA, Europe)", inUse: true },
  { path: "D:\\ROMs\\Pokemon - White Version (USA, Europe) (NDSi Enhanced).nds", game: "white", language: "English", status: "verified", name: "Pokemon - White Version (USA, Europe)", inUse: false },
  { path: "D:\\ROMs\\fr\\Pokemon - Version Noire (France).nds", game: "black", language: "French", status: "verified", name: "Pokemon - Version Noire (France)", inUse: false },
  { path: "D:\\ROMs\\jp\\pokemon_white_jp.nds", game: "white", language: "Japanese", status: "verified", name: "Pocket Monsters - White (Japan)", inUse: false },
  { path: "E:\\old\\black_hack.nds", game: "black", language: "English", status: "modified", name: null, inUse: false },
  { path: "F:\\usb\\Pokemon - Weisse Edition (Germany).nds", game: "white", language: "German", status: "missing", name: null, inUse: false },
];

const answers: Record<string, unknown> = {
  overview,
  list_remote: remote,
  mods_list: mods,
  get_settings: settings,
  save_settings: settings,
  // ?onelang: a library in one language (the picker greyed out)
  rom_library: new URLSearchParams(location.search).has("onelang") ? roms.slice(0, 1) : roms,

  add_roms: { library: roms, added: 0, refused: [] },
  remove_rom: roms,
  locate_rom: roms,
  move_profile_data: "The data is now at E:\\Games\\eidos.",
  take_pending_join: null,
};

let next = 1;

export function installMock() {
  const w = window as unknown as Record<string, unknown>;
  w.__EIDOS_MOCK__ = true;
  w.__TAURI_INTERNALS__ = {
    invoke: async (cmd: string) => {
      if (cmd.startsWith("plugin:event|")) return next++;
      if (cmd.startsWith("plugin:updater|")) return null;
      if (cmd in answers) return structuredClone(answers[cmd]);
      return null;
    },
    transformCallback: () => next++,
    unregisterCallback: () => {},
    metadata: { currentWindow: { label: "main" }, currentWebview: { label: "main" } },
  };
  w.__TAURI_EVENT_PLUGIN_INTERNALS__ = { unregisterListener: () => {} };
}
