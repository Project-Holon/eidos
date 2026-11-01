<script lang="ts">
  import { onMount } from "svelte";
  import { listen } from "@tauri-apps/api/event";
  import { toast } from "morphe/menu.svelte";
  import { installVersion, knownRemote, listRemote, message, removeVersion, type Overview, type Remote } from "../lib/backend";
  import Icon from "../lib/Icon.svelte";

  let { ov, onRefresh }: { ov: Overview; onRefresh: () => Promise<void> | void } = $props();

  let remote = $state<Remote[] | null>(knownRemote());
  let error = $state<string | null>(null);
  let loading = $state(false);
  let busy = $state<string | null>(null);
  let stage = $state("");

  async function load(fresh = false) {
    loading = true;
    error = null;
    try {
      remote = await listRemote(fresh);
    } catch (e) {
      error = message(e);
    } finally {
      loading = false;
    }
  }

  async function install(tag: string) {
    busy = tag;
    try {
      await installVersion(tag);
      toast("Installed.");
      await onRefresh();
      await load(true);
    } catch (e) {
      toast(message(e), true);
    } finally {
      busy = null;
      stage = "";
    }
  }

  async function remove(id: string) {
    try {
      await removeVersion(id);
      await onRefresh();
      await load(true);
    } catch (e) {
      toast(message(e), true);
    }
  }

  const size = (n: number | null) => (n == null ? "" : `${(n / 1048576).toFixed(1)} MB`);
  const date = (s: string | null) =>
    s ? new Date(s).toLocaleDateString(undefined, { year: "numeric", month: "short", day: "numeric" }) : "";
  const newest = $derived(remote?.find((r) => !r.prerelease)?.tag);

  onMount(() => {
    load();
    const stop = listen<[string, string]>("install-progress", (e) => (stage = e.payload[1]));
    return () => {
      stop.then((f) => f());
    };
  });
</script>

<section class="page">
  <header class="page-head">
    <h1>Versions</h1>
  </header>

  <div>
    <h2 class="section-label">Installed</h2>
    <ul class="rows window">
      {#if ov.localBuild}
        <li>
          <div class="main">
            <span class="title">Local build</span>
            <span class="sub">{ov.localBuild}</span>
          </div>
          {#if !ov.localOk}<span class="badge bad">No eidos program</span>{/if}
        </li>
      {/if}
      {#each ov.installed as id, i (id)}
        <li>
          <div class="main">
            <span class="title">eidos {id}</span>
          </div>
          {#if i === 0}<span class="badge">Newest</span>{/if}
          <div class="actions">
            <button class="quiet outlined danger" onclick={() => remove(id)}><Icon name="trash" size={15} />Remove</button>
          </div>
        </li>
      {/each}
      {#if !ov.localBuild && !ov.installed.length}<li class="empty">None</li>{/if}
    </ul>
  </div>

  <div>
    <h2 class="section-label">
      Releases
      <button class="quiet small refresh" disabled={loading} onclick={() => load(true)} title="Refresh the list">
        <Icon name="refresh" size={14} />{loading ? "Loading" : "Refresh"}
      </button>
    </h2>
    {#if error}
      <p class="bad" role="alert">{error}</p>
    {:else if remote && !remote.length}
      <p class="muted">No releases.</p>
    {:else if remote}
      <ul class="rows window">
        {#each remote as r (r.tag)}
          <li>
            <div class="main">
              <span class="title">{r.name}</span>
              <span class="sub">{[date(r.published), size(r.size)].filter(Boolean).join(" · ")}</span>
            </div>
            {#if r.tag === newest}<span class="badge accent">Latest</span>{/if}
            {#if r.prerelease}<span class="badge">Pre-release</span>{/if}
            <div class="actions">
              {#if r.installed}
                <span class="badge ok"><Icon name="check" size={13} />Installed</span>
              {:else if !r.available}
                <span class="badge">Not for this system</span>
              {:else}
                <button class={r.tag === newest && busy !== r.tag ? "primary" : ""} disabled={busy !== null} onclick={() => install(r.tag)}>
                  <Icon name="download" size={15} />{busy === r.tag ? stage || "Working" : "Install"}
                </button>
              {/if}
            </div>
          </li>
        {/each}
      </ul>
    {:else}
      <p class="muted">Loading</p>
    {/if}
  </div>
</section>

<style>
  .refresh {
    padding: 0.15rem 0.5rem;
    font-size: 0.75rem;
    letter-spacing: normal;
    text-transform: none;
  }
</style>
