// Moving around the launcher without a mouse (#31): with a game controller
// (src-tauri/src/pad.rs sends its presses as `pad` events) or the keyboard.
//
// The arrows and the D-pad move the focus to the nearest control in that
// direction; confirm (A, Enter) presses it; back (B, Escape) closes an open
// dialog, or goes to the first page; prev and next (the shoulder buttons)
// change the page.  Inside an open dialog only its controls are reachable.
//
// One device at a time: some systems send a pad's presses as keys as well
// (Steam's desktop layout turns a Steam Deck's D-pad into the arrow keys), so
// the keys wait while a pad is in use, as the game does
// (engine/platform/platform.c, PAD_OWNS_MS).

import { listen } from "@tauri-apps/api/event";

export type PadAction = "up" | "down" | "left" | "right" | "confirm" | "back" | "prev" | "next";

export interface PadNavHooks {
  /** Change the page by one (-1 the one before). */
  page(step: number): void;
  /** Back with no dialog open. */
  back(): void;
}

const PAD_OWNS_MS = 400;
const FOCUSABLE =
  'button, a[href], input, select, textarea, summary, [tabindex]:not([tabindex="-1"])';

let padUntil = 0;

/** The controls the focus can reach: shown, enabled, in the open dialog if
 * there is one. */
function reachable(): HTMLElement[] {
  const dialogs = Array.from(document.querySelectorAll<HTMLDialogElement>("dialog[open]"));
  const root: ParentNode = dialogs.length > 0 ? dialogs[dialogs.length - 1] : document;
  return Array.from(root.querySelectorAll<HTMLElement>(FOCUSABLE)).filter((el) => {
    if ((el as HTMLButtonElement).disabled || el.closest("[inert]")) return false;
    if (dialogs.length === 0 && el.closest("dialog")) return false;
    const r = el.getBoundingClientRect();
    return r.width > 0 && r.height > 0 && getComputedStyle(el).visibility !== "hidden";
  });
}

function focus(el: HTMLElement) {
  // focusVisible: the ring shows, as for a keyboard (Chromium, WebKit)
  el.focus({ focusVisible: true } as FocusOptions);
  el.scrollIntoView({ block: "nearest", inline: "nearest" });
}

/** The control nearest to the focused one in a direction: the one whose
 * middle is ahead, closest along the direction and least to the side. */
function move(dir: "up" | "down" | "left" | "right") {
  const all = reachable();
  if (all.length === 0) return;
  const cur = document.activeElement as HTMLElement | null;
  if (!cur || !all.includes(cur)) {
    focus(all[0]);
    return;
  }
  const a = cur.getBoundingClientRect();
  const ax = a.left + a.width / 2;
  const ay = a.top + a.height / 2;
  const [dx, dy] = { up: [0, -1], down: [0, 1], left: [-1, 0], right: [1, 0] }[dir];
  let best: HTMLElement | null = null;
  let bestCost = Infinity;
  for (const el of all) {
    if (el === cur) continue;
    const r = el.getBoundingClientRect();
    // the nearest edges along the direction: rows of different widths
    const along =
      dx > 0 ? r.left - a.right : dx < 0 ? a.left - r.right : dy > 0 ? r.top - a.bottom : a.top - r.bottom;
    const mx = r.left + r.width / 2;
    const my = r.top + r.height / 2;
    const ahead = (mx - ax) * dx + (my - ay) * dy;
    if (ahead <= 0 || along < -Math.min(a.width, a.height) / 2) continue;
    const side = dx !== 0 ? Math.abs(my - ay) : Math.abs(mx - ax);
    const cost = Math.max(along, 0) + 2 * side;
    if (cost < bestCost) {
      bestCost = cost;
      best = el;
    }
  }
  if (best) focus(best);
}

function confirm() {
  const cur = document.activeElement as HTMLElement | null;
  if (cur && reachable().includes(cur)) {
    if (cur instanceof HTMLSelectElement) {
      // its list opens; the arrows and confirm then work in it as the
      // system's own list
      try {
        cur.showPicker();
      } catch {
        cur.focus();
      }
      return;
    }
    if (cur instanceof HTMLInputElement && cur.type !== "checkbox" && cur.type !== "radio") {
      cur.focus();
      return;
    }
    cur.click();
  } else {
    move("down");
  }
}

function act(what: PadAction, hooks: PadNavHooks) {
  switch (what) {
    case "up":
    case "down":
    case "left":
    case "right":
      move(what);
      break;
    case "confirm":
      confirm();
      break;
    case "back": {
      const open = document.querySelectorAll<HTMLDialogElement>("dialog[open]");
      if (open.length > 0) open[open.length - 1].close();
      else hooks.back();
      break;
    }
    case "prev":
    case "next":
      if (document.querySelector("dialog[open]") === null) hooks.page(what === "next" ? 1 : -1);
      break;
  }
}

/** An arrow the field the focus is in uses itself: left and right move in
 * a text (and a slider), up and down too in a text of several lines.  A
 * list (select) does not keep the arrows: they move on, and confirm opens
 * it, so a D-pad sent as arrows cannot get stuck in one. */
function fieldKey(e: KeyboardEvent): boolean {
  const t = e.target as HTMLElement | null;
  const sideways = e.key === "ArrowLeft" || e.key === "ArrowRight";
  if (!t) return false;
  if (t instanceof HTMLTextAreaElement || t.isContentEditable) return true;
  if (t instanceof HTMLInputElement) {
    return sideways && t.type !== "checkbox" && t.type !== "radio" && t.type !== "button";
  }
  return false;
}

const KEYS: Record<string, PadAction> = {
  ArrowUp: "up",
  ArrowDown: "down",
  ArrowLeft: "left",
  ArrowRight: "right",
};

/** Starts listening; returns the stop. */
export function startPadNav(hooks: PadNavHooks): () => void {
  const onKey = (e: KeyboardEvent) => {
    const what = KEYS[e.key];
    if (!what || e.altKey || e.ctrlKey || e.metaKey || fieldKey(e)) return;
    e.preventDefault();
    if (performance.now() < padUntil) return;   // the pad's own presses, as keys
    move(what as "up" | "down" | "left" | "right");
  };
  const onEnterEscape = (e: KeyboardEvent) => {
    // Enter and Escape already press and close by themselves; while a pad is
    // in use they are its copies and wait.
    if ((e.key === "Enter" || e.key === "Escape" || e.key === " ") && performance.now() < padUntil) {
      e.preventDefault();
      e.stopPropagation();
    }
  };
  window.addEventListener("keydown", onKey);
  window.addEventListener("keydown", onEnterEscape, true);
  const stop = listen<PadAction>("pad", (e) => {
    padUntil = performance.now() + PAD_OWNS_MS;
    // only the window in front: the game, once started, has the pad
    if (!document.hasFocus()) return;
    act(e.payload, hooks);
  });
  return () => {
    window.removeEventListener("keydown", onKey);
    window.removeEventListener("keydown", onEnterEscape, true);
    stop.then((s) => s());
  };
}
