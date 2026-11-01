<script lang="ts">
  import { onMount } from "svelte";
  import { listen } from "@tauri-apps/api/event";
  import { toast } from "morphe/menu.svelte";
  import {
    LATEST,
    LOCAL,
    coverArt,
    installVersion,
    listRemote,
    message,
    newerThan,
    play,
    supportsOneScreen,
    versionLabel,
    type Layout,
    type Overview,
    type Profile,
    type Remote,
  } from "../lib/backend";
  import Icon from "../lib/Icon.svelte";
  import dragon from "../assets/dragon.png";
  import dragonDay from "../assets/dragon-day.png";

  let {
    ov,
    selected,
    current,
    onSelect,
    onRefresh,
    onGoto,
  }: {
    ov: Overview;
    selected: string;
    current: Profile | undefined;
    onSelect: (name: string) => void;
    onRefresh: () => void;
    onGoto: (page: "versions" | "profiles") => void;
  } = $props();

  let error = $state<string | null>(null);
  let starting = $state(false);

  // The profile's game ("" is Black). With a ROM of that game in the
  // library, its box legendary is shown, drawn from the ROM; without one,
  // the eidos dragon.
  const game = $derived(current?.game === "white" ? "white" : "black");
  const art = new Map<string, string | null>();
  let legend = $state<string | null>(null);
  $effect(() => {
    const g = game;
    if (art.has(g)) {
      legend = art.get(g)!;
      return;
    }
    legend = null;
    coverArt(g)
      .then((url) => {
        art.set(g, url);
        if (g === game) legend = url;
      })
      .catch(() => art.set(g, null));
  });

  // The version a Play press runs, as the backend resolves it.
  const version = $derived.by(() => {
    if (!current) return null;
    if (current.version === LATEST) return ov.installed[0] ?? (ov.localOk ? LOCAL : null);
    if (current.version === LOCAL) return ov.localOk ? LOCAL : null;
    return ov.installed.includes(current.version) ? current.version : null;
  });
  const running = $derived(ov.running.includes(selected));

  const canOne = $derived(supportsOneScreen(version));

  // A newer release than any installed one, for the profiles that follow
  // the latest version.  A failed check shows nothing.
  let remote = $state<Remote[] | null>(null);
  let updating = $state(false);
  let stage = $state("");
  const update = $derived.by(() => {
    if (!remote || !current || current.version !== LATEST) return null;
    const r = remote.find((x) => !x.prerelease && x.available);
    if (!r || r.installed) return null;
    const have = ov.installed[0];
    return !have || newerThan(r.id ?? r.tag, have) ? r : null;
  });

  async function installUpdate(r: Remote) {
    updating = true;
    try {
      await installVersion(r.tag);
      toast(`eidos ${r.id ?? r.tag} installed.`);
      await onRefresh();
      remote = await listRemote();
    } catch (e) {
      toast(message(e), true);
    } finally {
      updating = false;
      stage = "";
    }
  }

  onMount(() => {
    listRemote()
      .then((r) => (remote = r))
      .catch(() => {});
    const stop = listen<[string, string]>("install-progress", (e) => (stage = e.payload[1]));
    return () => {
      stop.then((f) => f());
    };
  });

  async function start(layout: Layout) {
    error = null;
    starting = true;
    try {
      await play(selected, undefined, layout);
      await onRefresh();
    } catch (e) {
      error = message(e);
      toast(error, true);
    } finally {
      starting = false;
    }
  }
</script>

<section class="home">
  <div class="hero window">
    {#if legend}
      <!-- the profile's box legendary, drawn from the player's ROM -->
      <img class="legend" src={legend} alt={game === "white" ? "Reshiram" : "Zekrom"} />
    {:else}
      <!-- the eidos dragon by night, in Reshiram's colours by day -->
      <div class="art night" style:background-image="url({dragon})"></div>
      <div class="art day" style:background-image="url({dragonDay})"></div>
    {/if}
    <div class="intro">
      <span class="display title">EIDOS</span>
    </div>

    <div class="launch">
      <label class="field profile">
        <span>Profile</span>
        <select value={selected} onchange={(e) => onSelect(e.currentTarget.value)}>
          {#each ov.profiles as p}<option value={p.name}>{p.name}</option>{/each}
        </select>
      </label>
      <div class="which">
        {#if version}
          <span class="badge accent">eidos {versionLabel(version)}</span>
        {:else}
          <span class="badge bad">No version</span>
        {/if}
      </div>
      <div class="plays" class:single={!canOne}>
        {#if canOne}
          <!-- Remaster is the main button; Classic is the DS layout -->
          {#each [["one", "Remaster", "remaster"], ["classic", "Classic", "classic"]] as [layout, label, icon]}
            <button
              class="play"
              class:primary={layout === "one"}
              disabled={!version || running || starting}
              onclick={() => start(layout as Layout)}
            >
              <Icon name={icon} size={24} />
              {label}
            </button>
          {/each}
        {:else}
          <button class="primary play" disabled={!version || running || starting} onclick={() => start("classic")}>
            <Icon name="play" size={20} />
            {running ? "Running" : starting ? "Starting" : "Play"}
          </button>
        {/if}
      </div>
    </div>
  </div>

  {#if update}
    <div class="update window" role="status">
      <Icon name="download" size={20} />
      <div class="what">
        <span class="headline">eidos {update.id ?? update.tag} is available</span>
        <span class="sub">{updating ? stage || "Installing" : `You have ${version ? versionLabel(version) : "no version"}.`}</span>
      </div>
      <button class="primary" disabled={updating || running} onclick={() => installUpdate(update)}>
        {updating ? "Installing" : "Update"}
      </button>
    </div>
  {/if}

  {#if running}
    <p class="note">{selected} is running.</p>
  {:else if !version}
    <p class="note">
      {#if current && current.version !== LATEST && current.version !== LOCAL}
        Version {current.version} is not installed.
      {:else}
        No version installed.
      {/if}
      <button class="quiet outlined" onclick={() => onGoto("versions")}><Icon name="download" size={16} />Install one</button>
    </p>
  {/if}
  {#if error}<p class="note bad" role="alert">{error}</p>{/if}

  <div class="tiles">
    <button class="tile window" onclick={() => onGoto("profiles")}>
      <span class="section-label">Profiles</span>
      <span class="big">{ov.profiles.length}</span>
    </button>
    <button class="tile window" onclick={() => onGoto("versions")}>
      <span class="section-label">Versions</span>
      <span class="big">{ov.installed.length}</span>
      {#if ov.installed[0]}<span class="faint small">Newest: {ov.installed[0]}</span>{/if}
    </button>
  </div>
</section>

<style>
  .home {
    display: flex;
    flex-direction: column;
    gap: 1rem;
    max-width: 58rem;
  }
  .hero {
    position: relative;
    overflow: hidden;
    min-height: 22.5rem;
    display: grid;
    grid-template-rows: 1fr auto;
    background: linear-gradient(160deg, var(--hero-a), var(--hero-b));
  }
  .art {
    position: absolute;
    right: 2rem;
    top: 1.25rem;
    width: 290px;
    height: 238px;
    background-repeat: no-repeat;
    background-size: 290px 238px;
    image-rendering: pixelated;
  }
  .legend {
    position: absolute;
    right: 2rem;
    top: 0.5rem;
    width: 256px;
    height: 256px;
    image-rendering: pixelated;
    pointer-events: none;
  }
  .day,
  :global(:root[data-theme="light"]) .night {
    display: none;
  }
  :global(:root[data-theme="light"]) .day {
    display: block;
  }
  .intro {
    position: relative;
    padding: 2.25rem 2rem 0;
    display: flex;
    flex-direction: column;
    gap: 0.35rem;
  }
  .title {
    font-size: 3.2rem;
    letter-spacing: 0.35em;
    line-height: 1;
  }
  .launch {
    position: relative;
    display: flex;
    align-items: flex-end;
    flex-wrap: wrap;
    gap: 1rem;
    padding: 1.25rem 1.5rem;
    background: var(--panel-2);
    border-top: 2px solid var(--outline);
  }
  /* the row keeps the play buttons beside the profile down to the
   * window's smallest width: the profile and the buttons give up width
   * before anything wraps */
  .profile {
    flex: 0 1 13rem;
    min-width: 8rem;
  }
  .profile select {
    width: 100%;
  }
  .which {
    flex: 1 0 auto;
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
    align-items: flex-start;
    padding-bottom: 0.3rem;
  }
  .plays {
    display: flex;
    gap: 0.75rem;
  }
  .plays .play {
    flex: 1 1 0;
    min-width: 8.5rem;
    justify-content: center;
    font-size: 1.15rem;
    padding: 0.75rem 1.25rem;
  }
  /* the icon's middle on the label's capitals, not on its line box */
  .plays .play :global(svg) {
    position: relative;
    top: 1px;
    flex-shrink: 0;
  }
  .plays.single .play {
    flex: none;
    padding: 0.75rem 2.4rem;
  }
  @media (max-width: 520px) {
    .plays {
      flex-direction: column;
      width: 100%;
    }
  }
  .update {
    display: flex;
    align-items: center;
    gap: 0.9rem;
    padding: 0.8rem 1rem 0.8rem 1.2rem;
    border-color: var(--accent);
  }
  .update .what {
    flex: 1 1 auto;
    display: flex;
    flex-direction: column;
    gap: 0.1rem;
    min-width: 0;
  }
  .update .headline {
    font-weight: 700;
  }
  .update .sub {
    color: var(--muted);
    font-size: 0.9rem;
  }
  .note {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    color: var(--muted);
    margin: 0;
  }
  .tiles {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(14rem, 1fr));
    gap: 1rem;
  }
  .tile {
    flex-direction: column;
    align-items: flex-start;
    gap: 0.15rem;
    padding: 1rem 1.2rem;
    text-align: left;
    font-weight: 400;
  }
  .tile .section-label {
    margin: 0;
  }
  .big {
    font-size: 1.6rem;
    font-weight: 700;
  }
</style>
