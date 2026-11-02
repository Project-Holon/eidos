// The phone app: the launcher and the game are one app there
// (docs/distribution.md, "Phones"). It plays one-screen only, has one eidos
// version (the app's own) and no folders to open. The browser preview takes
// ?mobile to show it.
export const MOBILE: boolean =
  /Android|iPhone|iPad|iPod/.test(navigator.userAgent) || new URLSearchParams(location.search).has("mobile");
