// The saved theme's background before the styles load, so the window does
// not flash white (loaded from index.html before anything else; a file, as
// the launcher's content security policy allows no inline script).
try {
  var t = localStorage.getItem("eidos-launcher.theme");
  if (t !== "light" && t !== "dark") t = matchMedia("(prefers-color-scheme: light)").matches ? "light" : "dark";
  document.documentElement.dataset.theme = t;
  document.documentElement.style.background = t === "light" ? "#eeeef5" : "#121419";
} catch (e) {
  // storage blocked: the styles' own background, a moment later
}
