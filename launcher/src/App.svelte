<script lang="ts">
  import { onMount } from "svelte";
  import { listen } from "@tauri-apps/api/event";
  import { toast } from "morphe/menu.svelte";
  import {
    gamesOf,
    message,
    overview,
    play,
    romLibrary,
    setProfileGame,
    takePendingJoin,
    versionLabel,
    type Overview,
  } from "./lib/backend";
  import { setGameTheme } from "./lib/theme";
  import Icon from "./lib/Icon.svelte";
  import { MOBILE } from "./lib/platform";
  import { startPadNav } from "./lib/padnav";
  import { checkForUpdate, updates } from "./lib/updater.svelte";
  import Home from "./pages/Home.svelte";
  import Profiles from "./pages/Profiles.svelte";
  import Versions from "./pages/Versions.svelte";
  import Mods from "./pages/Mods.svelte";
  import Settings from "./pages/Settings.svelte";

  type Page = "home" | "profiles" | "versions" | "mods" | "settings";
  const PAGES: [Page, string, string][] = [
    ["home", "Play", "play"],
    ["profiles", "Profiles", "profiles"],
    ["versions", "Versions", "versions"],
    ["mods", "Mods", "mods"],
    ["settings", "Settings", "settings"],
  ];
  const shownPages = PAGES.filter(([id]) => !(MOBILE && id === "versions"));
  const KEY = "eidos-launcher.profile";

  let ov = $state<Overview | null>(null);
  let failure = $state<string | null>(null);
  let page = $state<Page>(firstPage());
  let selected = $state(read());
  let inviteCode = $state<string | null>(null);
  let inviteProfile = $state("");
  let inviteDialog = $state<HTMLDialogElement>();

  // The development preview opens a page from the address (?page=mods).
  function firstPage(): Page {
    const p = new URLSearchParams(location.search).get("page");
    return PAGES.some(([id]) => id === p) ? (p as Page) : "home";
  }

  function read(): string {
    try {
      return localStorage.getItem(KEY) ?? "Black";
    } catch {
      return "Black";
    }
  }

  function select(name: string) {
    selected = name;
    try {
      localStorage.setItem(KEY, name);
    } catch {
      /* the choice is only remembered when storage works */
    }
  }

  async function refresh() {
    try {
      ov = await overview();
      if (!ov.profiles.some((p) => p.name === selected)) select("Black");
    } catch (e) {
      failure = message(e);
    }
  }

  function showInvite(code: string) {
    inviteCode = code;
    inviteProfile = selected;
    inviteDialog?.showModal();
  }

  async function join() {
    if (!inviteCode) return;
    try {
      await play(inviteProfile, inviteCode);
      inviteDialog?.close();
      await refresh();
    } catch (e) {
      toast(message(e), true);
    }
  }

  onMount(() => {
    refresh();
    loadGames();
    // The launcher's own update, once per launch; a failure only reports.
    checkForUpdate().then(() => {
      if (updates.state.kind === "failed") toast(updates.state.message, true);
    });
    const stops: Promise<() => void>[] = [
      listen("running-changed", refresh),
      listen<string>("invite", async (e) => {
        await takePendingJoin();
        await refresh();
        showInvite(e.payload);
      }),
    ];
    takePendingJoin().then((code) => code && showInvite(code));
    // a controller or the keyboard moves through the page (#31)
    const stopNav = startPadNav({
      page: (step) => {
        const ids = shownPages.map(([id]) => id);
        page = ids[(ids.indexOf(page) + step + ids.length) % ids.length];
      },
      back: () => (page = "home"),
    });
    return () => {
      stops.forEach((s) => s.then((stop) => stop()));
      stopNav();
    };
  });

  const current = $derived(ov?.profiles.find((p) => p.name === selected));

  // Black or White, in the sidebar: it selects the Black or the White
  // profile, and the launcher's look follows the selected profile's game
  // (any profile's) unless a fixed theme is chosen in Settings.  Black is Reshiram's game, by day; White is Zekrom's,
  // by night (the box legendaries, as morphe's themes name them).
  type Game = "black" | "white";
  const PROFILE_OF: Record<Game, string> = { black: "Black", white: "White" };
  const THEME_OF: Record<Game, "light" | "dark"> = { black: "light", white: "dark" };
  let haveGames = $state<Game[]>([]);
  let askRom = $state<Game | null>(null);
  let askDialog = $state<HTMLDialogElement>();
  const game = $derived<Game>(current?.game === "white" ? "white" : "black");

  $effect(() => {
    if (current) setGameTheme(THEME_OF[game]);
  });

  async function loadGames() {
    try {
      haveGames = gamesOf(await romLibrary());
    } catch {
      haveGames = [];
    }
  }

  // The switch always selects the profile; Play asks for the ROM when that
  // game has none yet (needRom).
  function pickGame(g: Game) {
    select(PROFILE_OF[g]);
  }

  function needRom(g: Game) {
    askRom = g;
    askDialog?.showModal();
  }

  function addRom() {
    askDialog?.close();
    page = "settings";
  }
  const percent = $derived.by(() => {
    const s = updates.state;
    return s.kind === "downloading" && s.total ? Math.min(100, Math.round((s.done / s.total) * 100)) : null;
  });
</script>

{#if failure}
  <div class="state"><p class="bad" role="alert">{failure}</p></div>
{:else if !ov}
  <div class="state muted">Loading</div>
{:else}
  <div class="shell">
    <nav aria-label="Pages">
      <h1 class="brand">eidos</h1>
      <div class="links">
        {#each shownPages as [id, label, icon]}
          <button class="row-item" aria-current={page === id} onclick={() => (page = id)}>
            <Icon name={icon} />
            <span>{label}</span>
          </button>
        {/each}
      </div>
      <div class="game-toggle" class:white={game === "white"} role="radiogroup" aria-label="Game">
        <span class="knob" aria-hidden="true"></span>
        {#each [["black", "Black"], ["white", "White"]] as [g, label] (g)}
          <button
            role="radio"
            class={g}
            aria-checked={game === g}
            class:on={game === g}
            title={`Pokémon ${label}`}
            onclick={() => pickGame(g as Game)}>{label}</button>
        {/each}
      </div>
      <div class="foot">
        <span class="faint small">Launcher {ov.launcherVersion}</span>
        {#if updates.state.kind === "checking"}
          <span class="faint small">Checking for updates</span>
        {:else if updates.state.kind === "current"}
          <span class="faint small">Up to date</span>
        {/if}
      </div>
    </nav>
    <main>
      {#if page === "home"}
        <Home {ov} {selected} {current} onSelect={select} onRefresh={refresh} onGoto={(p: Page) => (page = p)} onNeedRom={needRom} />
      {:else if page === "profiles"}
        <Profiles {ov} {selected} onSelect={select} onRefresh={refresh} />
      {:else if page === "versions"}
        <Versions {ov} onRefresh={refresh} />
      {:else if page === "mods"}
        <Mods {ov} {selected} onSelect={select} />
      {:else}
        <Settings {ov} onRefresh={refresh} />
      {/if}
    </main>
  </div>
{/if}

{#if updates.overlay}
  <div class="updating" role="status">
    <div class="window box">
      {#if updates.state.kind === "downloading"}
        <h2>Updating the launcher</h2>
        <p class="muted">Downloading version {updates.state.version}</p>
        <div class="bar"><div class="fill" style:width="{percent ?? 30}%" class:moving={percent === null}></div></div>
      {:else if updates.state.kind === "installing"}
        <h2>Updating the launcher</h2>
        <p class="muted">Installing version {updates.state.version}</p>
        <div class="bar"><div class="fill moving" style:width="30%"></div></div>
      {/if}
    </div>
  </div>
{/if}

<dialog bind:this={askDialog} class="modal">
  <h2>Add Pokémon {askRom === "white" ? "White" : "Black"}</h2>
  <p class="muted">
    The {askRom === "white" ? "White" : "Black"} profile needs your own Pokémon {askRom === "white" ? "White" : "Black"} ROM.
    Add it in Settings, ROMs.
  </p>
  <div class="buttons">
    <button class="quiet" onclick={() => askDialog?.close()}>Not now</button>
    <button class="primary" onclick={addRom}>Go to ROMs</button>
  </div>
</dialog>

<dialog bind:this={inviteDialog} class="modal">
  {#if ov && inviteCode}
    <h2>Join game {inviteCode}</h2>
    <p class="muted">Both players need the same version.</p>
    <label class="field" style="margin-top: 1rem">
      <span>Profile</span>
      <select bind:value={inviteProfile}>
        {#each ov.profiles as p}<option value={p.name}>{p.name} ({versionLabel(p.version)})</option>{/each}
      </select>
    </label>
    <div class="buttons">
      <button class="quiet" onclick={() => inviteDialog?.close()}>Cancel</button>
      <button class="primary" disabled={ov.running.includes(inviteProfile)} onclick={join}>Join</button>
    </div>
  {/if}
</dialog>

<style>
  .state {
    display: grid;
    place-items: center;
    height: 100%;
  }
  .shell {
    display: grid;
    grid-template-columns: 10.5rem 1fr;
    height: 100%;
  }
  nav {
    display: flex;
    flex-direction: column;
    background: var(--panel);
    border-right: 2px solid var(--outline);
    padding: 1rem 0.6rem;
  }
  .brand {
    font-size: 1.15rem;
    padding: 0 0.6rem 1rem;
  }
  .links {
    display: flex;
    flex-direction: column;
    gap: 0.15rem;
  }
  /* Black / White: a flat two-way switch the width of the links, the chosen
     half a plate in the game's own colour (black for Black, white for
     White), in the interface's own font. */
  .game-toggle {
    position: relative;
    margin: auto 0 0.9rem;
    display: grid;
    grid-template-columns: 1fr 1fr;
    padding: 3px;
    border-radius: 8px;
    background: var(--panel-3);
    border: 1px solid var(--line-strong);
  }
  .game-toggle .knob {
    position: absolute;
    top: 3px;
    bottom: 3px;
    left: 3px;
    width: calc((100% - 6px) / 2);
    border-radius: 6px;
    background: #17171b;
    box-shadow: 0 1px 2px rgb(0 0 0 / 0.3);
    transition:
      transform 0.2s cubic-bezier(0.3, 0.7, 0.2, 1),
      background-color 0.2s;
  }
  .game-toggle.white .knob {
    transform: translateX(100%);
    background: #fbfaf7;
    box-shadow: 0 1px 2px rgb(0 0 0 / 0.2), inset 0 0 0 1px rgb(0 0 0 / 0.1);
  }
  .game-toggle button {
    position: relative;
    z-index: 1;
    display: flex;
    align-items: center;
    justify-content: center;
    height: 2rem;
    min-height: 0;
    min-width: 0;
    padding: 0;
    margin: 0;
    border: none;
    border-radius: 6px;
    background: transparent;
    box-shadow: none;
    font: inherit;
    font-size: 0.875rem;
    font-weight: 600;
    color: var(--muted);
    transition: color 0.2s;
  }
  .game-toggle button:hover {
    color: var(--text);
  }
  .game-toggle button.black.on {
    color: #f4f2ec;
  }
  .game-toggle button.white.on {
    color: #16161a;
  }
  .game-toggle button:focus-visible {
    outline: 2px solid var(--accent);
    outline-offset: 1px;
  }
  .foot {
    display: flex;
    flex-direction: column;
    padding: 0 0.6rem;
  }
  /* The window never scrolls: each page fits it, and its lists scroll
     inside their panels (launcher.css, .page). */
  main {
    min-width: 0;
    min-height: 0;
    overflow: hidden;
    display: flex;
    flex-direction: column;
    padding: 1.5rem 1.75rem;
  }
  @media (max-height: 600px) {
    main {
      padding: 1rem 1.25rem;
    }
  }
  main > :global(*) {
    flex: 1 1 auto;
    min-height: 0;
  }
  /* A phone: the pages take the whole width and the links become a bar
     along the bottom, in thumb's reach. */
  @media (max-width: 640px) {
    .shell {
      grid-template-columns: minmax(0, 1fr);
      grid-template-rows: minmax(0, 1fr) auto;
    }
    nav {
      grid-row: 2;
      flex-direction: row;
      border-right: none;
      border-top: 2px solid var(--outline);
      padding: 0.35rem 0.25rem calc(0.35rem + env(safe-area-inset-bottom));
    }
    .brand,
    .foot,
    .game-toggle {
      display: none;
    }
    .links {
      flex-direction: row;
      flex: 1;
      justify-content: space-around;
      gap: 0;
    }
    .links :global(.row-item) {
      flex-direction: column;
      gap: 0.15rem;
      width: auto;
      flex: 1;
      min-width: 0;
      padding: 0.35rem 0.2rem;
      font-size: 0.72rem;
      text-align: center;
      justify-content: center;
    }
    main {
      grid-row: 1;
      padding: calc(0.9rem + env(safe-area-inset-top)) 0.9rem 0.9rem;
    }
  }
  .updating {
    position: fixed;
    inset: 0;
    display: grid;
    place-items: center;
    background: rgba(0, 0, 0, 0.5);
    z-index: 50;
  }
  .box {
    width: min(26rem, calc(100vw - 2rem));
    padding: 1.5rem;
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
  }
  .box h2 {
    margin: 0;
    font-size: 1.15rem;
  }
  .bar {
    height: 6px;
    border-radius: 999px;
    background: var(--panel-3);
    overflow: hidden;
    margin-top: 0.5rem;
  }
  .fill {
    height: 100%;
    background: var(--accent);
    border-radius: 999px;
    transition: width 0.2s;
  }
  .fill.moving {
    animation: slide 1.2s ease-in-out infinite;
  }
  @keyframes slide {
    from {
      transform: translateX(-100%);
    }
    to {
      transform: translateX(340%);
    }
  }
</style>
