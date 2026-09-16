# Mouse-only control — design

Date: 2026-09-16
Status: approved

## Goal

Make Test Drive Enhanced playable with the mouse alone: drive, navigate menus, shift gears,
toggle pause/sound, and enter a high-score name. The cursor stays visible and free — no relative
mouse mode, no grab, no capture, consistent with the earlier "cursor not locked" requirement.

Mouse is *additive*: keyboard and gamepad keep working exactly as they do, and every existing path
falls through to them when the mouse is idle. No CLI flag; mouse support is always present.

## Findings (measured, not assumed)

- **No mouse code exists.** `rg -i mouse src/` returns nothing; `host.c` handles only keyboard,
  gamepad and window events (`src/host.c:418`).
- **There is already an input seam.** `host_joy_read()` (`src/host.c:452`) feeds `joy_read()`
  (`src/platform/input.c:35`), which returns a direction bitfield (1 up, 2 down, 4 right, 8 left,
  0x10 A, 0x20 B). Both menu dispatch (`getkey_kbd_joy_edge`, `src/platform/input.c:118`) and
  driving (`input_poll_drive`, `src/platform/input.c:228`) consume it.
- **Driving input word format** (`sim_tick`, `src/game/sim.c:123`): low nibble = direction code
  1–8; bit `0x10` = fire. Fire clear → throttle from `DS_throttle_dir_table` and steering from
  `DS_steer_dir_table` (`src/game/sim.c:219`, `243`). Fire set → gear shift from
  `DS_gear_delta_table`. All three tables live in the game data segment; the port reads them as-is.
- **Gearbox / gate toggles** are character keys `d`/`D` (gear-box display) and `o`/`O` (gate-shift
  mode), decoded in `sim_input_charkey` (`src/game/sim.c:143`). The gate path is only taken when
  `DS_joy_enabled == 1` (`src/game/sim.c:229`); the delta-shift path is always available.
- **Menu Enter** already maps a joystick button to `0x000D` (`src/platform/input.c:125`); menu
  directions go through `DS_joy_menu_scan`, deduped with `DS_joy_menu_last`.
- **Name entry** is `text_input_line` (`src/platform/input.c:354`), called only from
  `scores_enter_name` (`src/game/flow_scores.c:82`) with `(name, 15, 0xB8, 0xB4, 3000)`. It needs
  letters, Enter, Left/Right, Insert, Delete and Backspace.
- **Overlay slot is taken.** `enh_init` installs the single `gfx_set_overlay` slot
  (`src/enhanced/enhanced.c:1189`) for the road view, active only inside `run_stage`. Name entry
  runs outside `run_stage`, so it must not reuse that slot.

## Non-goals

- No change to the faithful core's behaviour, timing, arithmetic or memory model. Additions are
  `PORT:`-marked deviations, like the existing notes in `platform/input.c`.
- No relative mouse mode, no cursor grab, no cursor hiding.
- No new dependencies and no new source files: mouse support lives in the two layers that already
  own input.
- No mouse binding for the `d` (gear-box display) and `o` (gate-shift mode) toggles. Gear shifting
  itself is mouse-driven through the always-available delta path (`src/game/sim.c:243`); the two
  toggles stay on the keyboard. Pause matters more than either cosmetic toggle and middle click is
  spent on it (see the resolution note below).

## Changes

### A — `host.c` / `host.h`: raw mouse state

Follow the existing host pattern (`host_kbd_*`, `host_joy_read`): `host.c` owns SDL and exposes
plain state; it contains no control policy.

```c
/* Queued mouse state since the last call: horizontal motion (dx) accumulates and is consumed on
 * read; held buttons as bit0 left, bit1 right, bit2 middle, bit3 X1, bit4 X2; wheel steps
 * (+1 up, -1 down) accumulate. */
void host_mouse_read(s16 *dx, u8 *held, s16 *wheel);
bool host_mouse_click(s16 *ex, s16 *ey, u8 *button);   /* false when no queued press */
```

- Motion deltas accumulate in `process_events` and are zeroed by `host_mouse_read`, so nothing is
  lost between polls and repeated reads within one tick do not double-count.
- Click coordinates are converted with `SDL_ConvertEventToRenderCoordinates` and then mapped from
  render-logical space to EGA space using the `frame_w`/`frame_h` already stored by
  `host_set_frame_source`, so `--res-scale` and window resizing need no special handling.
- `SDL_EVENT_WINDOW_MOUSE_LEAVE` and window focus loss clear the held-button state, so accelerate
  cannot stick when the pointer leaves the window mid-press.

### B — `platform/input.c`: control policy

All interpretation lives here (the module that already decides what key means what). `input.c`
never includes SDL.

Steering is relative with auto-centre, using the host clock for decay so it behaves the same in
driving and menus without being tied to the 100 Hz tick:

```c
off += dx;                                            /* host_mouse_read */
off  = clamp_s16(off, -MOUSE_OFF_MAX, MOUSE_OFF_MAX);
u32 dt_ms = elapsed since last update;
off -= sign(off) * min(abs(off), MOUSE_DECAY_PER_MS * dt_ms);
steer = abs(off) > MOUSE_OFF_THRESH ? sign(off) : 0;
```

Then throttle and steer combine into the original's 1–8 direction code using the same truth table
`held_controls()` already implements (`src/platform/input.c:207`). Constants are file-local with a
comment naming the trade-off: a held steering angle cannot be parked, it springs back when the
mouse is idle; `MOUSE_DECAY_PER_MS` is the single feel knob.

Drive mapping, added to `input_poll_drive` after the keyboard handling and before the joystick
fallback (`src/platform/input.c:244`):

| Mouse | Word |
|---|---|
| left held | direction up, fire clear (accelerate) |
| right held | direction down, fire clear (brake) |
| steer offset | left / right combined into the direction code |
| wheel up / down | fire + up / fire + down, latched for a fixed few polls (one gear per step) |

The wheel latch holds bit `0x10` for a small fixed number of driving polls (order of 30 ms) after
each step, so the gear knob animation starts, then releases — matching the original's edge
behaviour without leaving fire stuck on.

Menu mapping, added to `getkey_dx` (`src/platform/input.c:132`) so it applies to every input mode,
not only the edge modes:

| Mouse | Key |
|---|---|
| left click | `0x000D` Enter |
| right click | `0x1B` Esc |
| a flick past the steer threshold | `DS_joy_menu_scan[…]`, deduped like the joystick path |

Meta controls, on every screen, taken from the click queue:

| Mouse | Action |
|---|---|
| middle click | pause (`Ctrl-P`) |
| X1 side button | sound on/off (`Ctrl-Q` / `Ctrl-S`) |

### Resolution note — middle click

The two pre-design answers disagreed about middle click: the gear question gave it the `o`
gate-shift toggle, the meta question gave it pause. This spec assigns it to **pause**, because
pause is an everyday safety hatch and `o` is an optional mode toggle. `d` and `o` therefore have no
mouse binding (see Non-goals); `host_kbd_push` is consequently not needed and is not added.

### C — `platform/input.c`: on-screen keyboard for the name editor

`text_input_line` keeps its single loop and its contract (`buf`, return 0); a mouse branch is
added beside the existing keyboard branch, so typing still works and `scores_enter_name` needs no
change.

The grid is drawn onto the EGA screen with the existing primitives (`gfx_fill_rect`,
`gfx_draw_text`, `draw_rect_outline`) — not the overlay, which the enhanced renderer owns. Layout
in 320×200: a title, the live name field, then `A–Z` + `SPACE`, with `◀` `▶` `DEL` `OK`. Since the
cursor is visible, cells are hit-tested by absolute position and hover is highlighted; left click
acts on the cell under the pointer, right click still cancels, and the idle timeout still applies.

## Verification

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build                                       # expect no warnings
./build/testdrive-enhanced --game-dir Game --check        # expect exit 0
```

The steering/throttle combination is pure arithmetic, so it is factored into a function with an
assert-based self-check run from the scratch area (ENGINE.md allows throwaway verification
scripts); no test framework is added.

Interactive, needs a display, not scriptable:

```
./build/testdrive-enhanced --game-dir Game
```

- Menus: left click selects, right click backs out, flicks move the selection.
- Driving: left accelerates, right brakes, horizontal movement steers and self-centres, wheel
  shifts a gear, middle click pauses, side button toggles sound.
- High score: the grid types a name with clicks and commits with OK.
- Cursor is never captured, hidden or confined; pointer leaving the window releases the buttons.

## Risks

- **Auto-centre feel.** Holding a steering angle requires continued movement; if this reads badly
  on the road, `MOUSE_DECAY_PER_MS` (or "only decay after N ms idle") is the single adjustment.
- **Menus rely on `DS_input_mode == 4`.** `flow.c:125` sets it globally, so handling the mouse in
  `getkey_dx` rather than only the edge path is what keeps this correct; if a screen ever runs in
  mode 0 the mouse Enter still works through the same `getkey_dx` hook.
- **Interactive verification** is the only unscriptable step, as in the Apple Silicon spec.
- **Mice without side buttons** cannot toggle sound from the mouse; `Ctrl-Q` / `Ctrl-S` remain. All
  gameplay, menus, pause and name entry still work, so the mouse-only claim holds for playing.
- **Windowed only.** No fullscreen or dock changes; `Alt+Enter` stays keyboard.

## Follow-up: on-screen steering buttons

Added after the review of the work above. Scope: two buttons that let the pointer steer without
relative motion, supplementary to it (nothing is removed).

- Geometry and the pure hit-test (`steer_button_at`, EGA 320x200, half-open rects) live in
  `src/platform/input.h` so drawing and hit-testing share one source of truth.
- The enhanced renderer draws them in `ov_draw()` only while a stage is active, over the dashboard
  corners (y 176..196, clear of the road window rows 19..111). `ov_dirty()` also reports hover or
  press changes so the highlight refreshes even when the road is still.
- Holding the left button over an arrow steers that way via the existing truth table; because the
  left button is also the accelerator, that reads as accelerate-and-turn (direction 8 or 2, and
  6/4 with the right button held as brake). Sliding off the arrow releases the steer.
- `host_mouse_buttons()` is a side-effect-free accessor so the renderer can highlight a pressed
  button without consuming motion or wheel input from `host_mouse_read`.
- Not included: SDL touch/finger input. The buttons are pointer hit-regions over the visible
  cursor, so a touchscreen would need `SDL_EVENT_FINGER_*` plumbing later.
