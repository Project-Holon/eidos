// The launcher's own updates (docs/launcher.md, "Updates").
//
// On launch the launcher asks the newest release's latest.json whether a
// newer launcher exists, downloads it, checks its signature and installs it,
// then starts again.  Every step can fail (no network, GitHub down, a bad
// download, an installer that does not run): a failure is reported in one
// line and the launcher goes on as it is.  A version that failed twice is
// not tried again on launch for a day, so a broken release cannot trap the
// launcher in a loop; the Settings page can still try it by hand.

import { invoke } from "@tauri-apps/api/core";
import { check, type Update } from "@tauri-apps/plugin-updater";
import { relaunch } from "@tauri-apps/plugin-process";

const AUTO_KEY = "eidos-launcher.auto-update";
const FAIL_KEY = "eidos-launcher.update-failures";
// GitHub's download host can take 20 s or more to answer a first request;
// the check runs in the background, so a long wait blocks nothing.
const CHECK_TIMEOUT_MS = 60000;
const RETRY_AFTER_MS = 24 * 60 * 60 * 1000;

export type UpdateState =
  | { kind: "idle" }
  | { kind: "checking" }
  | { kind: "current" }
  | { kind: "downloading"; version: string; done: number; total: number | null }
  | { kind: "installing"; version: string }
  | { kind: "failed"; message: string };

export const updates = $state<{ state: UpdateState; overlay: boolean }>({
  state: { kind: "idle" },
  overlay: false,
});

type Failures = { version: string; count: number; at: number };

function readFailures(): Failures | null {
  try {
    return JSON.parse(localStorage.getItem(FAIL_KEY) ?? "null");
  } catch {
    return null;
  }
}

function noteFailure(version: string) {
  const f = readFailures();
  const count = f && f.version === version ? f.count + 1 : 1;
  try {
    localStorage.setItem(FAIL_KEY, JSON.stringify({ version, count, at: Date.now() }));
  } catch {
    /* without storage every launch tries again */
  }
}

function clearFailures() {
  try {
    localStorage.removeItem(FAIL_KEY);
  } catch {
    /* nothing to clear */
  }
}

/// The automatic-update choice, kept in the launcher's settings file so every
/// build reads the same one.  An "off" left in the web view's storage by an
/// older launcher is carried over once.
export async function autoUpdate(): Promise<boolean> {
  try {
    if (localStorage.getItem(AUTO_KEY) === "off") {
      localStorage.removeItem(AUTO_KEY);
      await setAutoUpdate(false);
      return false;
    }
  } catch {
    /* no storage: nothing to carry over */
  }
  try {
    return await invoke<boolean>("get_auto_update");
  } catch {
    return true;
  }
}

export async function setAutoUpdate(on: boolean): Promise<void> {
  await invoke<void>("set_auto_update", { on });
}

/// Whether test builds (pre-releases) are listed with the releases.
export async function preReleases(): Promise<boolean> {
  try {
    return await invoke<boolean>("get_pre_releases");
  } catch {
    return false;
  }
}

export async function setPreReleases(on: boolean): Promise<void> {
  await invoke<void>("set_pre_releases", { on });
}

function withTimeout<T>(p: Promise<T>, ms: number): Promise<T> {
  return new Promise((resolve, reject) => {
    const t = setTimeout(() => reject(new Error("no answer from the update server")), ms);
    p.then(
      (v) => (clearTimeout(t), resolve(v)),
      (e) => (clearTimeout(t), reject(e)),
    );
  });
}

const text = (e: unknown) => (e instanceof Error ? e.message : String(e));

async function install(update: Update) {
  updates.overlay = true;
  updates.state = { kind: "downloading", version: update.version, done: 0, total: null };
  try {
    await update.downloadAndInstall((ev) => {
      const s = updates.state;
      if (s.kind !== "downloading") return;
      if (ev.event === "Started") s.total = ev.data.contentLength ?? null;
      else if (ev.event === "Progress") s.done += ev.data.chunkLength;
      else if (ev.event === "Finished") updates.state = { kind: "installing", version: update.version };
    });
    clearFailures();
    await relaunch();
  } catch (e) {
    noteFailure(update.version);
    updates.overlay = false;
    updates.state = { kind: "failed", message: `The update to ${update.version} failed: ${text(e)}` };
  }
}

/// Checks for a newer launcher and installs it.  `manual` is the Settings
/// button: it ignores the automatic setting and the failure pause.
export async function checkForUpdate(manual = false): Promise<void> {
  if (!manual) {
    // A development build (`npm run tauri dev`) never updates itself on its
    // own: it would replace the installed launcher with the release.
    if (import.meta.env.DEV) return;
    if (!(await autoUpdate())) return;
  }
  updates.state = { kind: "checking" };
  let update: Update | null;
  try {
    update = await withTimeout(check(), CHECK_TIMEOUT_MS);
  } catch (e) {
    updates.state = { kind: "failed", message: `Could not check for updates: ${text(e)}` };
    return;
  }
  if (!update) {
    updates.state = { kind: "current" };
    return;
  }
  const f = readFailures();
  if (!manual && f && f.version === update.version && f.count >= 2 && Date.now() - f.at < RETRY_AFTER_MS) {
    updates.state = { kind: "failed", message: `The update to ${update.version} failed twice; try it from Settings.` };
    return;
  }
  await install(update);
}
