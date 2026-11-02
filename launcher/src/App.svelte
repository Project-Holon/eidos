<script lang="ts">
  import { onMount } from "svelte";
  import { listen } from "@tauri-apps/api/event";
  import { toast } from "morphe/menu.svelte";
  import {
    message,
    overview,
    play,
    takePendingJoin,
    versionLabel,
    type Overview,
  } from "./lib/backend";
  import Icon from "./lib/Icon.svelte";
  import { MOBILE } from "./lib/platform";
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
      return localStorage.getItem(KEY) ?? "Default";
    } catch {
      return "Default";
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
      if (!ov.profiles.some((p) => p.name === selected)) select("Default");
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
    return () => stops.forEach((s) => s.then((stop) => stop()));
  });

  const current = $derived(ov?.profiles.find((p) => p.name === selected));
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
        {#each PAGES.filter(([id]) => !(MOBILE && id === "versions")) as [id, label, icon]}
          <button class="row-item" aria-current={page === id} onclick={() => (page = id)}>
            <Icon name={icon} />
            <span>{label}</span>
          </button>
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
        <Home {ov} {selected} {current} onSelect={select} onRefresh={refresh} onGoto={(p: Page) => (page = p)} />
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
  .foot {
    margin-top: auto;
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
    .foot {
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
