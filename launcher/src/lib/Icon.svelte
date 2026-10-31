<script lang="ts">
  // Line icons, 24 x 24 on a 2 px stroke, in the text colour.
  let { name, size = 18 }: { name: string; size?: number } = $props();

  const PATHS: Record<string, string> = {
    play: "M7 4.5v15l12-7.5z",
    profiles: "M16 20v-1.5a4 4 0 0 0-4-4H6a4 4 0 0 0-4 4V20M9 11a3.5 3.5 0 1 0 0-7 3.5 3.5 0 0 0 0 7M22 20v-1.5a4 4 0 0 0-3-3.85M16 4.15a3.5 3.5 0 0 1 0 6.7",
    versions: "M12 3 2 8l10 5 10-5zM2 13l10 5 10-5M2 18l10 5 10-5",
    mods: "M20 13.5h-1.5a1.5 1.5 0 0 0-1.5 1.5v1.5a2 2 0 0 1-4 0V15a1.5 1.5 0 0 0-1.5-1.5H10a2 2 0 0 1 0-4h1.5A1.5 1.5 0 0 0 13 8V6.5a2 2 0 0 1 4 0V8a1.5 1.5 0 0 0 1.5 1.5H20M4 4h5v5H4zM4 15h5v5H4z",
    settings: "M12 15a3 3 0 1 0 0-6 3 3 0 0 0 0 6M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06A1.65 1.65 0 0 0 4.68 15a1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06A1.65 1.65 0 0 0 9 4.68a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06A1.65 1.65 0 0 0 19.4 9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1",
    folder: "M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z",
    copy: "M9 9h11v11H9zM5 15H4V4h11v1",
    trash: "M4 7h16M10 11v6M14 11v6M6 7l1 13h10l1-13M9 7V4h6v3",
    refresh: "M20 11a8 8 0 0 0-14.3-4.9L4 8M4 4v4h4M4 13a8 8 0 0 0 14.3 4.9L20 16M20 20v-4h-4",
    download: "M12 3v12M7 10l5 5 5-5M5 21h14",
    plus: "M12 5v14M5 12h14",
    check: "M5 12.5l4.5 4.5L19 7.5",
    edit: "M4 20h4L19 9l-4-4L4 16zM14 6l4 4",
    link: "M10 14a4 4 0 0 0 5.66 0l3-3a4 4 0 0 0-5.66-5.66l-1 1M14 10a4 4 0 0 0-5.66 0l-3 3a4 4 0 0 0 5.66 5.66l1-1",
    upload: "M12 21V9M7 14l5-5 5 5M5 3h14",
    // a character and a letter, the usual sign for languages
    translate: "M3 5.5h9M7.5 3.5v2M10 5.5c-.8 4-3.2 7.4-6.5 9.5M5.3 9c1.2 2.4 3.2 4.3 5.7 5.3M12.5 20.5l4-10 4 10M14 17h5",
    chevron: "M7 10l5 5 5-5",
  };

  // Icons drawn on their own grid: a 1.5 px line, solid screens 1.5 px in
  // from each frame, 2.25 px outer corners.
  const ART: Record<string, string> = {
    // an open DS: two equal shells, a screen in each, the D-pad and buttons
    classic: `
      <rect x="3.75" y="1.25" width="16.5" height="9.25" rx="2.25"/>
      <rect x="7.5" y="3.5" width="9" height="4.75" rx=".5" fill="currentColor" stroke="none"/>
      <rect x="3.75" y="13.5" width="16.5" height="9.25" rx="2.25"/>
      <rect x="7.5" y="15.75" width="9" height="4.75" rx=".5" fill="currentColor" stroke="none"/>
      <circle cx="6" cy="18.125" r=".75" fill="currentColor" stroke="none"/>
      <circle cx="18" cy="18.125" r=".75" fill="currentColor" stroke="none"/>`,
    // a wide monitor, a spark at its corner
    remaster: `<g transform="translate(0 1)">
      <path d="M16.25 4.75H4.5a2.25 2.25 0 0 0-2.25 2.25v8.5a2.25 2.25 0 0 0 2.25 2.25h15a2.25 2.25 0 0 0 2.25-2.25V8.25"/>
      <rect x="4.5" y="7" width="15" height="8.5" rx=".5" fill="currentColor" stroke="none"/>
      <path d="M12 17.75v3.25M8.5 21h7"/>
      <path d="M20.25 .75l.9 1.6 1.6.9-1.6.9-.9 1.6-.9-1.6-1.6-.9 1.6-.9z" fill="currentColor" stroke="none"/></g>`,
  };
</script>

<svg
  width={size}
  height={size}
  viewBox="0 0 24 24"
  fill={name === "play" ? "currentColor" : "none"}
  stroke="currentColor"
  stroke-width="2"
  stroke-linecap="round"
  stroke-linejoin="round"
  aria-hidden="true"
  >{#if ART[name]}<g stroke-width="1.5">{@html ART[name]}</g>{:else}<path
      d={PATHS[name] ?? ""}
    />{/if}</svg
>
