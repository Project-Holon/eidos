<script lang="ts">
  import { open } from "@tauri-apps/plugin-dialog";
  import { toast } from "morphe/menu.svelte";
  import {
    LATEST,
    LOCAL,
    createProfile,
    deleteProfile,
    duplicateProfile,
    message,
    moveProfileData,
    openFolder,
    renameProfile,
    languageName,
    languagesOf,
    romLibrary,
    setProfileDataRoot,
    setProfileLanguage,
    setProfileVersion,
    versionLabel,
    type LanguageChoice,
    type Overview,
  } from "../lib/backend";
  import Icon from "../lib/Icon.svelte";

  let {
    ov,
    selected,
    onSelect,
    onRefresh,
  }: {
    ov: Overview;
    selected: string;
    onSelect: (name: string) => void;
    onRefresh: () => Promise<void> | void;
  } = $props();

  let newName = $state("");
  let newVersion = $state(LATEST);
  let renaming = $state<string | null>(null);
  let renameTo = $state("");
  let deleting = $state<string | null>(null);
  let moveAsk = $state<{ name: string; path: string } | null>(null);
  let moving = $state<string | null>(null);
  let deleteDialog = $state<HTMLDialogElement>();

  // The languages the ROMs cover: a profile plays in one of them.  The
  // choice always shows, greyed out while there is only one.
  let languages = $state<LanguageChoice[]>([]);
  $effect(() => {
    romLibrary().then((r) => (languages = languagesOf(r))).catch(() => {});
  });
  const langChoices = $derived<LanguageChoice[]>(languages.length ? languages : [{ code: "en", name: "English" }]);
  const langLabel = (l: LanguageChoice) => (l.note ? `${l.name} (${l.note})` : l.name);

  // Latest, the local build when set, then the installed versions.
  const choices = $derived([LATEST, ...(ov.localBuild ? [LOCAL] : []), ...ov.installed]);

  async function run(task: () => Promise<unknown>, done?: string) {
    try {
      await task();
      await onRefresh();
      if (done) toast(done);
    } catch (e) {
      toast(message(e), true);
    }
  }

  const create = () =>
    run(async () => {
      const p = await createProfile(newName, newVersion);
      newName = "";
      onSelect(p.name);
    }, "Profile created.");

  const rename = (from: string) =>
    run(async () => {
      await renameProfile(from, renameTo);
      if (selected === from) onSelect(renameTo.trim());
      renaming = null;
    });

  const duplicate = (name: string) => run(() => duplicateProfile(name), "Profile copied.");

  async function pickData(name: string) {
    const path = await open({ directory: true, multiple: false, title: "Game data folder" });
    if (typeof path === "string") await run(() => setProfileDataRoot(name, path));
  }

  async function askMove(name: string) {
    const path = await open({ directory: true, multiple: false, title: "Move the game data to" });
    if (typeof path === "string") moveAsk = { name, path };
  }

  async function doMove() {
    const ask = moveAsk;
    if (!ask) return;
    moveAsk = null;
    moving = ask.name;
    try {
      toast(await moveProfileData(ask.name, ask.path));
      await onRefresh();
    } catch (e) {
      toast(message(e), true);
    } finally {
      moving = null;
    }
  }

  function askDelete(name: string) {
    deleting = name;
    deleteDialog?.showModal();
  }

  const confirmDelete = () =>
    run(async () => {
      if (deleting) await deleteProfile(deleting);
      deleting = null;
      deleteDialog?.close();
    });
</script>

<section class="page">
  <header class="page-head">
    <h1>Profiles</h1>
  </header>

  <div>
    <h2 class="section-label">Profiles</h2>
    <ul class="rows window">
      {#each ov.profiles as p (p.name)}
        <li class:chosen={selected === p.name}>
          {#if renaming === p.name}
            <input class="grow" bind:value={renameTo} onkeydown={(e) => e.key === "Enter" && rename(p.name)} />
            <div class="actions">
              <button class="primary" onclick={() => rename(p.name)}>Save</button>
              <button class="quiet" onclick={() => (renaming = null)}>Cancel</button>
            </div>
          {:else}
            <button class="pick" onclick={() => onSelect(p.name)} title="Play with this profile">
              <span class="dot" aria-hidden="true"></span>
              <span class="main">
                <span class="title">{p.name}</span>
              </span>
            </button>
            {#if ov.running.includes(p.name)}<span class="badge ok">Running</span>{/if}
            <select
              value={p.version}
              aria-label="Version of {p.name}"
              onchange={(e) => run(() => setProfileVersion(p.name, e.currentTarget.value))}>
              {#each choices as c}<option value={c}>{versionLabel(c)}</option>{/each}
              {#if !choices.includes(p.version)}<option value={p.version}>{p.version} (not installed)</option>{/if}
            </select>
            <div class="actions">
              <button class="quiet outlined" title="Open this profile's folder" onclick={() => openFolder("profile", p.name)}><Icon name="folder" size={15} />Folder</button>
              <button class="quiet outlined" title="Make a copy of this profile" onclick={() => duplicate(p.name)}><Icon name="copy" size={15} />Copy</button>
              {#if p.name !== "Default"}
                <button class="quiet" title="Rename" onclick={() => ((renaming = p.name), (renameTo = p.name))}><Icon name="edit" size={15} /></button>
                <button class="quiet danger" title="Delete" onclick={() => askDelete(p.name)}><Icon name="trash" size={15} /></button>
              {/if}
            </div>
            <div class="data">
              {#if moveAsk?.name === p.name}
                <span class="path" title={moveAsk.path}>Move the game data to {moveAsk.path}?</span>
                <button class="primary" onclick={doMove}>Move</button>
                <button class="quiet outlined" onclick={() => (moveAsk = null)}>Cancel</button>
              {:else}
                {@const busy = moving === p.name || ov.running.includes(p.name)}
                <span class="label">Game data folder</span>
                <span class="path" class:faint={!p.dataRoot} title={p.dataRoot}>{moving === p.name ? "Moving..." : p.dataRoot || "Default"}</span>
                <button class="quiet outlined" disabled={busy} onclick={() => pickData(p.name)}><Icon name="folder" size={15} />Choose...</button>
                <button class="quiet outlined" disabled={busy} title="Move the existing game data to another folder" onclick={() => askMove(p.name)}>Move...</button>
                <button class="quiet outlined" disabled={!p.dataRoot || busy} onclick={() => run(() => setProfileDataRoot(p.name, ""))}>Use default</button>
              {/if}
            </div>
            <div class="data">
              <span class="label lang-label"><Icon name="translate" size={15} />Language</span>
              <select
                class="lang-select"
                value={p.language || "en"}
                aria-label="Language of {p.name}"
                disabled={langChoices.length < 2 || ov.running.includes(p.name)}
                title={langChoices.length < 2 ? "Add ROMs in other languages in Settings to choose one" : undefined}
                onchange={(e) => run(() => setProfileLanguage(p.name, e.currentTarget.value))}>
                {#each langChoices as l (l.code)}<option value={l.code}>{langLabel(l)}</option>{/each}
                {#if !langChoices.some((l) => l.code === (p.language || "en"))}
                  <option value={p.language}>{languageName(p.language)} (no ROM)</option>
                {/if}
              </select>
            </div>
          {/if}
        </li>
      {/each}
    </ul>
  </div>

  <div>
    <h2 class="section-label">New profile</h2>
    <div class="window create">
      <label class="field grow">
        <span>Name</span>
        <input placeholder="Name" bind:value={newName} onkeydown={(e) => e.key === "Enter" && create()} />
      </label>
      <label class="field">
        <span>Version</span>
        <select bind:value={newVersion}>
          {#each choices as c}<option value={c}>{versionLabel(c)}</option>{/each}
        </select>
      </label>
      <button class="primary" disabled={!newName.trim()} onclick={create}><Icon name="plus" size={16} />Create</button>
    </div>
  </div>
</section>

<dialog bind:this={deleteDialog} class="modal" onclose={() => (deleting = null)}>
  <h2>Delete {deleting}?</h2>
  <p class="muted">Its mods, saves and settings are deleted with it. This cannot be undone.</p>
  <div class="buttons">
    <button class="quiet" onclick={() => deleteDialog?.close()}>Cancel</button>
    <button class="primary danger" onclick={confirmDelete}>Delete</button>
  </div>
</dialog>

<style>
  .lang-select:disabled {
    opacity: 0.45;
    cursor: not-allowed;
  }
  .lang-label {
    display: inline-flex;
    align-items: center;
    gap: 0.35rem;
  }
  .pick {
    flex: 1;
    min-width: 0;
    justify-content: flex-start;
    gap: 0.75rem;
    background: none;
    border: 0;
    box-shadow: none;
    padding: 0;
    font-weight: 400;
    text-align: left;
  }
  .pick:hover:not(:disabled) {
    border: 0;
  }
  .dot {
    width: 0.8rem;
    height: 0.8rem;
    border-radius: 50%;
    border: 2px solid var(--line-strong);
    flex: none;
  }
  .chosen .dot {
    border-color: var(--accent);
    background: var(--accent);
    box-shadow: 0 0 0 3px var(--accent-soft);
  }
  .rows > li {
    flex-wrap: wrap;
  }
  .data {
    flex-basis: 100%;
    display: flex;
    align-items: center;
    gap: 0.5rem;
    padding-left: 1.55rem;
    font-size: 0.85em;
  }
  .data .label {
    color: var(--muted);
    white-space: nowrap;
  }
  .data .path {
    flex: 1;
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  select {
    width: 9rem;
  }
  .create {
    display: flex;
    align-items: flex-end;
    gap: 0.75rem;
    padding: 1rem;
    flex-wrap: wrap;
  }
  .create select {
    width: 10rem;
  }
</style>
