//! Game controllers in the launcher (#31): the pads are read here (gilrs:
//! evdev on Linux, XInput and Windows.Gaming.Input on Windows, IOKit on
//! macOS), because a web view's own gamepad support differs from one system
//! to the next.  Each press goes to the page as a `pad` event naming what it
//! does: up, down, left, right, confirm, back, prev, next.  The page moves
//! its focus with them (src/lib/padnav.ts).
//!
//! The directions repeat while held, as a list does on a console: the first
//! repeat after REPEAT_WAIT, then one every REPEAT_STEP.

use std::thread;
use std::time::{Duration, Instant};

use gilrs::{Axis, Button, EventType, Gilrs};
use tauri::{AppHandle, Emitter};

const REPEAT_WAIT: Duration = Duration::from_millis(400);
const REPEAT_STEP: Duration = Duration::from_millis(110);
const POLL: Duration = Duration::from_millis(16);
/// How far a stick goes before it counts as a direction, and how far back
/// it comes before it counts as let go.
const STICK_ON: f32 = 0.6;
const STICK_OFF: f32 = 0.35;

#[derive(Clone, Copy, PartialEq, Eq)]
enum Dir {
    Up,
    Down,
    Left,
    Right,
}

impl Dir {
    fn name(self) -> &'static str {
        match self {
            Dir::Up => "up",
            Dir::Down => "down",
            Dir::Left => "left",
            Dir::Right => "right",
        }
    }
}

/// What a button does in the launcher.  The face buttons go by place: the
/// bottom one confirms and the right one goes back, as Steam's and Xbox's
/// interfaces have it.
fn action(b: Button) -> Option<&'static str> {
    Some(match b {
        Button::South | Button::Start => "confirm",
        Button::East | Button::Select => "back",
        Button::LeftTrigger => "prev",
        Button::RightTrigger => "next",
        _ => return None,
    })
}

fn dpad(b: Button) -> Option<Dir> {
    Some(match b {
        Button::DPadUp => Dir::Up,
        Button::DPadDown => Dir::Down,
        Button::DPadLeft => Dir::Left,
        Button::DPadRight => Dir::Right,
        _ => return None,
    })
}

/// The direction the left stick points at, with some give so it does not
/// flicker between two.
fn stick(x: f32, y: f32, held: Option<Dir>) -> Option<Dir> {
    let (ax, ay) = (x.abs(), y.abs());
    let limit = if held.is_some() { STICK_OFF } else { STICK_ON };
    if ax.max(ay) < limit {
        return None;
    }
    Some(if ax > ay {
        if x > 0.0 { Dir::Right } else { Dir::Left }
    } else if y > 0.0 {
        Dir::Up
    } else {
        Dir::Down
    })
}

/// Reads the pads for as long as the launcher runs.  No pad support on this
/// system (gilrs cannot start) leaves the launcher as it was.
pub fn start(app: AppHandle) {
    thread::spawn(move || {
        let Ok(mut gilrs) = Gilrs::new() else {
            return;
        };
        let send = |what: &str| {
            let _ = app.emit("pad", what);
        };
        // the direction held (D-pad or stick), when it began, the last repeat
        let mut held: Option<(Dir, Instant, Instant)> = None;
        let (mut sx, mut sy) = (0.0f32, 0.0f32);
        let mut stick_dir: Option<Dir> = None;
        loop {
            let mut pressed: Option<Dir> = None;
            let mut released = false;
            while let Some(ev) = gilrs.next_event() {
                match ev.event {
                    EventType::ButtonPressed(b, _) => {
                        if let Some(d) = dpad(b) {
                            pressed = Some(d);
                        } else if let Some(a) = action(b) {
                            send(a);
                        }
                    }
                    EventType::ButtonReleased(b, _) => {
                        if dpad(b).is_some() {
                            released = true;
                        }
                    }
                    EventType::AxisChanged(Axis::LeftStickX, v, _) => sx = v,
                    EventType::AxisChanged(Axis::LeftStickY, v, _) => sy = v,
                    EventType::Disconnected => released = true,
                    _ => {}
                }
            }
            let now = Instant::now();
            let s = stick(sx, sy, stick_dir);
            if s != stick_dir {
                stick_dir = s;
                match s {
                    Some(d) => pressed = Some(d),
                    None => released = true,
                }
            }
            if let Some(d) = pressed {
                send(d.name());
                held = Some((d, now, now));
            } else if released && stick_dir.is_none() {
                held = None;
            } else if let Some((d, began, last)) = held {
                if now.duration_since(began) >= REPEAT_WAIT && now.duration_since(last) >= REPEAT_STEP {
                    send(d.name());
                    held = Some((d, began, now));
                }
            }
            thread::sleep(POLL);
        }
    });
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn stick_has_give() {
        assert!(stick(0.5, 0.0, None).is_none());
        assert!(stick(0.7, 0.1, None) == Some(Dir::Right));
        // held: it stays a direction until it comes back past STICK_OFF
        assert!(stick(0.5, 0.0, Some(Dir::Right)) == Some(Dir::Right));
        assert!(stick(0.3, 0.0, Some(Dir::Right)).is_none());
        assert!(stick(0.0, 0.9, None) == Some(Dir::Up));
        assert!(stick(0.0, -0.9, None) == Some(Dir::Down));
    }

    #[test]
    fn face_buttons_by_place() {
        assert_eq!(action(Button::South), Some("confirm"));
        assert_eq!(action(Button::East), Some("back"));
        assert_eq!(action(Button::LeftTrigger), Some("prev"));
        assert_eq!(action(Button::North), None);
    }
}
