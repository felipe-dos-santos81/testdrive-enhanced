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
