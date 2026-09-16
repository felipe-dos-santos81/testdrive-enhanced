#pragma once
/* Keyboard/joystick input — port of TDEGA 0x5C24, 0x67DD..0x6939, 0x7864, 0x8AFD, 0x92A8, 0x95B0, 0x95D9,
 * 0xA12F (port/spec/platform.md §4.5, game_flow.md for the name editor). Keys come from the host's BIOS
 * keyboard queue (host_kbd_*); the joystick is emulated from host_joy_read(). */
#include "../mem.h"

u16  input_poll_drive(void);        /* 0x5C24 direction/fire/hotkeys for driving and "press fire" waits */
u16  getkey(void);                  /* 0x67E8 non-blocking menu key (dispatch on input_mode DS:6422) */
u16  getkey_wait(void);             /* 0x67DD loops getkey until nonzero (pumps the host) */
void kbd_flush(void);               /* 0x6939 */
void input_set_mode(u16 mode);      /* 0x8AFD */
u16  getkey_until_deadline(void);   /* 0x7864 getkey until the set_deadline() deadline, else 0 */
int  menu_key(void);                /* 0x95B0 0 -> -1, Esc -> 1, else the key */
int  toupper_c(int c);              /* 0x95D9 */
u8   joy_read(void);                /* 0xA12F direction + button bits (emulated from the gamepad) */

/* 0x92A8 high-score name line editor (cursor glyph via draw_glyph). Returns the original's result code;
 * see game_flow.md scores_enter_name and platform.md §4.8 note. */
int  text_input_line(char *buf, int maxlen, s16 x, s16 y, u16 timeout);

/* Mouse control policy (docs/superpowers/specs/2026-09-16-mouse-control-design.md). Pure integer
 * arithmetic, host-free, so the scratch self-check can exercise it without linking the game.
 * Steering is relative with auto-centre: a held angle cannot be parked, it springs back when the
 * mouse is idle. MOUSE_DECAY_PER_MS is the single feel knob; thresholds are in render pixels, so a
 * very different --res-scale changes the feel. */
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

/* PORT: on-screen driving strip hit-test (spec 2026-09-16-left-button-mouse-design.md); the original
 * has no mouse cells. */
/* Driving strip over the dashboard, EGA 320x200. Cells are half-open [x0, x1). */
enum { CELL_NONE = 0, CELL_STEER_L, CELL_STEER_R, CELL_GEAR_UP, CELL_GEAR_DOWN, CELL_BRAKE, CELL_SOUND };
#define DRIVE_CELL_COUNT 6
#define STRIP_Y0 176
#define STRIP_Y1 196

/* PORT: EGA x band of one strip cell; false for CELL_NONE. */
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

/* PORT: i-th strip cell in drawing order, for looping over the buttons. */
static inline int drive_cell_nth(int i) { return CELL_STEER_L + i; }

/* PORT: hit-test a pointer position against the strip; CELL_NONE outside it. */
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

/* PORT: menu sound/back cells (spec 2026-09-16-left-button-mouse-design.md); the original has no
 * mouse cells. */
/* Menu screens carry ♪ and BACK at the right end of the same strip zone; menus and the driving
 * strip never share a screen, so the ranges may coincide. */
enum { MENU_CELL_NONE = 0, MENU_CELL_SOUND, MENU_CELL_BACK };

/* PORT: hit-test a pointer position against the menu cells. */
static inline int menu_cell_at(s16 ex, s16 ey)
{
    if (ey < 176 || ey >= 192) return MENU_CELL_NONE;
    if (ex >= 290 && ex < 312) return MENU_CELL_SOUND;
    if (ex >= 232 && ex < 284) return MENU_CELL_BACK;
    return MENU_CELL_NONE;
}
