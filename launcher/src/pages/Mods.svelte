<script lang="ts">
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
    type ModInfo,
    type Overview,
  } from "../lib/backend";
  import Icon from "../lib/Icon.svelte";

  let { ov, selected, onSelect }: { ov: Overview; selected: string; onSelect: (name: string) => void } = $props();

  let mods = $state<ModInfo[]>([]);
  let indexUrl = $state("");
  let removing = $state<ModInfo | null>(null);
  let removeDialog = $state<HTMLDialogElement>();
  let adding = $state<{ path: string; label: string } | null>(null);
  let addDialog = $state<HTMLDialogElement>();

  const running = $derived(ov.running.includes(selected));
  const enabled = $derived(mods.filter((m) => m.enabled).length);

  async function load() {
    try {
      mods = await modsList(selected);
    } catch (e) {
      toast(message(e), true);
    }
  }

  $effect(() => {
    selected;
    load();
  });

  $effect(() => {
    getSettings().then((s) => (indexUrl = s.indexUrl));
  });

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
    <h2 class="section-label">Index</h2>
    <p class="muted">{indexUrl ? indexUrl : "No index."}</p>
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
