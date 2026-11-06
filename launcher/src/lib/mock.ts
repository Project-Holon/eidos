// A stand-in for the Tauri backend, for designing the pages in a plain
// browser (`npm run dev`, then open http://localhost:1430/?page=versions).
// Loaded only by the development server outside Tauri (main.ts); the built
// app never contains it.

const overview = {
  profiles: [
    { name: "Black", version: "latest", dataRoot: "", language: "", game: "" },
    { name: "White", version: "latest", dataRoot: "", language: "", game: "white" },
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
  { folder: "Nuzlocke", name: "Nuzlocke", order: 0, enabled: true, hasManifest: true, catalogId: "katalogos/nuzlocke", version: "0.9.0", icon: { item: 4 } },
  { folder: "fast-bike", name: "Fast Bike", order: 1, enabled: false, hasManifest: true, catalogId: null, version: null, icon: { species: 506 } },
  { folder: "my-test", name: "my-test", order: 2, enabled: true, hasManifest: false, catalogId: null, version: null, icon: null },
];

const catalog = {
  mods: [
    { id: "katalogos/nuzlocke", name: "Nuzlocke", version: "1.0.0", author: "Project Holon", description: "The Nuzlocke challenge: catch only the first wild Pokemon of each area, and a Pokemon that faints is gone for good.", official: true, multiplayer: false, hasCode: false, games: ["black", "white"], tags: ["challenge", "nuzlocke"], size: 2400, icon: { item: 4 }, index: 0 },
    { id: "katalogos/soul-link", name: "Soul Link", version: "1.0.0", author: "Project Holon", description: "A Nuzlocke for two or more players in one multiplayer game. The Pokemon you catch in the same area are linked.", official: true, multiplayer: true, hasCode: true, games: ["black", "white"], tags: ["challenge", "multiplayer"], size: 9100, icon: { item: 280 }, index: 1 },
    { id: "katalogos/exp-share", name: "Exp. Share", version: "1.0.0", author: "Project Holon", description: "The whole party gains experience in every battle. Can be switched off in the settings.", official: true, multiplayer: false, hasCode: false, games: ["black", "white"], tags: ["quality of life"], size: 700, icon: { item: 216 }, index: 2 },
    { id: "katalogos/smart-trainers", name: "Smart Trainers", version: "0.2.0", author: "Project Holon", description: "Trainers battle like a thinking player.", official: true, multiplayer: false, hasCode: true, games: ["black", "white"], tags: ["difficulty"], size: 72000, icon: { item: 267 }, index: 3 },
    { id: "someone/fairy-type", name: "Fairy type", version: "0.3.0", author: "someone", description: "Adds the Fairy type.", official: false, multiplayer: false, hasCode: false, games: ["black"], tags: ["types"], size: 512000, icon: null, index: 4 },
  ],
  errors: [],
};

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
  catalog_list: catalog,
  catalog_install: "Nuzlocke",
  get_settings: settings,
  save_settings: settings,
  // ?onelang: a library in one language (the picker greyed out)
  cover_art: null,
  saves: [],
  set_current_save: null,
  get_auto_update: true,
  get_pre_releases: false,
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
    invoke: async (cmd: string, args?: Record<string, unknown>) => {
      if (cmd.startsWith("plugin:event|")) return next++;
      // The sample sprites in public/mock (not in the repository): the
      // battle sprites the real launcher draws from the player's ROM.
      // Saves decoded by the real reader from real files, dropped in
      // public/mock/saves.json (not in the repository); none without it.
      // The real latest eidos release, as `gh release view --json
      // tagName,name,body,publishedAt` writes it, in public/mock/release.json
      // (not in the repository): first in the list, with its notes.
      if (cmd === "list_remote") {
        const r = await fetch("/mock/release.json").catch(() => null);
        if (!r || !r.ok) return remote;
        const g = await r.json();
        const id = String(g.tagName).replace(/^v/, "");
        return [
          { tag: g.tagName, id, name: g.name, prerelease: false, published: g.publishedAt, size: 7500000, notes: g.body, available: true, installed: false },
          ...remote.filter((x) => x.id !== id),
        ];
      }
      // The badge case drawn by the real reader from a real ROM, in
      // public/mock/badges/b<i>.png and o<i>.png (not in the repository).
      if (cmd === "badge_art") {
        const get = async (f: string) => {
          const r = await fetch(`/mock/badges/${f}.png`).catch(() => null);
          return r && r.ok ? Array.from(new Uint8Array(await r.arrayBuffer())) : null;
        };
        const badges = await Promise.all([0, 1, 2, 3, 4, 5, 6, 7].map((i) => get(`b${i}`)));
        const outlines = await Promise.all([0, 1, 2, 3, 4, 5, 6, 7].map((i) => get(`o${i}`)));
        return badges.every(Boolean) && outlines.every(Boolean) ? { badges, outlines } : null;
      }
      if (cmd === "saves") {
        const r = await fetch("/mock/saves.json").catch(() => null);
        return r && r.ok ? r.json() : [];
      }
      if (cmd === "player_art") {
        const r = await fetch(`/mock/player_${args?.female ? 1 : 0}.png`).catch(() => null);
        return r && r.ok ? Array.from(new Uint8Array(await r.arrayBuffer())) : null;
      }
      // Item bag icons drawn by the real reader from a real ROM, in
      // public/mock/item_<n>.png (not in the repository).
      if (cmd === "item_art") {
        const r = await fetch(`/mock/item_${args?.item}.png`).catch(() => null);
        return r && r.ok ? Array.from(new Uint8Array(await r.arrayBuffer())) : null;
      }
      if (cmd === "species_art" || cmd === "cover_art") {
        const n = cmd === "cover_art" ? (args?.game === "white" ? 644 : 643) : args?.species;
        const r = await fetch(`/mock/${n}.png`).catch(() => null);
        if (!r || !r.ok) return null;
        return Array.from(new Uint8Array(await r.arrayBuffer()));
      }
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
