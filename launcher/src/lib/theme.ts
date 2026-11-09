// The launcher's theme: morphe's choices (night, day, the system's) plus
// "game", where the look follows the selected profile's game (Black by
// day, White by night).  "game" is the default; any other choice stays
// whichever game is selected (#29).
import { THEMES, savedTheme, setTheme, type Theme } from "morphe/theme";

export type LauncherTheme = Theme | "game";

const FOLLOW_KEY = "eidos-launcher.theme-follows-game";

let gameTheme: "light" | "dark" = "light";

function followsGame(): boolean {
  try {
    return localStorage.getItem(FOLLOW_KEY) !== "0";
  } catch {
    return true;
  }
}

export function launcherTheme(): LauncherTheme {
  return followsGame() ? "game" : savedTheme();
}

/// The selected profile's game changed: its theme applies while "game" is chosen.
export function setGameTheme(t: "light" | "dark") {
  gameTheme = t;
  // Saved as morphe's own choice too, so the next start opens in it and
  // morphe's "follow system" listener stays quiet.
  if (followsGame()) setTheme(t);
}

export function setLauncherTheme(t: LauncherTheme) {
  try {
    localStorage.setItem(FOLLOW_KEY, t === "game" ? "1" : "0");
  } catch {
    // Not saved; it lasts for this session.
  }
  setTheme(t === "game" ? gameTheme : t);
}

export const LAUNCHER_THEMES: [LauncherTheme, string][] = [["game", "Follow the game"], ...THEMES];
