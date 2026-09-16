# Left-button mouse control — design

Date: 2026-09-16
Status: approved (pending spec review)

Supersedes the input-map sections of `2026-09-16-mouse-control-design.md` (its architecture, gesture
engine ownership and safety notes still apply; the button map does not).

## Goal

Make Test Drive Enhanced playable with the **left mouse button alone**, using single click, double
click, press-and-hold and mouse direction, plus on-screen buttons. Right button, wheel, middle
button and side buttons stop doing anything. Keyboard and gamepad keep working unchanged.

Gas stays where it already felt right: hold the left button on the road.

## Findings (current state)

The shipped map (commit `dd6680c`) uses five buttons: left = accelerate, right = brake, wheel =
gear, middle = pause, X1 = sound, X2 = fire. Menu Esc comes from a right click, and the name
keyboard cancels on a right click. The driving view is drawn by the enhanced renderer's single
overlay slot (`src/enhanced/enhanced.c`, `ov_draw`), which is active only inside a stage, so
anything drawn over the road must come from there; menu and name-entry screens are EGA images and
draw with gfx primitives (`text_input_line` already draws its on-screen keyboard that way).

## Non-goals

- No change to the driving model, simulation, timing, memory model or faithful core behaviour.
- No new source files and no new dependencies.
- No SDL touch/finger input; the buttons are pointer hit-regions over the visible cursor.
- The gearbox display (`d`), gate-shift mode (`o`) and the Ctrl hotkeys stay keyboard-only.
- No change to the `--bios-keys`, `--scale` or `--res-scale` options.

## Gesture model

Press latches its target at press time; sliding the pointer off does not change the action in
progress (so gas cannot become brake mid-hold). File-local constants:

| Constant | Value | Meaning |
|---|---|---|
| `HOLD_MS` | 180 | pressed this long → hold; released sooner → tap |
| `DOUBLE_MS` | 300 | a tap within this of the previous tap is a double click |
| `FIRE_PULSE_POLLS` | 3 | length of a fire pulse, as the gear latch already uses |

Pure predicates live in `src/platform/input.h` (`mouse_is_hold`, `mouse_is_tap`, `mouse_is_double`)
so the scratch harness can exercise the boundaries without linking the game. State lives in
`src/platform/input.c`.

## Control map

**Driving (road window or bottom strip):**

| Gesture | Target | Effect |
|---|---|---|
| hold | road | accelerate |
| hold | ◀ / ▶ | steer that way (accelerate + turn, as today) |
| hold | BRAKE | brake |
| tap | ▲ / ▼ | gear up / down (one gear per tap) |
| tap | ♪ | sound on / off |
| tap | road | fire pulse, direction 0 |
| double click | road | pause |

**Menus:** click = Enter (immediate, never delayed); mouse up/down = move selection; click BACK =
Esc; click ♪ = sound.

**Name entry:** click letters, `DEL`, `OK` as today; a `CANCEL` cell replaces the old right-click
cancel; `♪` beside it.

## Safety invariant

A tap's fire is always emitted with direction 0, which `sim_input_direction` (`src/game/sim.c`)
ignores for gear selection (both the gate path and the `DS_gear_delta_table` path need a non-zero
direction), so a click can never shift a gear while still satisfying `wait_fire_button`'s bit
`0x10`. Only ▲▼ or a hold emits fire with a direction. Because a double click must not also fire,
the tap's fire is held back by `DOUBLE_MS`; that delay affects only the rare wait-screen dismissal,
never gas, steering, menus or key entry.

## On-screen chrome

Driving strip, EGA 320×200, y 176..196 (below the road window rows 19..111), drawn by the enhanced
overlay over the dashboard:

| Cell | x range |
|---|---|
| ◀ | 8..38 |
| ▶ | 40..70 |
| ▲ | 128..158 |
| ▼ | 160..190 |
| BRAKE | 232..284 |
| ♪ | 290..312 |

Menu screens and the name keyboard cannot use the overlay (inactive outside a stage), so their
cells are drawn with gfx primitives from `input.c`, exactly as the name keyboard already is. Menus
draw `♪` at x 290..312 and `BACK` at x 232..284, both y 176..192 (the right end of the same strip
zone). Those ranges deliberately match the driving strip's `BRAKE` and `♪` cells: the driving
strip only exists inside a stage and the menu cells only exist outside one, so no screen ever shows
two meanings in one cell. The keyboard grid gains `CANCEL` as the 32nd cell (index 31: row 4,
column 3 of the 7×5 grid), leaving indices 32..34 empty.

Geometry, the strip enum (`drive_cell_at`) and the menu-cell hit-test live in
`src/platform/input.h`, so drawing and hit-testing share one source of truth.

Wait screens (crash, GAME OVER, ending) treat any tap as fire and dismiss; they get no strip, since
the game is waiting on a single fire bit and nothing else is meaningful there.

## Ownership

- `src/host.h` / `src/host.c`: only change is a press timestamp output on `host_mouse_click`, so
  tap/double timing uses the event's own time rather than when it was drained. The queue keeps
  press edges and held bits as now.
- `src/platform/input.h`: gesture predicates, strip and menu-cell geometry/hit-tests.
- `src/platform/input.c`: gesture state machine and mapping; the middle/X1 branches in `mouse_meta`
  are deleted; menu click handling reduces to left click = Enter plus the new cells; the keyboard
  editor swaps right-click cancel for the `CANCEL` cell.
- `src/enhanced/enhanced.c`: bottom strip drawing, extended with ▲ ▼ BRAKE ♪.
- `README.md`: controls section rewritten for the left-button scheme.

## Removed bindings

Right click (menu Esc and keyboard cancel), wheel (gear), middle click (pause), X1 (sound) and X2
(fire) all stop doing anything. `mouse_meta` loses its middle and X1 branches. Pause moves to a
road double click; gear moves to ▲▼; sound to the ♪ cell; fire to a road tap.

## Edge cases

- Pointer leaving the window ends the held action, as now (`MOUSE_LEAVE` / focus loss clear held).
- A double click that starts a hold (press, release, press and keep pressing) counts as hold; the
  pending tap's fire is dropped.
- Clicking a screen cell while a hold is in progress does not start a second action; the latch wins
  until release.
- The name-keyboard capture flag keeps clicks away from menu handling, and the click queue is
  drained on every editor exit, both unchanged.
- `--res-scale` and window resizing are unaffected: coordinates are EGA 320×200 derived from
  `frame_w`/`frame_h`, not hard-coded.

## Verification

- Warning-free build under the project flags; `./build/testdrive-enhanced --game-dir Game --check`
  exits 0.
- Scratch harness gains assert cases for the pure predicates (tap at `HOLD_MS - 1`, hold at
  `HOLD_MS`, double within `DOUBLE_MS`, not-double beyond it) and for the strip/menu hit-test
  boundaries including cell gaps and edges.
- Manual windowed pass (only way to judge feel): gas, steer both ways by motion and by ◀ ▶, brake,
  gear up/down, sound cell, tap to dismiss crash / GAME OVER / ending, double click to pause,
  menus by click and motion with BACK and ♪, name entry with CANCEL.

## Risks

- `DOUBLE_MS` delay on the tap-fire is visible only on the wait screens; if it reads as sluggish,
  the constant is the one knob.
- Pause on a road double click sits next to the gas hold; a manual check must confirm a quick
  double click does not read as an unintended burst of throttle.
- Latching on press means a hold continues after the pointer slides off a cell; deliberate, and it
  prevents a mid-hold action change, but it differs from the slide-off release shipped previously.
- Because the strip covers the lower dashboard, the bottom of the wheel sprite is overlaid; accepted
  in the layout choice (A).
