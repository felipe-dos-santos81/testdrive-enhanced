# Mouse-Only Control Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Test Drive Enhanced playable with the mouse alone — driving, menus, gear shift, pause/sound and high-score name entry — without ever capturing, hiding or confining the cursor.

**Architecture:** `src/host.c` owns all SDL and exposes raw mouse state (motion deltas, held buttons, wheel, click queue) exactly as it already owns the keyboard and gamepad. `src/platform/input.c` owns the control policy: it accumulates horizontal motion into an auto-centred steering offset and maps throttle/steer/wheel into the original's direction+fire input word, consulted at the two existing dispatch points. The high-score name editor gains an on-screen keyboard drawn with existing gfx primitives. No new source files.

**Tech Stack:** C11, SDL3, CMake + Ninja, the existing `TDEGA.EXE` faithful core.

**Spec:** `docs/superpowers/specs/2026-09-16-mouse-control-design.md`

## Global Constraints

- C11 (`set(CMAKE_C_STANDARD 11)`); types come from `src/types.h` (`u8/s8/u16/s16/u32/s32`) — never `int` where a fixed width is meant.
- Must compile **without warnings** under `-Wall -Wextra -Wno-unused-parameter -fno-strict-aliasing`.
- No new dependencies. No new source files: everything lands in `src/host.h`, `src/host.c`, `src/platform/input.h`, `src/platform/input.c`.
- Only `host.c` and `main.c` include SDL. `platform/input.c` never includes SDL; it talks to the host through `host.h`.
- No `static` game state in the faithful modules: game state lives in `mem[]`. Host-side caches (`host.c`) and the mouse control state in `input.c` are host-side, so `static` is allowed there, like the existing `held_keys` flag (`src/host.c:321`).
- Faithful core behaviour, timing and arithmetic stay unchanged; every addition is marked `/* PORT: ... */`, matching the existing notes in `platform/input.c`.
- Never call `SDL_SetWindowRelativeMouseMode`, `SDL_SetWindowGrab` or `SDL_HideCursor`.
- Commit after each task; message style matches history (`Area: change`).
- Build/verify commands (macOS, this machine):
  - `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build` — expect no warnings.
  - `./build/testdrive-enhanced --game-dir Game --check` — expect exit 0. On this machine the data is in `TestDrive/`, so use `--game-dir TestDrive` if `Game/` is absent.

### Spec amendments (implementation decisions found while planning)

1. **`host_mouse_pos` added.** The spec lists click coordinates only, but hover highlighting needs the live pointer, so `host.h` also exposes `bool host_mouse_pos(s16 *ex, s16 *ey)`. Same coordinate space as the click queue.
2. **Click queue is context-drained.** Menus consume left/right clicks as Enter/Esc; driving consumes buttons as *held* accelerate/brake and drains (discards) queued clicks so a click during driving cannot fire a stale Enter in the next menu. Middle/X1 are always meta (pause/sound) in both contexts.
3. **The name editor gets its own loop.** `text_input_line` blocks inside `menu_key()` (`src/platform/input.c:369`), which polls no mouse. The mouse-capable editor needs a loop that polls both, so `text_input_line`'s body is replaced by one editor that handles mouse *and* the original key cases with unchanged semantics. Contract and `buf` result are unchanged, so `scores_enter_name` (`src/game/flow_scores.c:82`) is not touched.

---

### Task 1: Host mouse state

**Files:**
- Modify: `src/host.h` (add declarations near `host_joy_read`, after line 58)
- Modify: `src/host.c` (state near the keyboard block at line 28; event cases in `process_events` at line 418; functions after `host_joy_read` at line 468)

**Interfaces:**
- Consumes: existing `process_events`, `frame_w`/`frame_h`, `renderer` in `host.c`.
- Produces:
  - `void host_mouse_read(s16 *dx, u8 *held, s16 *wheel)` — consumes accumulated horizontal motion and wheel steps; returns held buttons (`bit0` left, `bit1` right, `bit2` middle, `bit3` X1, `bit4` X2).
  - `bool host_mouse_pos(s16 *ex, s16 *ey)` — current pointer in EGA 320×200 coordinates; false if the frame size is not known yet.
  - `bool host_mouse_click(s16 *ex, s16 *ey, u8 *button)` — pops one queued press in EGA coordinates; false when empty.

- [ ] **Step 1: Add the declarations to `src/host.h`**

Insert after the joystick block (after `bool host_joy_read(s16 *x, s16 *y, u8 *buttons);`):

```c
/* ---- Mouse: raw SDL state, no policy. dx accumulates between calls and is consumed by
 * host_mouse_read. Buttons: bit0 left, bit1 right, bit2 middle, bit3 X1, bit4 X2. wheel is the
 * number of steps (+1 up, -1 down) since the last read. */
void host_mouse_read(s16 *dx, u8 *held, s16 *wheel);

/* Current pointer and one queued press, both in EGA 320x200 screen coordinates. */
bool host_mouse_pos(s16 *ex, s16 *ey);
bool host_mouse_click(s16 *ex, s16 *ey, u8 *button);
```

- [ ] **Step 2: Add the state to `src/host.c`**

Next to the keyboard buffer (after `static int kbd_head, kbd_tail;`):

```c
/* Mouse: raw SDL state, converted to EGA 320x200 coordinates on the way in. */
static s16 mouse_dx;
static s16 mouse_wheel;
static u8 mouse_held;
static s16 mouse_pos_x, mouse_pos_y;
#define MOUSE_CLICK_MAX 8
static struct { s16 x, y; u8 button; } mouse_clicks[MOUSE_CLICK_MAX];
static int mouse_click_head, mouse_click_tail;
```

- [ ] **Step 3: Add the event handling to `process_events`**

In the `switch (ev.type)` in `process_events` (`src/host.c:422`), before `default:`:

```c
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            SDL_ConvertEventToRenderCoordinates(renderer, &ev);
            float rx, ry;
            if (ev.type == SDL_EVENT_MOUSE_MOTION) {
                mouse_dx = (s16)(mouse_dx + (s16)ev.motion.xrel);
                rx = ev.motion.x;
                ry = ev.motion.y;
            } else {
                rx = ev.button.x;
                ry = ev.button.y;
            }
            if (frame_w > 0 && frame_h > 0) {                /* window -> EGA 320x200 */
                mouse_pos_x = (s16)(rx * 320.0f / (float)frame_w);
                mouse_pos_y = (s16)(ry * 200.0f / (float)frame_h);
            }
            if (ev.type != SDL_EVENT_MOUSE_MOTION) {
                u8 bit = 0;
                switch (ev.button.button) {
                case SDL_BUTTON_LEFT:   bit = 0x01; break;
                case SDL_BUTTON_RIGHT:  bit = 0x02; break;
                case SDL_BUTTON_MIDDLE: bit = 0x04; break;
                case SDL_BUTTON_X1:     bit = 0x08; break;
                case SDL_BUTTON_X2:     bit = 0x10; break;
                default: break;
                }
                if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    mouse_held |= bit;
                    int next = (mouse_click_tail + 1) % MOUSE_CLICK_MAX;
                    if (next != mouse_click_head) {          /* full: drop, like kbd_push */
                        mouse_clicks[mouse_click_tail].x = mouse_pos_x;
                        mouse_clicks[mouse_click_tail].y = mouse_pos_y;
                        mouse_clicks[mouse_click_tail].button = bit;
                        mouse_click_tail = next;
                    }
                } else {
                    mouse_held &= (u8)~bit;
                }
            }
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
            mouse_wheel = (s16)(mouse_wheel + (ev.wheel.y > 0 ? 1 : ev.wheel.y < 0 ? -1 : 0));
            break;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            mouse_held = 0;                                  /* no stuck accelerate */
            break;
```

- [ ] **Step 4: Add the accessors after `host_joy_read`**

```c
void host_mouse_read(s16 *dx, u8 *held, s16 *wheel)
{
    process_events();
    if (dx) *dx = mouse_dx;
    if (held) *held = mouse_held;
    if (wheel) *wheel = mouse_wheel;
    mouse_dx = 0;
    mouse_wheel = 0;
}

bool host_mouse_pos(s16 *ex, s16 *ey)
{
    process_events();
    if (frame_w <= 0 || frame_h <= 0) return false;
    if (ex) *ex = mouse_pos_x;
    if (ey) *ey = mouse_pos_y;
    return true;
}

bool host_mouse_click(s16 *ex, s16 *ey, u8 *button)
{
    process_events();
    if (mouse_click_head == mouse_click_tail) return false;
    if (ex) *ex = mouse_clicks[mouse_click_head].x;
    if (ey) *ey = mouse_clicks[mouse_click_head].y;
    if (button) *button = mouse_clicks[mouse_click_head].button;
    mouse_click_head = (mouse_click_head + 1) % MOUSE_CLICK_MAX;
    return true;
}
```

- [ ] **Step 5: Build and run the headless check**

Run:
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
./build/testdrive-enhanced --game-dir Game --check
```
Expected: build with no warnings; `--check` exits 0.

- [ ] **Step 6: Commit**

```bash
git add src/host.h src/host.c
git commit -m "Mouse control: host mouse state (deltas, buttons, wheel, clicks)"
```

---

### Task 2: Steering policy and direction mapping (pure, with self-check)

**Files:**
- Modify: `src/platform/input.h` (add the policy block after the `text_input_line` declaration, line 19)
- Test: scratch harness outside the repo, `/var/folders/h_/rk2gng5d0x99pw7x_3dg6mj40000gn/T/opencode/mouse_ctl_check.c` (never committed)

**Interfaces:**
- Consumes: `u8/s16/u16/u32/bool` from `types.h` (via `input.h` → `mem.h`).
- Produces (all `static inline`, so the scratch harness links nothing):
  - `s16 mouse_steer_step(s16 off, s16 dx, u32 dt_ms)`
  - `u16 mouse_direction(s16 off, bool up, bool down)` — 0 neutral, else 1–8 exactly as `held_controls` (`src/platform/input.c:207`) and `drive_key` (`src/platform/input.c:262`) define: 1 up, 2 up-right, 3 right, 4 down-right, 5 down, 6 down-left, 7 left, 8 up-left.
  - `u8 mouse_joy_nibble(u16 dir)` — direction code to the joystick direction bitfield `DS_joy_menu_scan` is indexed by (1 up, 2 down, 4 right, 8 left, OR-combined).

- [ ] **Step 1: Write the failing self-check**

Create `/var/folders/h_/rk2gng5d0x99pw7x_3dg6mj40000gn/T/opencode/mouse_ctl_check.c`:

```c
/* Throwaway self-check for the mouse control policy (spec 2026-09-16-mouse-control).
 * Build: cc -std=c11 -Wall -Wextra -I <repo>/src/platform mouse_ctl_check.c -o mouse_ctl_check */
#include "input.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    /* clamp on a fast flick */
    assert(mouse_steer_step(0, 1000, 0) == MOUSE_OFF_MAX);
    assert(mouse_steer_step(0, -1000, 0) == -MOUSE_OFF_MAX);

    /* auto-centre: decay is MOUSE_DECAY_PER_MS per millisecond, never past zero */
    assert(mouse_steer_step(10, 0, 1000) == 0);
    assert(mouse_steer_step(-10, 0, 1000) == 0);
    assert(mouse_steer_step(MOUSE_OFF_MAX, 0, 1) == MOUSE_OFF_MAX - MOUSE_DECAY_PER_MS);

    /* accumulate then survive a short idle */
    assert(mouse_steer_step(10, 10, 0) == 20);

    /* direction codes match held_controls() */
    assert(mouse_direction(0, true, false) == 1);
    assert(mouse_direction(0, false, true) == 5);
    assert(mouse_direction(0, false, false) == 0);
    assert(mouse_direction(MOUSE_OFF_THRESH, false, false) == 3);
    assert(mouse_direction(-MOUSE_OFF_THRESH, false, false) == 7);
    assert(mouse_direction(MOUSE_OFF_THRESH, true, false) == 2);
    assert(mouse_direction(-MOUSE_OFF_THRESH, false, true) == 6);
    assert(mouse_direction(MOUSE_OFF_THRESH, true, true) == 3);   /* up+down cancel */
    assert(mouse_direction(MOUSE_OFF_THRESH - 1, false, false) == 0);

    /* menu nibble: the bitfield DS_joy_menu_scan is indexed by */
    assert(mouse_joy_nibble(0) == 0);
    assert(mouse_joy_nibble(1) == 1);
    assert(mouse_joy_nibble(2) == 5);
    assert(mouse_joy_nibble(3) == 4);
    assert(mouse_joy_nibble(5) == 2);
    assert(mouse_joy_nibble(7) == 8);
    assert(mouse_joy_nibble(8) == 9);

    puts("mouse control policy: ok");
    return 0;
}
```

- [ ] **Step 2: Run it to verify it fails**

Run:
```bash
cc -std=c11 -Wall -Wextra -I src/platform \
   /var/folders/h_/rk2gng5d0x99pw7x_3dg6mj40000gn/T/opencode/mouse_ctl_check.c \
   -o /tmp/mouse_ctl_check
```
Expected: FAIL to compile — implicit declarations / `MOUSE_OFF_MAX` undeclared.

- [ ] **Step 3: Add the policy to `src/platform/input.h`**

Append after the `text_input_line` declaration:

```c
/* Mouse control policy (docs/superpowers/specs/2026-09-16-mouse-control-design.md). Pure integer
 * arithmetic, host-free, so the scratch self-check can exercise it without linking the game.
 * Steering is relative with auto-centre: a held angle cannot be parked, it springs back when the
 * mouse is idle. MOUSE_DECAY_PER_MS is the single feel knob; thresholds are in window pixels, so a
 * very different --scale changes the feel. */
#define MOUSE_OFF_MAX        120
#define MOUSE_OFF_THRESH      30
#define MOUSE_DECAY_PER_MS     1

static inline s16 mouse_steer_step(s16 off, s16 dx, u32 dt_ms)
{
    s32 o = (s32)off + dx;
    if (o > MOUSE_OFF_MAX) o = MOUSE_OFF_MAX;
    else if (o < -MOUSE_OFF_MAX) o = -MOUSE_OFF_MAX;
    s32 decay = (s32)MOUSE_DECAY_PER_MS * (s32)dt_ms;
    if (o > 0) { o -= decay; if (o < 0) o = 0; }
    else if (o < 0) { o += decay; if (o > 0) o = 0; }
    return (s16)o;
}

static inline u16 mouse_direction(s16 off, bool up, bool down)
{
    if (up && down) up = down = false;                      /* same truth table as held_controls() */
    bool left = off <= -MOUSE_OFF_THRESH;
    bool right = off >= MOUSE_OFF_THRESH;
    if (left && right) left = right = false;
    if (up)   return left ? 8 : right ? 2 : 1;
    if (down) return left ? 6 : right ? 4 : 5;
    if (left) return 7;
    if (right) return 3;
    return 0;
}

static inline u8 mouse_joy_nibble(u16 dir)
{
    switch (dir) {                                          /* 1 up, 2 down, 4 right, 8 left */
    case 1: return 1; case 2: return 1 | 4; case 3: return 4; case 4: return 2 | 4;
    case 5: return 2; case 6: return 2 | 8; case 7: return 8; case 8: return 1 | 8;
    default: return 0;
    }
}
```

- [ ] **Step 4: Run the self-check to verify it passes**

Run:
```bash
cc -std=c11 -Wall -Wextra -I src/platform \
   /var/folders/h_/rk2gng5d0x99pw7x_3dg6mj40000gn/T/opencode/mouse_ctl_check.c \
   -o /tmp/mouse_ctl_check && /tmp/mouse_ctl_check
```
Expected: `mouse control policy: ok`, no warnings.

- [ ] **Step 5: Build the game to confirm the header still compiles in context**

Run:
```bash
cmake --build build
```
Expected: no warnings.

- [ ] **Step 6: Commit**

```bash
git add src/platform/input.h
git commit -m "Mouse control: steering offset, direction code, menu nibble policy"
```

---

### Task 3: Mouse driving

**Files:**
- Modify: `src/platform/input.c` (mouse state after the forward declarations at line 58; `input_poll_drive`, line 228)

**Interfaces:**
- Consumes: `host_mouse_read` (Task 1); `mouse_steer_step`, `mouse_direction` (Task 2); `host_time_ns`, `host_kbd_read`, `host_held_keys`; existing `drive_key`, `held_controls`, `input_poll_drive_bios`, `getkey_wait_dx`.
- Produces: `static u16 mouse_drive(void)` and `static void mouse_meta(bool menus, u16 *pending)`, both used again by Task 4.

- [ ] **Step 1: Add the mouse state and helpers to `src/platform/input.c`**

Insert **after the two forward declarations** `static u16 getkey_kbd_ctrl(u16 *dx);` / `static u16 getkey_wait_dx(u16 *dx);` (line 58) and **before `getkey_kbd_ctrl`**, so the helpers are defined above `getkey_dx` (line 131), which Task 4 makes use of them:

```c
/* ---------------------------------------------------------------- mouse (PORT) */

/* PORT: mouse-only control (docs/superpowers/specs/2026-09-16-mouse-control-design.md). Steering is
 * relative with auto-centre; throttle comes from the held buttons; the wheel pulses fire + a
 * direction to shift one gear. Buttons held: bit0 left, bit1 right, bit2 middle, bit3 X1. */
typedef struct { s16 off; u8 held; s16 wheel; } MouseState;

static s16 mouse_off;
static uint64_t mouse_off_ns;
static s16 mouse_gear_dir;
static u8 mouse_gear_polls;

static MouseState mouse_poll(void)
{
    s16 dx; u8 held; s16 wheel;
    host_mouse_read(&dx, &held, &wheel);
    uint64_t now = host_time_ns();
    if (mouse_off_ns == 0) mouse_off_ns = now;
    u32 dt = (u32)((now - mouse_off_ns) / 1000000u);
    mouse_off_ns = now;
    if (dt > 250u) dt = 250u;                               /* cap after a stall */
    mouse_off = mouse_steer_step(mouse_off, dx, dt);
    MouseState s = { mouse_off, held, wheel };
    return s;
}

/* Middle click pauses, X1 toggles sound (the Ctrl-P / Ctrl-Q / Ctrl-S actions, same state writes).
 * In menus, left/right clicks become the pending Enter/Esc; while driving the buttons are held
 * controls, so the clicks are drained and discarded instead of leaking an Enter into the next menu. */
static void mouse_meta(bool menus, u16 *pending)
{
    s16 ex, ey;
    u8 btn;
    while (host_mouse_click(&ex, &ey, &btn)) {
        if ((btn & 0x04) && DSB(DS_modal_pause) == 0) {      /* middle: pause */
            u16 dx = 0;
            DSB(DS_modal_pause) = 1;
            getkey_wait_dx(&dx);
            DSB(DS_modal_pause) = 0;
        } else if (btn & 0x08) {                            /* X1: sound on/off */
            if (DSB(DS_snd_flags) & 4) {
                DSB(DS_snd_flags) &= 3;
            } else {
                DSB(DS_snd_flags) |= 4;
                if (DSB(DS_snd_flags) & 2) DSB(DS_snd_playing) |= 2;
            }
        } else if (menus && pending && *pending == 0) {
            if (btn & 0x01) *pending = 0x000D;              /* left: Enter */
            else if (btn & 0x02) *pending = 0x001B;         /* right: Esc */
        }
    }
}

/* Wheel = one gear step: fire + up / fire + down, held for a few polls so the knob animation starts. */
static u16 mouse_drive(void)
{
    MouseState s = mouse_poll();
    if (s.wheel > 0) { mouse_gear_dir = 1; mouse_gear_polls = 3; }
    else if (s.wheel < 0) { mouse_gear_dir = -1; mouse_gear_polls = 3; }
    if (mouse_gear_polls > 0) {
        mouse_gear_polls--;
        return (u16)((mouse_gear_dir > 0 ? 1u : 5u) | 0x10u);
    }
    return mouse_direction(s.off, (s.held & 0x01) != 0, (s.held & 0x02) != 0);
}
```

- [ ] **Step 2: Wire it into `input_poll_drive`**

Replace the body of `input_poll_drive` (lines 227-247) with:

```c
u16 input_poll_drive(void)
{
    u16 pending = 0;
    mouse_meta(false, &pending);                            /* PORT: pause / sound, always available */
    if (host_held_keys()) {
        /* Drain the buffer; buffered repeats of the held keys are skipped so they cannot displace a
         * real keystroke ("last key wins" applies to the remaining keys). */
        u16 key, last = 0;
        u8 taps = 0;
        bool other = false;
        while (host_kbd_read(&key)) {
            if (is_held_key(key)) taps |= tap_bits(key);
            else { last = key; other = true; }
        }
        if (other) {
            u16 r = drive_key(last);
            if (r != 0) return r;
        }
        u16 held = held_controls(taps);
        if (held) return held;
    }
    /* PORT: mouse driving when the keyboard is idle. */
    u16 m = mouse_drive();
    if (m != 0) return m;
    return input_poll_drive_bios();                         /* joystick / last key */
}
```

- [ ] **Step 3: Build and run the headless check**

Run:
```bash
cmake --build build && ./build/testdrive-enhanced --game-dir Game --check
```
Expected: no warnings; exit 0.

- [ ] **Step 4: Manual drive check**

Run `./build/testdrive-enhanced --game-dir Game` (or `--game-dir TestDrive`), start a stage, and confirm:
- moving the mouse left/right steers and springs back to straight when you stop;
- holding the left button accelerates, holding the right button brakes;
- one wheel notch changes the gear (the gear-box panel opens and the knob moves);
- middle click pauses; keyboard arrows still drive when the mouse is idle.

- [ ] **Step 5: Commit**

```bash
git add src/platform/input.c
git commit -m "Mouse control: drive with held buttons, auto-centred steering, wheel gear shift"
```

---

### Task 4: Mouse menus

**Files:**
- Modify: `src/platform/input.c` (`getkey_dx`, line 132)

**Interfaces:**
- Consumes: `mouse_meta`, `mouse_poll` (Task 3); `mouse_direction`, `mouse_joy_nibble` (Task 2); `DS_joy_menu_scan`, `DS_joy_menu_last` (existing symbols).
- Produces: `static u16 mouse_menu(void)`; `getkey_dx` now always falls back to the mouse.

- [ ] **Step 1: Replace `getkey_dx`**

Replace `getkey_dx` (lines 131-142) with:

```c
/* PORT: mouse menu action — left click Enter, right click Esc, a flick past the steer threshold
 * moves the selection through the same table the joystick edge path uses, deduped the same way. */
static u16 mouse_menu(void)
{
    u16 pending = 0;
    mouse_meta(true, &pending);
    if (pending != 0) return pending;
    u16 dir = mouse_direction(mouse_poll().off, false, false);
    u16 r = DSW((u16)(DS_joy_menu_scan + (u16)mouse_joy_nibble(dir) * 2));
    if (r == DSW(DS_joy_menu_last)) return 0;
    DSW(DS_joy_menu_last) = r;
    return r;
}

/* 0x67E8 getkey dispatch through CS:67F2[input_mode], with the mouse consulted when it yields nothing. */
static u16 getkey_dx(u16 *dx)
{
    switch (DSW(DS_input_mode)) {
    case 0: return getkey_kbd();
    case 2: { u16 k = getkey_kbd_joy_level(dx); if (k != 0) return k; break; }
    case 4: { u16 k = getkey_kbd_joy_edge(dx);   if (k != 0) return k; break; }
    default:
        /* PORT: other modes jump through code bytes in the original; treat as "no key". */
        break;
    }
    return mouse_menu();
}
```

- [ ] **Step 2: Build and run the headless check**

Run:
```bash
cmake --build build && ./build/testdrive-enhanced --game-dir Game --check
```
Expected: no warnings; exit 0.

- [ ] **Step 3: Manual menu check**

Run the game and confirm: left click starts a car / enters a menu, right click goes back, moving the mouse flick-moves the car-select selection one step per flick, and a click that happens while driving does not later fire an Enter in a menu.

- [ ] **Step 4: Commit**

```bash
git add src/platform/input.c
git commit -m "Mouse control: menu enter/back and flick selection"
```

---

### Task 5: On-screen keyboard for the high-score name

**Files:**
- Modify: `src/platform/input.c` (`text_input_line`, lines 353-410)

**Interfaces:**
- Consumes: `host_mouse_pos`, `host_mouse_click` (Task 1); `gfx_draw_text`, `draw_text_centered`, `draw_rect_outline`, `gfx_fill_rect`, `gfx_set_text_colours` (`gfx.h`); `getkey`, `set_deadline`, `ticks_elapsed`, `host_pump`; `DS_deadline_start`, `DS_deadline_len`.
- Produces: unchanged `int text_input_line(char *buf, int maxlen, s16 x, s16 y, u16 timeout)` — fills `buf` (already zero-initialised by the caller at `src/game/flow_scores.c:71`) and returns 0.

- [ ] **Step 1: Replace `text_input_line`**

Replace lines 353-410 with:

```c
/* 0x92A8 text_input_line — high-score name editor, PORT: mouse-capable on-screen keyboard. The
 * original's key semantics are kept (Right/Left/Ins/Del/Backspace/letters/Enter and the idle
 * timeout); a clickable grid is added. Right click clears and commits (an empty name is not
 * recorded, see scores_enter_name). */
enum { OSK_COLS = 7, OSK_ROWS = 5, OSK_CELLS = 31,
       OSK_X0 = 20, OSK_Y0 = 60, OSK_CW = 40, OSK_CH = 24 };

static const char *osk_label(int i)
{
    static const char *labels[OSK_CELLS] = {
        "A","B","C","D","E","F","G",   "H","I","J","K","L","M","N",
        "O","P","Q","R","S","T","U",   "V","W","X","Y","Z","SPC","<",
        ">","DEL","OK"
    };
    return i >= 0 && i < OSK_CELLS ? labels[i] : "";
}

static int osk_hit(s16 ex, s16 ey)
{
    if (ex < OSK_X0 || ey < OSK_Y0) return -1;
    int col = (ex - OSK_X0) / OSK_CW;
    int row = (ey - OSK_Y0) / OSK_CH;
    if (col < 0 || col >= OSK_COLS || row < 0 || row >= OSK_ROWS) return -1;
    int i = row * OSK_COLS + col;
    return i < OSK_CELLS ? i : -1;
}

static void osk_draw(const char *name, int hover)
{
    gfx_set_text_colours(0x0F, 0);
    draw_text_centered("ENTER YOUR NAME", 8);
    draw_rect_outline(0x50, 0x1C, 0xF0, 0x2C, 0x0F);
    gfx_draw_text(name, 0x58, 0x1E);
    for (int i = 0; i < OSK_CELLS; i++) {
        s16 cx = (s16)(OSK_X0 + (i % OSK_COLS) * OSK_CW);
        s16 cy = (s16)(OSK_Y0 + (i / OSK_COLS) * OSK_CH);
        if (i == hover) gfx_fill_rect((s16)(cx + 1), (s16)(cy + 1), OSK_CW - 2, OSK_CH - 2, 0x08);
        draw_rect_outline(cx, cy, (s16)(cx + OSK_CW - 2), (s16)(cy + OSK_CH - 2), 0x07);
        gfx_set_text_colours(0x0F, 0);
        gfx_draw_text(osk_label(i), (s16)(cx + 4), (s16)(cy + 8));
    }
}

int text_input_line(char *buf, int maxlen, s16 x, s16 y, u16 timeout)
{
    (void)x; (void)y;                                     /* the grid replaces the original layout */
    s16 len = (s16)maxlen;
    s16 i;
    for (i = 0; i < len; i++) buf[i] = ' ';
    buf[len] = 0;

    s16 pos = 0;
    gfx_clear_screen(0);
    osk_draw(buf, -1);
    set_deadline(timeout);

    for (;;) {
        bool acted = false;
        u16 key = getkey();
        if (key != 0) {
            acted = true;
            if (key == 0x0D) break;                        /* Enter */
            set_deadline(timeout);                         /* idle timeout restarts after a key */
            if (key == 0x4D00) {                           /* Right */
                if (len - 1 > pos) pos++;
            } else if (key == 0x4B00) {                    /* Left */
                if (pos != 0) pos--;
            } else if (key == 0x08) {                      /* Backspace */
                if (pos != 0) { pos--; buf[pos] = ' '; }
            } else if (key == 0x5300) {                    /* Del */
                for (i = pos; len - 1 > i; i++) buf[i] = buf[i + 1];
                buf[len - 1] = ' ';
            } else if (key >= 0x20 && key <= 0x7A) {
                buf[pos] = (char)key;
                if (len - 1 > pos) pos++;
            } else {
                acted = false;                             /* nothing changed */
            }
        } else {
            s16 ex, ey;
            u8 btn;
            while (host_mouse_click(&ex, &ey, &btn)) {
                acted = true;
                if (btn & 0x02) { buf[0] = 0; goto done; } /* right click: clear and commit */
                int cell = osk_hit(ex, ey);
                if (cell >= 0) {
                    set_deadline(timeout);
                    if (cell < 26) {                       /* letter */
                        buf[pos] = (char)('A' + cell);
                        if (len - 1 > pos) pos++;
                    } else if (cell == 26) {               /* SPACE */
                        buf[pos] = ' ';
                        if (len - 1 > pos) pos++;
                    } else if (cell == 27) {               /* < */
                        if (pos != 0) pos--;
                    } else if (cell == 28) {               /* > */
                        if (len - 1 > pos) pos++;
                    } else if (cell == 29) {               /* DEL */
                        if (pos != 0) { pos--; buf[pos] = ' '; }
                    } else {                               /* OK */
                        goto done;
                    }
                }
            }
        }
        if (!acted) {
            if (ticks_elapsed(DSW(DS_deadline_start)) >= DSW(DS_deadline_len)) break;
            host_pump();
            continue;
        }
        gfx_clear_screen(0);
        s16 hx, hy;
        osk_draw(buf, host_mouse_pos(&hx, &hy) ? osk_hit(hx, hy) : -1);
        host_present_now();
    }
done:
    buf[len] = 0;
    /* TODO(verify): the original returns whatever AX draw_glyph left; the only caller ignores it. */
    return 0;
}
```

- [ ] **Step 2: Add the includes if missing**

`src/platform/input.c` already includes `gfx.h`, `timer.h` and `../host.h` (lines 9-12). Confirm `host_present_now` is declared in `host.h` (line 41) — no edit needed unless the build complains.

- [ ] **Step 3: Build and run the headless check**

Run:
```bash
cmake --build build && ./build/testdrive-enhanced --game-dir Game --check
```
Expected: no warnings; exit 0.

- [ ] **Step 4: Manual name-entry check**

Play to a qualifying high score (or temporarily lower the bar), confirm:
- the grid appears; hovering highlights a cell; clicking a letter appends it and shows it in the box;
- `<` `>` move the cursor, `DEL` deletes backwards, `OK` commits and the name appears in the table;
- right click clears and commits an empty name (no table entry);
- typing on the keyboard still edits the name.

- [ ] **Step 5: Commit**

```bash
git add src/platform/input.c
git commit -m "Mouse control: on-screen keyboard for the high-score name"
```

---

### Task 6: Document the controls

**Files:**
- Modify: `README.md` (Controls section, lines 92-99)

**Interfaces:** none (documentation).

- [ ] **Step 1: Update the Controls section**

Replace the Controls list with (keep the existing keyboard bullets, add a mouse block):

```markdown
## Controls

### Mouse (works on every screen)

* Steer by moving the mouse left and right; it springs back to straight when you stop.
* Left button: accelerate while held. Right button: brake while held.
* Wheel: shift up / down one gear.
* Menus: left click selects, right click goes back, moving the mouse moves the selection.
* Middle click pauses. A side button toggles sound (Ctrl-Q / Ctrl-S otherwise).
* High-score name entry: click letters on the on-screen keyboard, `DEL` deletes, `OK` commits.
* The cursor is never captured, hidden or confined.

### Keyboard / gamepad (from the original)

* Arrow keys / numeric keypad: steer, accelerate, brake and shift through the gear gate, as in the original.
* Esc: quit the current drive or menu.
* Ctrl-J / Ctrl-K: joystick / keyboard control. A connected gamepad acts as the joystick (left stick or
  D-pad, A = fire).
* Ctrl-Q / Ctrl-S: sound off / on.
```

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "Mouse control: document the mouse controls"
```

---

## Self-Review

**Spec coverage**

| Spec section | Task |
|---|---|
| Host raw mouse state (`host_mouse_read`/`click`) | 1 |
| Steering: relative + auto-centre + thresholds | 2 |
| Drive map: L accelerate, R brake, wheel gear | 3 |
| Pause (middle) / sound (X1) | 3 |
| Menu map: click Enter/Esc, flick selection | 4 |
| On-screen keyboard name entry | 5 |
| No capture/hide/confine; leave/focus clears buttons | 1 |
| Warning-free build, `--check` exit 0, scratch self-check | 1–5 |
| Docs | 6 |

**Placeholder scan:** no TBD/TODO steps; the only `TODO(verify)` is carried over verbatim from the existing code and is not an instruction. Every code step shows the code.

**Type consistency:** `host_mouse_read(s16*, u8*, s16*)`, `host_mouse_pos(s16*, s16*)`, `host_mouse_click(s16*, s16*, u8*)` are declared in Task 1 and used with those exact shapes in Tasks 3 and 5. `mouse_steer_step(s16,s16,u32)`, `mouse_direction(s16,bool,bool)`, `mouse_joy_nibble(u16)` are defined in Task 2 and used identically in Tasks 3-4. Button bits (`0x01` left, `0x02` right, `0x04` middle, `0x08` X1, `0x10` X2) are defined in Task 1 and used consistently in Tasks 3-5.

## Execution Handoff

Two execution options:

1. **Subagent-Driven (recommended)** — a fresh subagent per task, review between tasks.
2. **Inline Execution** — execute the tasks in this session with checkpoints.
