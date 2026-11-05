<script lang="ts">
  import { MOBILE } from "../lib/platform";
  import { onMount } from "svelte";
  import { listen } from "@tauri-apps/api/event";
  import { toast } from "morphe/menu.svelte";
  import {
    LATEST,
    LOCAL,
    coverArt,
    installVersion,
    saves as listSaves,
    setCurrentSave,
    speciesArt,
    badgeArt,
    playerArt,
    type BadgeArt,
    type Save,
    listRemote,
    knownRemote,
    message,
    openFolder,
    newerThan,
    play,
    romLibrary,
    gamesOf,
    supportsOneScreen,
    versionLabel,
    type Layout,
    type Overview,
    type Profile,
    type Remote,
  } from "../lib/backend";
  import Icon from "../lib/Icon.svelte";
  import { notesHtml, whatsNew } from "../lib/notes";
  import dragon from "../assets/dragon.png";
  import dragonDay from "../assets/dragon-day.png";

  let {
    ov,
    selected,
    current,
    onSelect,
    onRefresh,
    onGoto,
    onNeedRom,
  }: {
    ov: Overview;
    selected: string;
    current: Profile | undefined;
    onSelect: (name: string) => void;
    onRefresh: () => void;
    onGoto: (page: "versions" | "profiles") => void;
    onNeedRom: (game: "black" | "white") => void;
  } = $props();

  let error = $state<string | null>(null);
  let starting = $state(false);

  // The box legendaries, drawn from the player's ROMs: Reshiram by day (on
  // Black's box: a Black ROM in the library), Zekrom by night (on White's
  // box: a White ROM). Without that game's ROM, its theme keeps the eidos
  // dragon.
  let zekrom = $state<string | null>(null);
  let reshiram = $state<string | null>(null);
  onMount(() => {
    coverArt("white")
      .then((url) => (zekrom = url))
      .catch(() => {});
    coverArt("black")
      .then((url) => (reshiram = url))
      .catch(() => {});
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
  let remote = $state<Remote[] | null>(knownRemote());
  // The list could not be had (offline): no notes panel, no skeleton.
  let remoteFailed = $state(false);
  let updating = $state(false);
  let stage = $state("");
  const update = $derived.by(() => {
    if (!remote || !current || current.version !== LATEST) return null;
    const r = remote.find((x) => !x.prerelease && x.available);
    if (!r || r.installed) return null;
    const have = ov.installed[0];
    return !have || newerThan(r.id ?? r.tag, have) ? r : null;
  });

  // The newest release and its notes: what eidos is now, whether this
  // profile runs it yet or not.
  const latest = $derived(remote?.find((r) => !r.prerelease) ?? null);
  const notes = $derived(latest?.notes ? notesHtml(whatsNew(latest.notes)) : "");
  const released = $derived(
    latest?.published
      ? new Date(latest.published).toLocaleDateString(undefined, { year: "numeric", month: "short", day: "numeric" })
      : "",
  );

  async function installUpdate(r: Remote) {
    updating = true;
    try {
      await installVersion(r.tag);
      toast(`eidos ${r.id ?? r.tag} installed.`);
      await onRefresh();
      remote = await listRemote(true);
    } catch (e) {
      toast(message(e), true);
    } finally {
      updating = false;
      stage = "";
    }
  }

  // A first start (no version installed, none built here, the profile
  // following the latest): the latest release installs by itself, once.
  let autoInstalled = false;
  $effect(() => {
    if (autoInstalled || updating || !update || ov.installed.length || ov.localOk) return;
    autoInstalled = true;
    installUpdate(update);
  });

  onMount(() => {
    listRemote()
      .then((r) => (remote = r))
      .catch(() => (remoteFailed = remote === null));
    const stop = listen<[string, string]>("install-progress", (e) => (stage = e.payload[1]));
    return () => {
      stop.then((f) => f());
    };
  });

  // The profile's saves, read from its save files; the party drawn from the
  // player's ROMs. Read again when a game closes (it may have saved).
  let saveList = $state<Save[]>([]);
  // The saves of the game the profile plays: a Black save is not shown
  // while the profile plays White, and the other way round.
  const game = $derived<"black" | "white">(current?.game === "white" ? "white" : "black");
  const shown = $derived(saveList.filter((s) => s.game === game));
  const hidden = $derived(saveList.length - shown.length);
  let sprites = $state<Record<string, string | null>>({});
  const art = (s: Save, species: number) => sprites[`${s.game}:${species}`];
  // The badge case's badges and empty slots, per game.
  let badgeCase = $state<Record<string, BadgeArt | null>>({});
  // Hilbert and Hilda, per game.
  let players = $state<Record<string, string | null>>({});
  const player = (s: Save) => players[`${s.game}:${s.female}`];
  $effect(() => {
    const name = selected;
    void ov.running.length;
    listSaves(name)
      .then((list) => {
        if (name !== selected) return;
        saveList = list;
        for (const s of list) {
          const k = `${s.game}:${s.female}`;
          if (!(k in players)) playerArt(s.game, s.female).then((url) => (players = { ...players, [k]: url }));
        }
        for (const g of new Set(list.map((s) => s.game)))
          if (!(g in badgeCase)) badgeArt(g).then((a) => (badgeCase = { ...badgeCase, [g]: a }));
        for (const s of list)
          for (const m of s.party)
            speciesArt(m.species, s.game).then((url) => (sprites = { ...sprites, [`${s.game}:${m.species}`]: url }));
      })
      .catch(() => (saveList = []));
  });

  // A sprite as large as its frame allows: a whole-number scale (every
  // pixel the same size) when that fills most of the frame, else the
  // exact fit.
  function pixelFit(img: HTMLImageElement) {
    const frame = img.parentElement!;
    const apply = () => {
      const w = img.naturalWidth;
      if (!w) return;
      const room = Math.min(frame.clientWidth, frame.clientHeight);
      const whole = Math.floor(room / w);
      const scale = whole >= 1 && whole * w >= 0.8 * room ? whole : room / w;
      img.style.width = `${w * scale}px`;
    };
    img.addEventListener("load", apply);
    const ro = new ResizeObserver(apply);
    ro.observe(frame);
    apply();
    return {
      destroy() {
        ro.disconnect();
        img.removeEventListener("load", apply);
      },
    };
  }

  async function chooseSave(s: Save) {
    if (s.current) return;
    try {
      await setCurrentSave(selected, s.slot);
      saveList = saveList.map((x) => ({ ...x, current: x.slot === s.slot }));
    } catch (e) {
      toast(message(e), true);
    }
  }

  const playTime = (sec: number) => `${Math.floor(sec / 3600)}:${String(Math.floor(sec / 60) % 60).padStart(2, "0")}`;
  function ago(t: number): string {
    const d = Date.now() / 1000 - t;
    if (d < 3600) return `${Math.max(1, Math.floor(d / 60))} min ago`;
    if (d < 86400) return `${Math.floor(d / 3600)} h ago`;
    if (d < 2 * 86400) return "yesterday";
    if (d < 7 * 86400) return `${Math.floor(d / 86400)} days ago`;
    return new Date(t * 1000).toLocaleDateString(undefined, { month: "short", day: "numeric" });
  }

  async function start(layout: Layout) {
    error = null;
    // no ROM of the profile's game yet: ask for it, rather than an error
    try {
      if (!gamesOf(await romLibrary()).includes(game)) {
        onNeedRom(game);
        return;
      }
    } catch {
      /* the launch below reports what is missing */
    }
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
  <div class="main-col">
  <div class="hero window">
    <div class="stage">
    <div class="intro">
      <span class="display title">EIDOS</span>
    </div>
    <div class="pic">
    <!-- by night Zekrom, by day Reshiram, from the player's ROMs; the eidos
         dragon (and its Reshiram-coloured day form) without them -->
    {#if zekrom}
      <img class="legend night" src={zekrom} alt="Zekrom" />
    {:else}
      <div class="art night" style:background-image="url({dragon})"></div>
    {/if}
    {#if reshiram}
      <img class="legend day" src={reshiram} alt="Reshiram" />
    {:else}
      <div class="art day" style:background-image="url({dragonDay})"></div>
    {/if}
    </div>
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
          {#each MOBILE ? [["one", "Play", "play"]] : [["one", "Remaster", "remaster"], ["classic", "Classic", "classic"]] as [layout, label, icon]}
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

  {#if !latest && !remoteFailed}
    <!-- the first load of the release list: the panel's shape, waiting -->
    <section class="news window" aria-label="Release notes" aria-busy="true">
      <header><span class="skel" style:width="14rem"></span></header>
      <div class="notes">
        <span class="skel" style:width="6rem"></span>
        <span class="skel" style:width="92%"></span>
        <span class="skel" style:width="78%"></span>
        <span class="skel" style:width="85%"></span>
        <span class="skel" style:width="60%"></span>
      </div>
    </section>
  {:else if latest}
    <section class="news window" aria-label="Release notes">
      <header>
        <span class="section-label">What's new in eidos {latest.id ?? latest.tag}</span>
        <span class="faint small">{released}{latest.installed ? " · installed" : ""}</span>
      </header>
      {#if notes}
        <!-- escaped text with simple markup (lib/notes.ts) -->
        <div class="notes">{@html notes}</div>
      {:else}
        <p class="muted small">This release has no notes.</p>
      {/if}
    </section>
  {/if}

  </div>

  <aside class="saves window" aria-label="Saves">
    <header>
      <span class="section-label">Saves in {selected}</span>
      <span class="faint small">{saveList.length} of 8{#if hidden > 0} · {hidden} {game === "black" ? "White" : "Black"} hidden{/if}</span>
    </header>
      <div class="list">
        {#if shown.length === 0}
          <p class="faint small">No saves yet.</p>
        {/if}
        {#each shown as s (s.slot)}
          <button
            class="save"
            class:current={s.current}
            disabled={running}
            onclick={() => chooseSave(s)}
            title={s.current ? "The game opens on this save" : "Open this save next time"}
          >
            <span class="avatar" aria-hidden="true">
              {#if player(s)}<img src={player(s)} alt="" />{/if}
            </span>
            <span class="body">
            <span class="top">
              <span class="who" class:female={s.female}>{s.trainer}</span>
              {#if s.current}<span class="badge accent">Continues here</span>{:else}<span class="faint small">Save {s.slot}</span>{/if}
            </span>
            <span class="party">
              {#each s.party as m, i (i)}
                <span class="frame small-frame" title={m.egg ? "Egg" : `${m.name} · Lv. ${m.level} · ${m.hp}/${m.maxHp} HP`}>
                  {#if m.egg}<span class="egg"></span>{:else if art(s, m.species)}<img use:pixelFit src={art(s, m.species)} alt="" />{/if}
                </span>
              {/each}
            </span>
            <span class="facts">
              <span><b>{playTime(s.seconds)}</b> played</span>
              <span class="faint small">{s.badges} of 8 badges</span>
            </span>
            {#if badgeCase[s.game]}
              {@const b = badgeCase[s.game]!}
              <span class="case" aria-hidden="true">
                {#each b.badges as src, i (i)}
                  <img src={(s.badgeBits >> i) & 1 ? src : b.outlines[i]} alt="" />
                {/each}
              </span>
            {:else}
              <span class="dots" aria-hidden="true">
                {#each Array(8) as _, i}<i class:on={((s.badgeBits >> i) & 1) === 1}></i>{/each}
              </span>
            {/if}
            <span class="where faint small">{s.place || "Unknown place"} · {ago(s.saved)}</span>
            </span>
          </button>
        {/each}
      </div>
    <footer>
      <span class="faint small hint">New Game in the title menu takes a free slot.</span>
      {#if !MOBILE}<button class="quiet outlined" onclick={() => openFolder("saves", selected)}><Icon name="folder" size={15} />Open saves folder</button>{/if}
    </footer>
  </aside>
</section>

<style>
  /* The window never scrolls. The banner keeps one shape (9:4) and its
     title one proportion at every size; the saves run down the side, their
     list scrolling inside the panel (under the banner only in the
     narrowest windows). */
  .home {
    display: flex;
    flex-direction: column;
    gap: 1rem;
    height: 100%;
    min-height: 0;
  }
  @media (min-width: 800px) {
    .home {
      display: grid;
      grid-template-columns: minmax(0, 1.7fr) minmax(16rem, 1fr);
      align-items: stretch;
    }
    .main-col {
      min-height: 0;
    }
  }
  /* The release notes take what the column has left under the banner and
     scroll inside their panel; a narrow or very short window leaves them
     out. */
  .news {
    display: none;
  }
  @media (min-width: 800px) and (min-height: 540px) {
    .news {
      flex: 1 1 auto;
      min-height: 0;
      display: flex;
      flex-direction: column;
      gap: 0.6rem;
      padding: 1rem 1.2rem;
    }
  }
  .news header {
    display: flex;
    justify-content: space-between;
    align-items: baseline;
    gap: 1rem;
  }
  .news header .section-label {
    margin: 0;
  }
  .skel {
    display: block;
    height: 0.8rem;
    margin: 0.35rem 0 0.6rem;
    border-radius: 0.4rem;
    background: linear-gradient(90deg, var(--panel-2), var(--panel-3), var(--panel-2));
    background-size: 200% 100%;
    animation: skel 1.4s ease-in-out infinite;
  }
  @keyframes skel {
    from {
      background-position: 100% 0;
    }
    to {
      background-position: -100% 0;
    }
  }
  @media (prefers-reduced-motion: reduce) {
    .skel {
      animation: none;
    }
  }
  .notes {
    min-height: 0;
    overflow-y: auto;
    line-height: 1.5;
  }
  .notes :global(h4) {
    margin: 0.4rem 0 0.3rem;
    font-size: 0.95rem;
  }
  .notes :global(h4:first-child) {
    margin-top: 0;
  }
  .notes :global(ul) {
    margin: 0 0 0.6rem;
    padding-left: 1.2rem;
  }
  .notes :global(li) {
    margin: 0.2rem 0;
  }
  .notes :global(p) {
    margin: 0 0 0.6rem;
  }
  .notes :global(code) {
    font-size: 0.9em;
  }
  .main-col {
    display: flex;
    flex-direction: column;
    gap: 1rem;
    min-width: 0;
    flex: none;
  }
  .main-col .hero {
    flex: none;
  }
  .saves {
    min-height: 6rem;
  }
  /* the smallest windows: the hint gives way, the button stays */
  @media (max-height: 600px) {
    .saves footer .hint {
      display: none;
    }
    .saves footer {
      padding-top: 0.5rem;
    }
    .saves {
      padding: 0.75rem 1rem;
      gap: 0.5rem;
    }
  }
  .saves header {
    display: flex;
    justify-content: space-between;
    align-items: baseline;
  }
  .saves header .section-label {
    margin: 0;
  }
  .frame {
    aspect-ratio: 1;
    display: grid;
    place-items: center;
    overflow: hidden;
    border-radius: var(--radius-s);
    background: var(--panel-2);
    border: 2px solid var(--panel-3);
  }
  .frame img {
    display: block;
    height: auto;
    image-rendering: pixelated;
  }
  .egg {
    width: 38%;
    aspect-ratio: 4 / 5;
    border-radius: 50% 50% 46% 46% / 58% 58% 42% 42%;
    background: #f1ead2;
    box-shadow: inset -3px -4px 0 rgba(0, 0, 0, 0.12), 0 0 0 2px var(--outline);
  }
  .saves {
    flex: 1 1 auto;
    display: flex;
    flex-direction: column;
    gap: 0.75rem;
    padding: 1rem 1.1rem;
    min-width: 0;
    min-height: 0;
  }
  .list {
    flex: 1 1 auto;
    min-height: 0;
    overflow-y: auto;
    overflow-x: hidden;
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(min(100%, 17rem), 1fr));
    align-content: start;
    gap: 0.6rem;
  }
  .saves footer {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    justify-content: space-between;
    gap: 0.5rem;
    margin-top: auto;
    padding-top: 0.75rem;
    border-top: 2px solid var(--panel-3);
  }
  /* the player's picture down the left, the save's facts beside it */
  .save {
    display: grid;
    grid-template-columns: 3.6rem minmax(0, 1fr);
    align-items: stretch;
    gap: 0.75rem;
    padding: 0.7rem 0.9rem 0.7rem 0.6rem;
    text-align: left;
    font-weight: 400;
    background: var(--panel-2);
    min-width: 0;
    flex: none;
  }
  .save .body {
    display: flex;
    flex-direction: column;
    gap: 0.45rem;
    min-width: 0;
  }
  .avatar {
    display: flex;
    align-items: flex-end;
    justify-content: center;
  }
  .avatar img {
    width: 100%;
    max-height: 7rem;
    object-fit: contain;
    object-position: bottom;
    image-rendering: pixelated;
  }
  .save.current {
    border-color: var(--accent);
    box-shadow: inset 0 0 0 1px var(--accent);
  }
  .save .top {
    display: flex;
    flex-wrap: wrap;
    justify-content: space-between;
    align-items: center;
    gap: 0.25rem 0.5rem;
  }
  .who {
    font-weight: 700;
    font-size: 1.05rem;
    color: var(--male);
  }
  .who.female {
    color: var(--female);
  }
  .party {
    display: grid;
    grid-template-columns: repeat(6, minmax(0, 3.25rem));
    gap: 0.3rem;
  }
  .party:empty {
    display: none;
  }
  .small-frame {
    border-width: 1px;
  }
  .facts {
    display: flex;
    flex-wrap: wrap;
    justify-content: space-between;
    align-items: center;
    gap: 0.5rem;
  }
  /* the badge case's own pictures: a badge where it is won, its empty slot
     where not */
  .case {
    display: grid;
    grid-template-columns: repeat(8, minmax(0, 1fr));
    justify-items: center;
  }
  .case img {
    width: 100%;
    max-width: 1.1rem;
    height: auto;
    aspect-ratio: 1 / 2;
    image-rendering: pixelated;
  }
  .dots {
    display: flex;
    gap: 3px;
  }
  .dots i {
    width: 9px;
    height: 9px;
    border-radius: 50%;
    background: var(--panel-3);
    box-shadow: 0 0 0 1px var(--outline);
  }
  .dots i.on {
    background: var(--gold);
  }
  /* the trainer's colour: the game's blue and red for the boy and the
     girl, in both themes (the light theme's accent is red) */
  .home {
    --male: #3d8ee6;
    --female: #e0503f;
  }
  .hero {
    position: relative;
    overflow: hidden;
    container-type: inline-size;
    display: grid;
    grid-template-rows: auto auto;
    background: linear-gradient(160deg, var(--hero-a), var(--hero-b));
  }
  /* The art's own area above the launch row: the legendary (or the
     dragon) fills its height on the right at every window size, never
     cut. */
  /* the art area keeps one shape (3:1) at every width; the launch row
     sits under it */
  .stage {
    display: flex;
    aspect-ratio: 3 / 1;
    min-height: 0;
  }
  .pic {
    position: relative;
    flex: 1 1 auto;
    min-width: 0;
  }
  .art,
  .legend {
    position: absolute;
    top: 0.75rem;
    right: 1.5rem;
    height: calc(100% - 1.5rem);
    max-width: calc(100% - 1.5rem);
    image-rendering: pixelated;
    pointer-events: none;
  }
  .legend {
    width: auto;
    object-fit: contain;
    object-position: right center;
  }
  .art {
    width: calc(100% - 1.5rem);
    background-repeat: no-repeat;
    background-size: contain;
    background-position: right center;
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
    flex: none;
    padding: 4.5cqw 4cqw 0;
    display: flex;
    flex-direction: column;
    gap: 0.35rem;
  }
  .title {
    font-size: 5.2cqw;
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
    flex: 1 1 9rem;
    max-width: 14rem;
    min-width: 7rem;
  }
  .profile select {
    width: 100%;
  }
  .which {
    flex: none;
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
    align-items: flex-start;
    padding-bottom: 0.3rem;
  }
  .plays {
    display: flex;
    gap: 0.75rem;
    margin-left: auto;
  }
  .plays .play {
    flex: 1 1 0;
    min-width: 7.5rem;
    justify-content: center;
    font-size: 1.1rem;
    padding: 0.7rem 1.1rem;
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
</style>
