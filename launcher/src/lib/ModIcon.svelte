<script lang="ts">
  import { modIconArt, type ModIcon } from "./backend";
  import Icon from "./Icon.svelte";

  // A mod's icon in a square tile; the puzzle glyph when it has none or no
  // ROM can draw it.
  let { icon, game }: { icon: ModIcon | null; game: string } = $props();

  let url = $state<string | null>(null);
  $effect(() => {
    let live = true;
    url = null;
    modIconArt(icon, game).then((u) => {
      if (live) url = u;
    });
    return () => {
      live = false;
    };
  });
</script>

<span class="mod-icon" aria-hidden="true">
  {#if url}<img src={url} alt="" />{:else}<Icon name="mods" size={18} />{/if}
</span>

<style>
  .mod-icon {
    flex: none;
    width: 2.5rem;
    height: 2.5rem;
    display: grid;
    place-items: center;
    border-radius: 8px;
    background: var(--panel-3);
    border: 1px solid var(--line);
    color: var(--muted);
  }
  img {
    width: 2rem;
    height: 2rem;
    object-fit: contain;
    image-rendering: pixelated;
  }
</style>
