<script module lang="ts">
  import type { Catalog as CatalogT, ModInfo as ModInfoT } from "../lib/backend";

  // Kept while the launcher runs: the page is rebuilt each time its tab
  // opens, and the catalog is a download. The refresh button reloads it.
  let cachedCatalog: CatalogT | null = null;
  const cachedMods = new Map<string, ModInfoT[]>();
</script>

<script lang="ts">
  import { untrack } from "svelte";
  import { MOBILE } from "../lib/platform";
  import { open } from "@tauri-apps/plugin-dialog";
  import { toast } from "morphe/menu.svelte";
  import {
    message,
    modsAdd,
    modsInspect,
    modsList,
    modsRemove,
    modsSetEnabled,
    openFolder,
    getSettings,
    catalogList,
    catalogInstall,
    type Catalog,
    type CatalogMod,
    type ModInfo,
    type Overview,
  } from "../lib/backend";
  import Icon from "../lib/Icon.svelte";
  import ModIcon from "../lib/ModIcon.svelte";

  let { ov, selected, onSelect }: { ov: Overview; selected: string; onSelect: (name: string) => void } = $props();

  // The game species icons are drawn from: the profile's.
  const game = $derived(ov.profiles.find((p) => p.name === selected)?.game || "black");

  let mods = $state<ModInfo[]>(untrack(() => cachedMods.get(selected)) ?? []);
  let indexUrl = $state("");
  let removing = $state<ModInfo | null>(null);
  let removeDialog = $state<HTMLDialogElement>();
  let adding = $state<{ path: string; label: string } | null>(null);
  let addDialog = $state<HTMLDialogElement>();

  const running = $derived(ov.running.includes(selected));
  const enabled = $derived(mods.filter((m) => m.enabled).length);

  async function load() {
    try {
      const name = selected;
      const list = await modsList(name);
      cachedMods.set(name, list);
      if (name === selected) mods = list;
    } catch (e) {
      toast(message(e), true);
    }
  }

  // The installed list is read again on each visit (it is local and quick),
  // but the last one shows meanwhile, so the list does not flash empty.
  $effect(() => {
    mods = cachedMods.get(selected) ?? [];
    load();
  });

  $effect(() => {
    getSettings().then((s) => (indexUrl = s.indexUrl));
  });

  // The katalogos index: the mods that can be installed.
  let catalog = $state<Catalog | null>(cachedCatalog);
  let catalogError = $state("");
  let loadingCatalog = $state(false);
  let installing = $state<string | null>(null);

  async function loadCatalog() {
    loadingCatalog = true;
    catalogError = "";
    try {
      catalog = await catalogList();
      cachedCatalog = catalog;
    } catch (e) {
      catalogError = message(e);
    } finally {
      loadingCatalog = false;
    }
  }

  // Downloaded once a launcher run; the refresh button gets it again.
  $effect(() => {
    if (cachedCatalog === null) loadCatalog();
  });

  const installedOf = (c: CatalogMod) => mods.find((m) => m.catalogId === c.id);

  function modState(c: CatalogMod): "install" | "update" | "installed" {
    const m = installedOf(c);
    if (!m) return "install";
    return m.version === c.version ? "installed" : "update";
  }

  async function install(c: CatalogMod) {
    installing = c.id;
    const s = modState(c);
    await run(() => catalogInstall(selected, c.index), s === "update" ? `${c.name} updated.` : `${c.name} installed.`);
    installing = null;
  }

  const size = (b: number) => (b < 1048576 ? `${Math.max(1, Math.round(b / 1024))} KB` : `${(b / 1048576).toFixed(1)} MB`);

  async function run(task: () => Promise<unknown>, done?: string) {
    try {
      await task();
      await load();
      if (done) toast(done);
    } catch (e) {
      toast(message(e), true);
    }
  }

  async function pickPackage() {
    const path = await open({ multiple: false, title: "Add a mod", filters: [{ name: "Mod package", extensions: ["bw"] }] });
    if (typeof path !== "string") return;
    try {
      const info = await modsInspect(path);
      adding = { path, label: `${info.name}: ${info.files} files, ${(info.bytes / 1048576).toFixed(1)} MB` };
      addDialog?.showModal();
    } catch (e) {
      toast(message(e), true);
    }
  }

  async function pickFolder() {
    const path = await open({ directory: true, multiple: false, title: "Add a mod folder" });
    if (typeof path === "string") run(() => modsAdd(selected, path), "Mod added.");
  }

  const confirmAdd = () =>
    run(async () => {
      if (adding) await modsAdd(selected, adding.path);
      addDialog?.close();
    }, "Mod added.");

  function askRemove(m: ModInfo) {
    removing = m;
    removeDialog?.showModal();
  }

  const confirmRemove = () =>
    run(async () => {
      if (removing) await modsRemove(selected, removing.folder);
      removeDialog?.close();
    });
</script>

<section class="page">
  <header class="page-head">
    <h1>Mods</h1>
  </header>

  <div class="window bar">
    <label class="field">
      <span>Profile</span>
      <select value={selected} onchange={(e) => onSelect(e.currentTarget.value)}>
        {#each ov.profiles as p}<option value={p.name}>{p.name}</option>{/each}
      </select>
    </label>
    <div class="grow"></div>
    <button class="primary" disabled={running} onclick={pickPackage}><Icon name="upload" size={15} />Add .bw file</button>
    {#if !MOBILE}
    <button disabled={running} onclick={pickFolder}><Icon name="plus" size={15} />Add folder</button>
    <button class="quiet outlined" onclick={() => openFolder("mods", selected)}><Icon name="folder" size={15} />Open folder</button>
    {/if}
  </div>
  {#if running}<p class="muted">Close the game to change mods.</p>{/if}

  <div>
    <h2 class="section-label">{mods.length} {mods.length === 1 ? "mod" : "mods"} · {enabled} on</h2>
    <ul class="rows window">
      {#each mods as m (m.folder)}
        <li class:off={!m.enabled}>
          <label class="switch" title={m.enabled ? "On" : "Off"}>
            <input
              type="checkbox"
              checked={m.enabled}
              disabled={running}
              onchange={(e) => run(() => modsSetEnabled(selected, m.folder, e.currentTarget.checked))} />
            <span class="track" aria-hidden="true"></span>
          </label>
          <ModIcon icon={m.icon} {game} />
          <div class="main">
            <span class="title">{m.name}</span>
            {#if m.name !== m.folder}<span class="sub">{m.folder}</span>{/if}
          </div>
          {#if !m.hasManifest}<span class="badge">No manifest</span>{/if}
          <div class="actions">
            <button class="quiet danger" disabled={running} title="Remove" onclick={() => askRemove(m)}><Icon name="trash" size={15} /></button>
          </div>
        </li>
      {:else}
        <li class="empty">No mods</li>
      {/each}
    </ul>
  </div>

  <div>
    <div class="catalog-head">
      <h2 class="section-label">Get mods</h2>
      <button class="quiet" disabled={loadingCatalog} title="Refresh" onclick={loadCatalog}><Icon name="refresh" size={15} /></button>
    </div>
    {#if indexUrl}<p class="muted">From {indexUrl}</p>{/if}
    {#if catalogError}
      <p class="muted">Could not read the mod list: {catalogError}</p>
    {/if}
    <ul class="rows window">
      {#if catalog}
        {#each catalog.mods as c (c.id)}
          {@const st = modState(c)}
          <li class="catalog">
            <ModIcon icon={c.icon} {game} />
            <div class="main">
              <span class="title">
                {c.name}
                {#if c.official}<span class="badge accent">Official</span>{/if}
                {#if c.multiplayer}<span class="badge">Multiplayer</span>{/if}
                {#if c.hasCode}<span class="badge" title="This mod runs its own code in the game">Code</span>{/if}
              </span>
              <span class="sub">{c.author} · {c.version} · {size(c.size)}{c.games.length === 1 ? ` · ${c.games[0] === "black" ? "Black" : "White"} only` : ""}</span>
              <span class="desc">{c.description}</span>
            </div>
            <div class="actions">
              {#if st === "installed"}
                <span class="badge"><Icon name="check" size={13} />Installed</span>
              {:else}
                <button class={st === "update" ? "primary" : ""} disabled={running || installing !== null} onclick={() => install(c)}>
                  <Icon name="download" size={15} />{installing === c.id ? "Installing" : st === "update" ? "Update" : "Install"}
                </button>
              {/if}
            </div>
          </li>
        {:else}
          <li class="empty">No mods in the list</li>
        {/each}
        {#each catalog.errors as e}
          <li class="empty">{e.entry}: {e.error}</li>
        {/each}
      {:else if loadingCatalog}
        <li class="empty">Loading the mod list</li>
      {/if}
    </ul>
  </div>
</section>

<dialog bind:this={addDialog} class="modal">
  <h2>Add this mod?</h2>
  <p class="muted">{adding?.label}</p>
  <div class="buttons">
    <button class="quiet" onclick={() => addDialog?.close()}>Cancel</button>
    <button class="primary" onclick={confirmAdd}>Add</button>
  </div>
</dialog>

<dialog bind:this={removeDialog} class="modal">
  <h2>Remove {removing?.name}?</h2>
  <p class="muted">The mod's folder is deleted from this profile.</p>
  <div class="buttons">
    <button class="quiet" onclick={() => removeDialog?.close()}>Cancel</button>
    <button class="primary danger" onclick={confirmRemove}>Remove</button>
  </div>
</dialog>

<style>
  .bar {
    display: flex;
    align-items: flex-end;
    gap: 0.6rem;
    padding: 1rem;
    flex-wrap: wrap;
  }
  .bar select {
    width: 12rem;
  }
  .off .title {
    color: var(--muted);
  }
  .catalog-head {
    display: flex;
    align-items: center;
    justify-content: space-between;
  }
  .catalog .title {
    display: inline-flex;
    align-items: center;
    gap: 0.4rem;
    flex-wrap: wrap;
  }
  .desc {
    color: var(--muted);
    font-size: 0.85rem;
    line-height: 1.35;
  }
  .switch {
    position: relative;
    display: inline-flex;
    flex: none;
  }
  .switch input {
    position: absolute;
    opacity: 0;
    inset: 0;
    margin: 0;
    width: 100%;
    height: 100%;
    cursor: pointer;
  }
  .track {
    width: 2.3rem;
    height: 1.3rem;
    border-radius: 999px;
    background: var(--panel-3);
    border: 1px solid var(--line-strong);
    position: relative;
    transition: background-color 0.15s;
  }
  .track::after {
    content: "";
    position: absolute;
    top: 2px;
    left: 2px;
    width: calc(1.3rem - 6px);
    height: calc(1.3rem - 6px);
    border-radius: 50%;
    background: var(--muted);
    transition: transform 0.15s, background-color 0.15s;
  }
  .switch input:checked + .track {
    background: var(--accent);
    border-color: var(--accent);
  }
  .switch input:checked + .track::after {
    transform: translateX(1rem);
    background: var(--accent-ink);
  }
  .switch input:disabled + .track {
    opacity: 0.5;
  }
</style>
