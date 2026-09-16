# Left-Button Mouse Control Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Test Drive Enhanced playable with the left mouse button alone — single click, double click, press-and-hold, mouse direction and on-screen buttons — and stop the right button, wheel, middle button and side buttons from doing anything.

**Architecture:** All gesture and mapping policy stays in the input layer: pure predicates and hit-tests in `src/platform/input.h` (exercised by the existing scratch harness), a state machine in `src/platform/input.c`. The driving strip is drawn by the enhanced renderer's overlay (the only thing that draws over the road); menu cells and the name keyboard draw with gfx primitives, as the keyboard already does. `host.c` gains one thing: the press timestamp on the click queue.

**Tech Stack:** C11, SDL3, CMake + Ninja, the existing `TDEGA.EXE` faithful core.

**Spec:** `docs/superpowers/specs/2026-09-16-left-button-mouse-design.md`

## Global Constraints

- C11; fixed-width types from `src/types.h`; `uint64_t` for nanoseconds. Game state stays in `mem[]`; only host-side state may be `static`.
- Warning-free under `-Wall -Wextra -Wno-unused-parameter -fno-strict-aliasing`.
- No new dependencies; no new source files. Files touched: `src/host.h`, `src/host.c`, `src/platform/input.h`, `src/platform/input.c`, `src/enhanced/enhanced.c`, `README.md`.
- `src/platform/input.c` never includes SDL; only `host.c` and `main.c` do.
- Additions marked `/* PORT: ... */`.
- Never capture, hide or confine the cursor.
- Safety invariant: a tap's fire is emitted with direction 0 and must never carry a direction (a direction would shift gear).
- Mouse idle ⇒ keyboard and gamepad behave exactly as before.
- Constants: `MOUSE_HOLD_MS` 180, `MOUSE_DOUBLE_MS` 300, `FIRE_PULSE_POLLS` 3.
- Build/verify: `cmake --build build` (no warnings) and `./build/testdrive-enhanced --game-dir Game --check` (exit 0; on this machine the data is in `TestDrive/`, so use `--game-dir TestDrive` if `Game/` is absent).

### Spec clarifications found while planning

1. **Double click on the road, then keep holding.** The spec says a double click that starts a hold counts as hold. On the road a double click pauses, and the pause consumes the pointer while it waits, so the road case clears the press latch; a double click that starts a hold still works on every *other* cell (steer/brake), where no pause fires. This is the only reading that is internally consistent.
2. **Cell labels are text.** The mockup's `BRAKE` and `♪` labels are drawn as text: the driving strip uses the enhanced renderer's existing 5×7 glyph style (`DIGITS`, `draw_timer`) with a small added letter table, and the menu cells use the game font through `gfx_draw_text` (`BACK`, `SND`).

---

### Task 1: Press timestamp on the click queue

**Files:**
- Modify: `src/host.h` (declaration at the mouse block)
- Modify: `src/host.c` (`mouse_clicks` struct, button-down push, `host_mouse_click`)
- Modify: `src/platform/input.c` (the two existing callers, so it still builds)

**Interfaces:**
- Produces: `bool host_mouse_click(s16 *ex, s16 *ey, u8 *button, uint64_t *ns)` — pops one queued press in EGA coordinates plus the SDL event timestamp in nanoseconds (`ns` may be NULL).

- [ ] **Step 1: Change the declaration in `src/host.h`**

```c
/* Current pointer and one queued press, both in EGA 320x200 screen coordinates. ns receives the
 * press event's timestamp in nanoseconds (SDL's own clock), for tap/hold/double timing. */
bool host_mouse_pos(s16 *ex, s16 *ey);
bool host_mouse_click(s16 *ex, s16 *ey, u8 *button, uint64_t *ns);
```

- [ ] **Step 2: Store the timestamp when queuing**

In `src/host.c`, the click queue becomes:

```c
#define MOUSE_CLICK_MAX 8
static struct { s16 x, y; u8 button; uint64_t ns; } mouse_clicks[MOUSE_CLICK_MAX];
```

and in `process_events`'s `SDL_EVENT_MOUSE_BUTTON_DOWN` branch, when a slot is free:

```c
                        mouse_clicks[mouse_click_tail].x = mouse_pos_x;
                        mouse_clicks[mouse_click_tail].y = mouse_pos_y;
                        mouse_clicks[mouse_click_tail].button = bit;
                        mouse_clicks[mouse_click_tail].ns = ev.button.timestamp ? ev.button.timestamp : SDL_GetTicksNS();
                        mouse_click_tail = next;
```

- [ ] **Step 3: Return it from `host_mouse_click`**

```c
bool host_mouse_click(s16 *ex, s16 *ey, u8 *button, uint64_t *ns)
{
    process_events();
    if (mouse_click_head == mouse_click_tail) return false;
    if (ex) *ex = mouse_clicks[mouse_click_head].x;
    if (ey) *ey = mouse_clicks[mouse_click_head].y;
    if (button) *button = mouse_clicks[mouse_click_head].button;
    if (ns) *ns = mouse_clicks[mouse_click_head].ns;
    mouse_click_head = (mouse_click_head + 1) % MOUSE_CLICK_MAX;
    return true;
}
```

- [ ] **Step 4: Update the two callers in `src/platform/input.c` to keep the build green**

`mouse_meta()` (`while (host_mouse_click(&ex, &ey, &btn))`) and `text_input_line()` (`while (host_mouse_click(&ex, &ey, &btn))`) both gain a local `uint64_t ns;` and pass `&ns`; neither uses it yet. If the local is unused, pass `NULL` instead to avoid a warning.

- [ ] **Step 5: Build and check**

```bash
cmake --build build
./build/testdrive-enhanced --game-dir Game --check
```
Expected: no warnings; exit 0.

- [ ] **Step 6: Commit**

```bash
git add src/host.h src/host.c src/platform/input.c
git commit -m "Left-button mouse: press timestamp on the click queue"
```

---

### Task 2: Gesture predicates and hit-tests, replacing the old steering-button geometry

**Files:**
- Modify: `src/platform/input.h` (replace the `STEER_BTN_*` / `steer_button_at` block)
- Modify: `src/platform/input.c` (the one `steer_button_at` use in `mouse_drive`)
- Modify: `src/enhanced/enhanced.c` (`draw_steer_buttons` becomes a strip draw over the new cells)
- Test: the scratch harness `/var/folders/h_/rk2gng5d0x99pw7x_3dg6mj40000gn/T/opencode/mouse_ctl_check.c`

**Interfaces:**
- Consumes: nothing from Task 1.
- Produces:
  - `MOUSE_HOLD_MS`, `MOUSE_DOUBLE_MS`, `FIRE_PULSE_POLLS`
  - `bool mouse_is_hold(uint64_t press_ns, uint64_t now_ns)`
  - `bool mouse_is_tap(uint64_t press_ns, uint64_t release_ns)`
  - `bool mouse_is_double(uint64_t press_ns, uint64_t prev_tap_ns)`
  - `enum { CELL_NONE, CELL_STEER_L, CELL_STEER_R, CELL_GEAR_UP, CELL_GEAR_DOWN, CELL_BRAKE, CELL_SOUND }` (consecutive from `CELL_STEER_L`), `DRIVE_CELL_COUNT`
  - `int  drive_cell_at(s16 ex, s16 ey)`, `bool drive_cell_x(int cell, s16 *x0, s16 *x1)`, `int drive_cell_nth(int i)`
  - `enum { MENU_CELL_NONE, MENU_CELL_SOUND, MENU_CELL_BACK }`, `int menu_cell_at(s16 ex, s16 ey)`

- [ ] **Step 1: Write the failing harness cases**

Replace the `steer_button_at` block in `/var/folders/h_/rk2gng5d0x99pw7x_3dg6mj40000gn/T/opencode/mouse_ctl_check.c` with:

```c
    /* gesture timing boundaries */
    assert(mouse_is_hold(0, (uint64_t)MOUSE_HOLD_MS * 1000000u - 1) == false);
    assert(mouse_is_hold(0, (uint64_t)MOUSE_HOLD_MS * 1000000u) == true);
    assert(mouse_is_tap(0, (uint64_t)MOUSE_HOLD_MS * 1000000u - 1) == true);
    assert(mouse_is_tap(0, (uint64_t)MOUSE_HOLD_MS * 1000000u) == false);
    assert(mouse_is_double(1000000000u, 0) == false);                       /* no previous tap */
    assert(mouse_is_double(1000000000u, 1000000000u) == true);              /* same instant */
    assert(mouse_is_double(1000000000u, 1000000000u - (uint64_t)MOUSE_DOUBLE_MS * 1000000u) == true);
    assert(mouse_is_double(1000000000u, 1000000000u - (uint64_t)MOUSE_DOUBLE_MS * 1000000u - 1) == false);

    /* driving strip: cells are half-open, gaps and edges return CELL_NONE */
    assert(drive_cell_at(20, 185)  == CELL_STEER_L);
    assert(drive_cell_at(55, 185)  == CELL_STEER_R);
    assert(drive_cell_at(143, 185) == CELL_GEAR_UP);
    assert(drive_cell_at(175, 185) == CELL_GEAR_DOWN);
    assert(drive_cell_at(250, 185) == CELL_BRAKE);
    assert(drive_cell_at(300, 185) == CELL_SOUND);
    assert(drive_cell_at(8, 176)   == CELL_STEER_L);
    assert(drive_cell_at(38, 185)  == CELL_NONE);       /* x1 exclusive */
    assert(drive_cell_at(39, 185)  == CELL_NONE);       /* gap between the steer cells */
    assert(drive_cell_at(20, 175)  == CELL_NONE);       /* above the strip */
    assert(drive_cell_at(20, 196)  == CELL_NONE);       /* y1 exclusive */
    assert(drive_cell_at(160, 199) == CELL_NONE);       /* below the strip */
    for (int i = 0; i < DRIVE_CELL_COUNT; i++) {
        s16 x0, x1;
        assert(drive_cell_x(drive_cell_nth(i), &x0, &x1));
        assert(drive_cell_at(x0, 185) == drive_cell_nth(i));
        assert(drive_cell_at((s16)(x1 - 1), 185) == drive_cell_nth(i));
        assert(drive_cell_at(x1, 185) == CELL_NONE);
    }

    /* menu cells */
    assert(menu_cell_at(300, 180) == MENU_CELL_SOUND);
    assert(menu_cell_at(250, 180) == MENU_CELL_BACK);
    assert(menu_cell_at(250, 175) == MENU_CELL_NONE);
    assert(menu_cell_at(250, 192) == MENU_CELL_NONE);
    assert(menu_cell_at(100, 180) == MENU_CELL_NONE);
```

- [ ] **Step 2: Run it to verify it fails**

```bash
cc -std=c11 -Wall -Wextra -I src/platform \
   /var/folders/h_/rk2gng5d0x99pw7x_3dg6mj40000gn/T/opencode/mouse_ctl_check.c -o /tmp/mouse_ctl_check
```
Expected: FAIL to compile — the new functions and enum names do not exist.

- [ ] **Step 3: Replace the geometry block in `src/platform/input.h`**

Delete `STEER_BTN_Y0/Y1/LX0/LX1/RX0/RX1` and `steer_button_at`, and add:

```c
/* Left-button gestures (spec 2026-09-16-left-button-mouse-design.md). Pure predicates so the
 * scratch harness can exercise the boundaries. */
#define MOUSE_HOLD_MS    180
#define MOUSE_DOUBLE_MS  300
#define FIRE_PULSE_POLLS   3

static inline bool mouse_is_hold(uint64_t press_ns, uint64_t now_ns)
{ return now_ns - press_ns >= (uint64_t)MOUSE_HOLD_MS * 1000000u; }

static inline bool mouse_is_tap(uint64_t press_ns, uint64_t release_ns)
{ return release_ns - press_ns < (uint64_t)MOUSE_HOLD_MS * 1000000u; }

static inline bool mouse_is_double(uint64_t press_ns, uint64_t prev_tap_ns)
{ return prev_tap_ns != 0 && press_ns >= prev_tap_ns &&
         press_ns - prev_tap_ns <= (uint64_t)MOUSE_DOUBLE_MS * 1000000u; }

/* Driving strip over the dashboard, EGA 320x200. Cells are half-open [x0, x1). */
enum { CELL_NONE = 0, CELL_STEER_L, CELL_STEER_R, CELL_GEAR_UP, CELL_GEAR_DOWN, CELL_BRAKE, CELL_SOUND };
#define DRIVE_CELL_COUNT 6
#define STRIP_Y0 176
#define STRIP_Y1 196

static inline bool drive_cell_x(int cell, s16 *x0, s16 *x1)
{
    switch (cell) {
    case CELL_STEER_L:   *x0 =   8; *x1 =  38; return true;
    case CELL_STEER_R:   *x0 =  40; *x1 =  70; return true;
    case CELL_GEAR_UP:   *x0 = 128; *x1 = 158; return true;
    case CELL_GEAR_DOWN: *x0 = 160; *x1 = 190; return true;
    case CELL_BRAKE:     *x0 = 232; *x1 = 284; return true;
    case CELL_SOUND:     *x0 = 290; *x1 = 312; return true;
    default: return false;
    }
}

static inline int drive_cell_nth(int i) { return CELL_STEER_L + i; }

static inline int drive_cell_at(s16 ex, s16 ey)
{
    if (ey < STRIP_Y0 || ey >= STRIP_Y1) return CELL_NONE;
    for (int i = 0; i < DRIVE_CELL_COUNT; i++) {
        s16 x0, x1;
        drive_cell_x(drive_cell_nth(i), &x0, &x1);
        if (ex >= x0 && ex < x1) return drive_cell_nth(i);
    }
    return CELL_NONE;
}

/* Menu screens carry ♪ and BACK at the right end of the same strip zone; menus and the driving
 * strip never share a screen, so the ranges may coincide. */
enum { MENU_CELL_NONE = 0, MENU_CELL_SOUND, MENU_CELL_BACK };

static inline int menu_cell_at(s16 ex, s16 ey)
{
    if (ey < 176 || ey >= 192) return MENU_CELL_NONE;
    if (ex >= 290 && ex < 312) return MENU_CELL_SOUND;
    if (ex >= 232 && ex < 284) return MENU_CELL_BACK;
    return MENU_CELL_NONE;
}
```

- [ ] **Step 4: Update the two consumers so the game builds**

`src/platform/input.c` `mouse_drive()`: `steer_button_at(s.x, s.y)` becomes a cell test that still only steers:

```c
        int b = drive_cell_at(s.x, s.y);
        if (b == CELL_STEER_L) steer = (s16)-MOUSE_OFF_THRESH;
        else if (b == CELL_STEER_R) steer = (s16)MOUSE_OFF_THRESH;
```

`src/enhanced/enhanced.c`: replace `draw_steer_buttons` with a strip draw that iterates the shared geometry (no labels yet — Task 6 adds them):

```c
static void draw_steer_buttons(u32 *px, int k)
{
    s16 ex, ey;
    int hover = host_mouse_pos(&ex, &ey) ? drive_cell_at(ex, ey) : CELL_NONE;
    bool pressed = (host_mouse_buttons() & 0x01) != 0;
    for (int i = 0; i < DRIVE_CELL_COUNT; i++) {
        int cell = drive_cell_nth(i);
        s16 x0, x1;
        if (!drive_cell_x(cell, &x0, &x1)) continue;
        u32 edge = gfx_palette_rgb(15);
        u32 fill = (cell == hover && pressed) ? gfx_palette_rgb(7) : gfx_palette_rgb(8);
        out_rect(px, k, x0, STRIP_Y0, x1, STRIP_Y1, edge, fill);
        if (cell == CELL_STEER_L) out_arrow(px, k, (x0 + x1) / 2, (STRIP_Y0 + STRIP_Y1) / 2, 4,  1, edge);
        if (cell == CELL_STEER_R) out_arrow(px, k, (x0 + x1) / 2, (STRIP_Y0 + STRIP_Y1) / 2, 4, -1, edge);
        if (cell == CELL_GEAR_UP)   out_arrow(px, k, (x0 + x1) / 2, (STRIP_Y0 + STRIP_Y1) / 2 - 2, 3, -1, edge);
        if (cell == CELL_GEAR_DOWN) out_arrow(px, k, (x0 + x1) / 2, (STRIP_Y0 + STRIP_Y1) / 2 + 2, 3,  1, edge);
    }
}
```

`out_arrow` points left for `dir = +1` and right for `dir = -1` (as shipped). The gear cells need a
vertical arrow, so add one helper in the same style rather than distorting `out_arrow`:

```c
/* Filled triangle pointing up (dir +1) or down (dir -1). */
static void out_arrow_v(u32 *px, int k, int cx, int cy, int len, int dir, u32 c)
{
    int ow = 320 * k;
    for (int i = 0; i < len * k; i++)
        for (int j = -i; j <= i; j++) {
            int x = cx * k + j, y = cy * k - dir * i;
            if (x >= 0 && x < ow && y >= 0 && y < 200 * k) px[(size_t)y * ow + x] = c;
        }
}
```

and use `out_arrow_v(..., -1, ...)` for `▲` and `out_arrow_v(..., +1, ...)` for `▼` at the cell
centre. No judgement call is left: the windowed pass in Task 6 only confirms legibility.

Also update `ov_dirty()`: its hover comparison uses `steer_button_at` today; use `drive_cell_at`.

- [ ] **Step 5: Run the harness and build**

```bash
cc -std=c11 -Wall -Wextra -I src/platform \
   /var/folders/h_/rk2gng5d0x99pw7x_3dg6mj40000gn/T/opencode/mouse_ctl_check.c \
   -o /tmp/mouse_ctl_check && /tmp/mouse_ctl_check
cmake --build build
./build/testdrive-enhanced --game-dir Game --check
```
Expected: `mouse control policy: ok`; no warnings; exit 0.

- [ ] **Step 6: Commit**

```bash
git add src/platform/input.h src/platform/input.c src/enhanced/enhanced.c
git commit -m "Left-button mouse: gesture predicates and strip geometry"
```

---

### Task 3: Driving gestures — tap, hold, double click

**Files:**
- Modify: `src/platform/input.c` (mouse state, `mouse_meta`, `mouse_drive`)

**Interfaces:**
- Consumes: Task 1's timestamps, Task 2's predicates and cells.
- Produces: the driving map — hold road = accelerate, hold ◀ ▶ = accelerate+turn, hold BRAKE = brake, tap ▲ ▼ = gear, tap ♪ = sound, tap road = fire (direction 0), double click road = pause.

- [ ] **Step 1: Replace the mouse state and helpers**

Delete `mouse_gear_dir`, `mouse_gear_polls`, and `mouse_meta`'s middle/X1 branches. Add:

```c
/* PORT: left-button gestures (spec 2026-09-16-left-button-mouse-design.md). A press latches its
 * target until release; released before MOUSE_HOLD_MS it is a tap, still pressed at MOUSE_HOLD_MS
 * it is a hold; two taps within MOUSE_DOUBLE_MS on the road pause. */
static uint64_t press_ns;        /* 0 = no press in progress */
static int      press_cell;      /* cell latched at press time, CELL_NONE = road */
static bool     press_used;      /* a discrete action already fired for this press */
static uint64_t prev_tap_ns;     /* release time of the previous tap */
static uint64_t pending_fire_ns; /* road tap's fire, waiting out the double-click window */
static u8       fire_polls;
static u8       gear_polls;
static s16      gear_dir;

static void mouse_sound_toggle(void)
{
    if (DSB(DS_snd_flags) & 4) {
        DSB(DS_snd_flags) &= 3;
    } else {
        DSB(DS_snd_flags) |= 4;
        if (DSB(DS_snd_flags) & 2) DSB(DS_snd_playing) |= 2;
    }
}

static void mouse_pause(void)
{
    if (DSB(DS_modal_pause) != 0) return;
    u16 dx = 0;
    DSB(DS_modal_pause) = 1;
    getkey_wait_dx(&dx);
    DSB(DS_modal_pause) = 0;
}

static void mouse_gestures(void)
{
    uint64_t now = host_time_ns();
    s16 ex, ey;
    u8 btn;
    uint64_t ns;
    while (host_mouse_click(&ex, &ey, &btn, &ns)) {
        if (btn != 0x01) continue;                       /* left button only */
        bool dbl = mouse_is_double(ns, prev_tap_ns);
        int cell = drive_cell_at(ex, ey);
        if (dbl) {
            pending_fire_ns = 0;                         /* the first tap was half of a double */
            if (cell == CELL_NONE) {
                mouse_pause();
                prev_tap_ns = 0;
                press_ns = 0;                            /* the pause consumed the pointer */
                continue;
            }
        }
        press_ns = ns ? ns : now;
        press_cell = cell;
        press_used = false;
        if (cell == CELL_GEAR_UP)        { gear_dir =  1; gear_polls = FIRE_PULSE_POLLS; press_used = true; }
        else if (cell == CELL_GEAR_DOWN) { gear_dir = -1; gear_polls = FIRE_PULSE_POLLS; press_used = true; }
        else if (cell == CELL_SOUND)     { mouse_sound_toggle(); press_used = true; }
    }
    if (press_ns != 0 && (host_mouse_buttons() & 0x01) == 0) {
        if (mouse_is_tap(press_ns, now) && press_cell == CELL_NONE && !press_used)
            pending_fire_ns = now;                       /* fire once the double window passes */
        prev_tap_ns = now;
        press_ns = 0;
    }
    if (pending_fire_ns != 0 && now - pending_fire_ns >= (uint64_t)MOUSE_DOUBLE_MS * 1000000u) {
        pending_fire_ns = 0;
        fire_polls = FIRE_PULSE_POLLS;
    }
}
```

- [ ] **Step 2: Replace `mouse_drive`**

```c
static u16 mouse_drive(void)
{
    MouseState s = mouse_poll();
    mouse_gestures();

    /* PORT: fire is emitted neutral (direction 0) — simulation ignores it for gear selection, so a
     * tap can never shift a gear while still satisfying wait_fire_button's bit 0x10. */
    if (fire_polls > 0) { fire_polls--; return 0x10u; }
    if (gear_polls > 0) {                                /* deliberate ▲ ▼ pulse: fire + direction */
        gear_polls--;
        return (u16)((gear_dir > 0 ? 1u : 5u) | 0x10u);
    }

    s16 steer = s.off;                                   /* motion steering, as before */
    bool up = false, down = false;
    bool holding = press_ns != 0 && (s.held & 0x01) != 0 && mouse_is_hold(press_ns, host_time_ns());
    if (holding) {
        switch (press_cell) {
        case CELL_STEER_L: steer = (s16)-MOUSE_OFF_THRESH; up = true; break;   /* accelerate + turn */
        case CELL_STEER_R: steer = (s16) MOUSE_OFF_THRESH; up = true; break;
        case CELL_NONE:    up = true; break;                                   /* accelerate */
        case CELL_BRAKE:   down = true; break;
        default: break;                                                        /* gear/sound: no hold */
        }
    }
    return mouse_direction(steer, up, down);
}
```

- [ ] **Step 3: Remove the deleted bindings**

- `mouse_poll()` no longer needs `wheel`; keep the field but stop reading it for gear (the wheel must do nothing).
- Confirm nothing else references the removed `mouse_gear_*`, `mouse_meta` middle (bit `0x04`) or X1 (bit `0x08`) branches. `mouse_meta` is now only used by the menu path; if it becomes unused after Task 4, delete it there.

- [ ] **Step 4: Build and check**

```bash
cmake --build build
./build/testdrive-enhanced --game-dir Game --check
```
Expected: no warnings; exit 0.

- [ ] **Step 5: Manual driving check**

Windowed run: hold the road to accelerate (steering by motion still works while held), hold ◀ / ▶ to steer, hold BRAKE to brake, tap ▲ / ▼ one gear each, tap ♪ to mute, tap the road to dismiss a crash/GAME OVER wait, double click the road to pause and click again to resume. Confirm the right button, wheel, middle and side buttons do nothing.

- [ ] **Step 6: Commit**

```bash
git add src/platform/input.c
git commit -m "Left-button mouse: driving tap/hold/double gestures"
```

---

### Task 4: Menus with the left button only

**Files:**
- Modify: `src/platform/input.c` (`mouse_menu`, `mouse_meta`, `text_input_line`'s sibling helpers)

**Interfaces:**
- Consumes: `menu_cell_at`, `mouse_sound_toggle` (Task 3), gfx text primitives.
- Produces: menu click = Enter, flick = selection (unchanged), `BACK` cell = Esc, `SND` cell = sound; the two cells drawn on the EGA screen.

- [ ] **Step 1: Rewrite `mouse_menu`**

```c
/* PORT: menus with the left button only — a click elsewhere is Enter, the BACK cell is Esc and the
 * SND cell toggles sound. The right-click binding is gone. The cells are redrawn idempotently each
 * poll (menu screens are static, so the same bytes are rewritten). */
static void mouse_menu_cells(void)
{
    s16 ex, ey;
    int hover = host_mouse_pos(&ex, &ey) ? menu_cell_at(ex, ey) : MENU_CELL_NONE;
    gfx_set_text_colours(0x0F, 0);
    draw_rect_outline(232, 172, 284, 190, 0x0F);
    draw_rect_outline(290, 172, 312, 190, 0x0F);
    if (hover == MENU_CELL_BACK)  gfx_fill_rect(233, 173, 50, 16, 0x08);
    if (hover == MENU_CELL_SOUND) gfx_fill_rect(291, 173, 20, 16, 0x08);
    gfx_draw_text("BACK", 244, 178);
    gfx_draw_text("SND", 295, 178);
}

static u16 mouse_menu(void)
{
    mouse_menu_cells();
    s16 ex, ey;
    u8 btn;
    uint64_t ns;
    while (host_mouse_click(&ex, &ey, &btn, &ns)) {
        if (btn != 0x01) continue;                       /* left button only */
        switch (menu_cell_at(ex, ey)) {
        case MENU_CELL_SOUND: mouse_sound_toggle(); break;
        case MENU_CELL_BACK:  return 0x001B;             /* Esc */
        default:              return 0x000D;             /* Enter */
        }
    }
    MouseState s = mouse_poll();                         /* one poll per call: it consumes the deltas */
    u16 dir = mouse_direction(0, s.off_y <= -MOUSE_OFF_THRESH, s.off_y >= MOUSE_OFF_THRESH);
    u16 r = DSW((u16)(DS_joy_menu_scan + (u16)mouse_joy_nibble(dir) * 2));
    if (r == DSW(DS_joy_menu_last)) return 0;
    DSW(DS_joy_menu_last) = r;
    return r;
}
```

The vertical-flick selection keeps today's behaviour exactly, including the `DS_joy_menu_last`
dedupe. Poll once, as above — calling `mouse_poll()` twice would decay the offsets twice and
consume the motion deltas in the wrong place.

- [ ] **Step 2: Delete the right-click Esc branch and `mouse_meta` if unused**

`mouse_meta`'s only remaining job was left→Enter/right→Esc; that is now in `mouse_menu` and the name keyboard. Remove `mouse_meta` entirely if nothing else calls it (check with `rg -n "mouse_meta" src/`), and remove the now-unused `menus`/`pending` plumbing.

- [ ] **Step 3: Build and check**

```bash
cmake --build build
./build/testdrive-enhanced --game-dir Game --check
```
Expected: no warnings; exit 0.

- [ ] **Step 4: Manual menu check**

Windowed run: click selects a menu item, moving the mouse up/down moves the selection, clicking BACK leaves the menu, clicking SND toggles sound. Confirm a right click does nothing, and that a click while driving does not leak an Enter into the next menu.

- [ ] **Step 5: Commit**

```bash
git add src/platform/input.c
git commit -m "Left-button mouse: menu enter, back and sound cells"
```

---

### Task 5: Name keyboard without the right button

**Files:**
- Modify: `src/platform/input.c` (`osk_label`, `osk_draw`, `osk_hit`, `text_input_line`)

**Interfaces:**
- Consumes: nothing from Task 4.
- Produces: a `CANCEL` cell as grid index 31; clicking it clears the name and commits (the caller records nothing for an empty name). The right-click cancel is removed.

- [ ] **Step 1: Extend the grid**

`osk_label`'s table becomes 32 entries, adding `"CANCEL"` as index 31 (row 4, column 3 of the 7×5 grid; indices 32..34 stay unused):

```c
enum { OSK_COLS = 7, OSK_ROWS = 5, OSK_CELLS = 32,
       OSK_X0 = 20, OSK_Y0 = 60, OSK_CW = 40, OSK_CH = 24 };
```

The `CANCEL` label is wider than a letter cell; draw it at `x0 + 2` and accept the overflow into the two empty cells, or reduce the drawn font by starting it one pixel earlier. The manual check in Step 4 decides; keep `osk_hit` unchanged so the cell is 40×24 like the rest.

- [ ] **Step 2: Replace the right-click cancel with the cell**

In `text_input_line`'s mouse branch, drop `if (btn & 0x02) { buf[0] = 0; goto done; }` and `if (btn != 0x01) continue;`, then handle the new cell:

```c
                    } else if (cell == 29) {               /* DEL */
                        if (pos != 0) { pos--; buf[pos] = ' '; }
                    } else if (cell == 30) {               /* OK */
                        goto done;
                    } else {                               /* CANCEL (31): clear and commit */
                        buf[0] = 0;
                        goto done;
                    }
```

- [ ] **Step 3: Build and check**

```bash
cmake --build build
./build/testdrive-enhanced --game-dir Game --check
```
Expected: no warnings; exit 0.

- [ ] **Step 4: Manual name-entry check**

Windowed run at a qualifying score: click letters, `<` `>` `DEL` `OK`, and `CANCEL` (which must leave no new table entry). Confirm a right click does nothing and that the score table is not skipped after `OK`.

- [ ] **Step 5: Commit**

```bash
git add src/platform/input.c
git commit -m "Left-button mouse: name keyboard cancel cell"
```

---

### Task 6: Strip labels

**Files:**
- Modify: `src/enhanced/enhanced.c` (a small letter glyph table next to `DIGITS`, and `draw_steer_buttons`)

**Interfaces:**
- Consumes: `DIGITS`, `block`, `blend`, `draw_cell_x` geometry (Task 2).
- Produces: `BRAKE` and `SND` text inside their cells.

- [ ] **Step 1: Add the letter glyphs**

The existing font is 5 bits wide, 7 rows, one `u8` per row, drawn with `block(px, k, x, y, colour)` (see `DIGITS` and `draw_timer`). Add only the letters the strip needs, same shape:

```c
/* 5x7 uppercase glyphs, rows top to bottom, bit 0x10 = leftmost pixel (same shape as DIGITS). */
static const u8 LETTERS[8][7] = {
    /* B */ { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E },
    /* R */ { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 },
    /* A */ { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 },
    /* K */ { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 },
    /* E */ { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F },
    /* S */ { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E },
    /* N */ { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 },
    /* D */ { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E },
};

static void draw_letters(u32 *px, int k, const char *s, int x, int y, u32 c)
{
    for (int i = 0; s[i]; i++) {
        int gi = s[i] == 'B' ? 0 : s[i] == 'R' ? 1 : s[i] == 'A' ? 2 : s[i] == 'K' ? 3 :
                 s[i] == 'E' ? 4 : s[i] == 'S' ? 5 : s[i] == 'N' ? 6 : s[i] == 'D' ? 7 : -1;
        if (gi < 0) continue;
        for (int row = 0; row < 7; row++)
            for (int col = 0; col < 5; col++)
                if (LETTERS[gi][row] & (0x10 >> col)) block(px, k, x + i * 6 + col, y + row, c);
    }
}
```

- [ ] **Step 2: Label the cells**

In `draw_steer_buttons`, after the rect for each cell:

```c
        u32 ink = gfx_palette_rgb(15);
        if (cell == CELL_BRAKE)     draw_letters(px, k, "BRAKE", x0 + 6, STRIP_Y0 + 3, ink);
        else if (cell == CELL_SOUND) draw_letters(px, k, "SND",  x0 + 4, STRIP_Y0 + 3, ink);
```

Avoid `block` writes outside the frame: `block` is the same helper `draw_timer` uses inside the overlay, so the cells' coordinates (all inside 8..312 × 176..196) are safe.

- [ ] **Step 3: Build and check**

```bash
cmake --build build
./build/testdrive-enhanced --game-dir Game --check
```
Expected: no warnings; exit 0.

- [ ] **Step 4: Manual strip check**

Windowed run: the strip shows ◀ ▶ ▲ ▼ BRAKE SND; hovering highlights the cell, pressing and holding highlights it while steering, and the labels are legible at `--res-scale 4` and at `--res-scale 8`.

- [ ] **Step 5: Commit**

```bash
git add src/enhanced/enhanced.c
git commit -m "Left-button mouse: strip labels"
```

---

### Task 7: Document the controls

**Files:**
- Modify: `README.md` (Controls section)

- [ ] **Step 1: Rewrite the mouse section**

```markdown
### Mouse (left button only)

* Steer by moving the mouse left and right; it springs back to straight when you stop.
* Press and hold anywhere on the road to accelerate.
* Hold the on-screen ◀ / ▶ to steer that way (accelerate and turn), hold BRAKE to brake.
* Tap ▲ / ▼ for one gear each; tap SND to toggle sound.
* Tap the road to dismiss the crash / GAME OVER / ending waits; double click the road to pause.
* Menus: click to select, move the mouse up and down to move the selection, click BACK to go back.
* High-score name entry: click letters on the on-screen keyboard, `DEL` deletes, `OK` commits,
  `CANCEL` records nothing.
* The right button, wheel, middle and side buttons do nothing; the cursor is never captured.
```

Keep the Keyboard / gamepad bullets as they are.

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "Left-button mouse: document the controls"
```

---

## Self-Review

**Spec coverage**

| Spec section | Task |
|---|---|
| Gesture model (latched press, tap/hold/double, constants) | 2, 3 |
| Driving map (gas, steer, brake, gear, sound, fire, pause) | 3, 6 |
| Menu map (Enter, flick, BACK, SND cells) | 4 |
| Name entry CANCEL cell | 5 |
| Safety invariant (fire neutral, delayed past the double window) | 3 |
| Chrome: strip geometry, menu cells, shared hit-tests | 2, 4, 6 |
| Ownership (host timestamp; input policy; overlay drawing) | 1, 2, 3, 6 |
| Removed bindings | 3, 4, 5 |
| Verification (harness asserts, build, `--check`, manual) | 2, each task |

**Placeholder scan:** no TBDs; every code step carries code. Steps that must be judged by eye (gear
arrow orientation, `CANCEL` label width, strip legibility) say so and name the constant or check
that decides them.

**Type consistency:** `host_mouse_click(s16*, s16*, u8*, uint64_t*)` is declared in Task 1 and used
with that shape in Tasks 3 and 4 (and the name keyboard in Task 5). `drive_cell_at`,
`drive_cell_x`, `drive_cell_nth`, `CELL_*`, `menu_cell_at`, `MENU_CELL_*`, `mouse_is_hold/tap/double`
are defined in Task 2 and used with those exact names in Tasks 3, 4 and 6. `mouse_sound_toggle` and
`mouse_pause` are defined in Task 3 and used in Tasks 3 and 4.

**Known follow-ups:** `mouse_meta` is removed in Task 4 once its callers are reworked; the wheel
field in `MouseState` becomes unused in Task 3 and should be dropped there if no warning appears
either way.

## Execution Handoff

Two execution options:

1. **Subagent-Driven (recommended)** — a fresh subagent per task, review between tasks.
2. **Inline Execution** — execute the tasks in this session with checkpoints.
