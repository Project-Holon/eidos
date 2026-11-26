import { mount } from "svelte";
import App from "./App.svelte";
import "morphe/theme.css";
import "@fontsource/marcellus-sc/400.css";
import "./lib/launcher.css";
import { initTheme, themeStorageKey } from "morphe/theme";
import { installMenuFallback } from "morphe/menu.svelte";

themeStorageKey("eidos-launcher.theme");
initTheme();
if (import.meta.env.DEV) {
  const t = new URLSearchParams(location.search).get("theme");
  if (t === "light" || t === "dark") document.documentElement.dataset.theme = t;
}
installMenuFallback();

// The launcher keeps its 1280x720 proportions at every window size, as the
// game's interface does: the whole page (text, cards, gaps, sprites) is
// zoomed by the window's size over 1280x720, in eighths so 1920x1080 is
// exactly 1.5 and 2560x1440 exactly 2.  Never below 1: a smaller window or
// a phone keeps the page's own narrow layouts.
function applyScale() {
  const s = Math.min(innerWidth / 1280, innerHeight / 720);
  const z = s <= 1 ? 1 : Math.floor(s * 8) / 8;
  document.documentElement.style.setProperty("zoom", String(z));
  document.documentElement.style.setProperty("--scale", String(z));
}
applyScale();
addEventListener("resize", applyScale);

const target = document.getElementById("app");
if (!target) throw new Error("no #app element");

// The development server outside Tauri draws the pages over sample data
// (lib/mock.ts); the built app never loads it.
const ready =
  import.meta.env.DEV && !("__TAURI_INTERNALS__" in window)
    ? import("./lib/mock").then((m) => m.installMock())
    : Promise.resolve();

ready.then(() => mount(App, { target }));
