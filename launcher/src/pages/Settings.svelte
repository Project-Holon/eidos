<script lang="ts">
  import { open } from "@tauri-apps/plugin-dialog";
  import { getCurrentWebview } from "@tauri-apps/api/webview";
  import ThemePicker from "morphe/ThemePicker.svelte";
  import { toast } from "morphe/menu.svelte";
  import {
    addRoms,
    getSettings,
    locateRom,
    message,
    openFolder,
    removeRom,
    romLibrary,
    saveSettings,
    type Overview,
    type RomView,
  } from "../lib/backend";
  import Icon from "../lib/Icon.svelte";
  import { autoUpdate, checkForUpdate, setAutoUpdate, updates } from "../lib/updater.svelte";

  let { ov, onRefresh }: { ov: Overview; onRefresh: () => Promise<void> | void } = $props();

  let repo = $state("");
  let indexUrl = $state("");
  let localBuild = $state("");
  let tokenSet = $state(false);
  // Empty field keeps the saved token; "clear" removes it.
  let token = $state("");
  let clearToken = $state(false);
  let auto = $state(autoUpdate());
  let roms = $state<RomView[]>([]);
  let adding = $state(false);
  let dropping = $state(false);

  $effect(() => {
    getSettings().then((s) => {
      repo = s.repo;
      indexUrl = s.indexUrl;
      localBuild = s.localBuild ?? "";
      tokenSet = s.tokenSet;
    });
    romLibrary().then((r) => (roms = r)).catch((e) => toast(message(e), true));
  });

  // Files dropped on the window join the library.
  $effect(() => {
    let stop: (() => void) | undefined;
    getCurrentWebview()
      .onDragDropEvent((e) => {
        if (e.payload.type === "over" || e.payload.type === "enter") dropping = true;
        else if (e.payload.type === "leave") dropping = false;
        else if (e.payload.type === "drop") {
          dropping = false;
          add(e.payload.paths);
        }
      })
      .then((f) => (stop = f))
      .catch(() => {});
    return () => stop?.();
  });

  const gameName = (r: RomView) =>
    r.game === "black" ? "Pokémon Black" : r.game === "white" ? "Pokémon White" : "Unknown game";

  async function add(paths: string[]) {
    if (paths.length === 0) return;
    adding = true;
    try {
      const r = await addRoms(paths);
      roms = r.library;
      if (r.refused.length) toast(r.refused.join("\n"), true);
      else if (r.added) toast(r.added === 1 ? "ROM added." : `${r.added} ROMs added.`);
    } catch (e) {
      toast(message(e), true);
    } finally {
      adding = false;
    }
  }

  async function addFiles() {
    const p = await open({ multiple: true, title: "Add ROMs", filters: [{ name: "DS ROM", extensions: ["nds"] }] });
    if (p) await add(Array.isArray(p) ? p : [p]);
  }

  async function addFolder() {
    const p = await open({ directory: true, multiple: false, title: "A folder with ROMs" });
    if (typeof p === "string") await add([p]);
  }

  async function locate(r: RomView) {
    const p = await open({ multiple: false, title: `Where is ${fileName(r.path)}?`, filters: [{ name: "DS ROM", extensions: ["nds"] }] });
    if (typeof p !== "string") return;
    try {
      roms = await locateRom(r.path, p);
    } catch (e) {
      toast(message(e), true);
    }
  }

  async function remove(r: RomView) {
    try {
      roms = await removeRom(r.path);
    } catch (e) {
      toast(message(e), true);
    }
  }

  const fileName = (p: string) => p.split(/[\\/]/).pop() ?? p;

  async function pickBuild() {
    const path = await open({ directory: true, multiple: false, title: "Folder with an eidos build" });
    if (typeof path === "string") localBuild = path;
  }

  async function save() {
    try {
      const s = await saveSettings({
        repo,
        token: clearToken ? "" : token ? token : null,
        indexUrl,
        localBuild: localBuild || null,
      });
      tokenSet = s.tokenSet;
      token = "";
      clearToken = false;
      await onRefresh();
      toast("Saved.");
    } catch (e) {
      toast(message(e), true);
    }
  }

  async function checkNow() {
    await checkForUpdate(true);
    if (updates.state.kind === "current") toast("The launcher is up to date.");
    else if (updates.state.kind === "failed") toast(updates.state.message, true);
  }
</script>

<section class="page">
  <header class="page-head">
    <h1>Settings</h1>
  </header>

  <div>
    <h2 class="section-label">Appearance</h2>
    <div class="window block">
      <ThemePicker />
    </div>
  </div>

  <div>
    <h2 class="section-label">ROMs</h2>
    <div class="window block" class:dropping>
      {#if roms.length === 0}
        <div class="empty muted">No ROMs yet. Add your own dumps of Pokémon Black or White, or drop them here.</div>
      {:else}
        <div class="roms">
          {#each roms as r (r.path)}
            <div class="inline rom">
              <span class="cart {r.game ?? ''}"></span>
              <div class="grow">
                <div class="inline head">
                  <span class="title">{gameName(r)}</span>
                  <span class="badge">{r.language}</span>
                  {#if r.inUse}<span class="badge accent">In use</span>{/if}
                </div>
                <div class="muted small path" title={r.path}>{fileName(r.path)}</div>
              </div>
              {#if r.status === "verified"}
                <span class="badge ok" title={r.name ?? ""}>Verified</span>
              {:else if r.status === "modified"}
                <span class="badge bad" title="Not a known good dump: edited, damaged or another revision">Modified</span>
              {:else}
                <span class="badge bad">Missing</span>
                <button onclick={() => locate(r)}><Icon name="folder" size={15} />Locate</button>
              {/if}
              <button class="quiet" onclick={() => remove(r)} title="Remove from the list"><Icon name="trash" size={15} /></button>
            </div>
          {/each}
        </div>
      {/if}
      <div class="inline buttons">
        <span class="muted small grow">Each file is checked against the known good dumps. The files stay where they are.</span>
        <button disabled={adding} onclick={addFolder}><Icon name="folder" size={15} />Add folder</button>
        <button class="primary" disabled={adding} onclick={addFiles}><Icon name="plus" size={15} />{adding ? "Checking" : "Add ROMs"}</button>
      </div>
    </div>
  </div>

  <div>
    <h2 class="section-label">Updates</h2>
    <div class="window block">
      <div class="inline">
        <div class="grow">
          <div class="title">Launcher {ov.launcherVersion}</div>
          <div class="muted small">
            {#if updates.state.kind === "checking"}Checking for updates
            {:else if updates.state.kind === "current"}Up to date
            {:else if updates.state.kind === "failed"}{updates.state.message}
{/if}
          </div>
        </div>
        <button disabled={updates.state.kind === "checking"} onclick={checkNow}><Icon name="refresh" size={15} />Check now</button>
      </div>
      <label class="inline check">
        <input type="checkbox" bind:checked={auto} onchange={() => setAutoUpdate(auto)} />
        Update on start
      </label>
    </div>
  </div>

  <div>
    <h2 class="section-label">Downloads</h2>
    <div class="window block">
      <label class="field">
        <span>Releases repository</span>
        <input bind:value={repo} placeholder="owner/name" />
      </label>
      <label class="field">
        <span>GitHub token {tokenSet ? "(saved)" : ""}</span>
        <input type="password" bind:value={token} placeholder="Only for a private repository" autocomplete="off" />
        {#if tokenSet}
          <span class="inline check"><input type="checkbox" bind:checked={clearToken} /> Remove the saved token</span>
        {/if}
        <span class="hint">Stored as plain text in the launcher's settings file.</span>
      </label>
      <label class="field">
        <span>Local build folder</span>
        <span class="inline">
          <input class="grow" bind:value={localBuild} placeholder="A folder with eidos.exe or eidos_boot.exe" />
          <button onclick={pickBuild}><Icon name="folder" size={15} />Choose</button>
        </span>
      </label>
      <label class="field">
        <span>Mods index address</span>
        <input bind:value={indexUrl} placeholder="Empty: no index" />
      </label>
      <div class="inline buttons">
        <button class="quiet outlined" onclick={() => openFolder("root")}><Icon name="folder" size={15} />Open the eidos folder</button>
        <div class="grow"></div>
        <button class="primary" onclick={save}><Icon name="check" size={15} />Save</button>
      </div>
    </div>
  </div>
</section>

<style>
  .page {
    max-width: 40rem;
  }
  .block {
    display: flex;
    flex-direction: column;
    gap: 1rem;
    padding: 1.1rem 1.2rem;
  }
  .title {
    font-weight: 600;
  }
  .rom .grow {
    min-width: 0;
  }
  .roms {
    display: flex;
    flex-direction: column;
  }
  .rom {
    gap: 0.7rem;
    padding: 0.55rem 0;
  }
  .rom + .rom {
    border-top: 1px solid var(--line);
  }
  .head {
    gap: 0.45rem;
  }
  /* the cartridge's colour: Black's dark shell, White's light one */
  .cart {
    flex: none;
    width: 0.8rem;
    height: 2.1rem;
    border-radius: 0.2rem;
    border: 1px solid var(--line-strong);
    background: var(--panel);
  }
  .cart.black {
    background: #1b1b1f;
  }
  .cart.white {
    background: #ececea;
  }
  .empty {
    padding: 1.2rem 0;
    text-align: center;
  }
  .dropping {
    outline: 2px dashed var(--accent);
    outline-offset: -6px;
  }
  .path {
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .check {
    gap: 0.6rem;
    color: var(--ink);
    font-size: 0.9rem;
  }
  .buttons {
    border-top: 1px solid var(--line);
    padding-top: 1rem;
  }
</style>
