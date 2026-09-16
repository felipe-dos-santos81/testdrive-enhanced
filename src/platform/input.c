/* Keyboard/joystick input — port of TDEGA 0x5C24, 0x67DD..0x6939, 0x7864, 0x8AFD, 0x92A8, 0x95B0, 0x95D9,
 * 0x95F4 (reduced), 0xA12F (port/spec/platform.md §4.5, §5; game_flow.md scores_enter_name).
 *
 * Register note: after a Ctrl hotkey or a pause, 0x5C24 returns 0xFF00 | DH where DX is whatever the
 * called routines left in it (the drained key, the last key read by the pause's getkey_wait, or 0x201
 * from the joystick reader). The internal helpers below thread that DX value explicitly. */
#include "input.h"

#include "../host.h"
#include "../symbols.h"
#include "gfx.h"
#include "timer.h"

static void joy_calibrate_screen(void);

/* INT 16h AH=00h repeated while AH=01h reports a key: returns the LAST buffered key. */
static u16 kbd_drain_last(void)
{
    u16 k = 0;
    do {
        host_kbd_read(&k);
    } while (host_kbd_peek(NULL));
    return k;
}

/* 0xA12F joy_read with its DX side effect (DX = 201h when the reader runs). */
static u8 joy_read_dx(u16 *dx)
{
    if (DSB(DS_joy_enabled) == 0) return 0;
    *dx = 0x201;
    return joy_read();
}

/* 0xA12F joy_read */
u8 joy_read(void)
{
    if (DSB(DS_joy_enabled) == 0) return 0;
    /* PORT: port 201h one-shot timing and the adaptive min/max calibration (DS:6A3E..6A5A) are replaced by
     * fixed thresholds on the host gamepad (platform.md §5). The returned bit format is kept:
     * 1 up, 2 down, 4 right, 8 left, 0x10 button A, 0x20 button B. No gamepad -> no bits. */
    u8 res = 0;
    s16 x, y;
    u8 buttons;
    if (host_joy_read(&x, &y, &buttons)) {
        if (x < -16384) res |= 8;
        else if (x >= 16384) res |= 4;
        if (y < -16384) res |= 1;
        else if (y >= 16384) res |= 2;
        if (buttons & 1) res |= 0x10;
        if (buttons & 2) res |= 0x20;
    }
    DSB(DS_joy_result) = res;
    return res;
}

/* 0x6846 getkey_kbd_ctrl: drain the buffer, keep the last key, handle Ctrl hotkeys. */
static u16 getkey_kbd_ctrl(u16 *dx);
static u16 getkey_wait_dx(u16 *dx);

/* ---------------------------------------------------------------- mouse (PORT) */

/* PORT: mouse-only control (docs/superpowers/specs/2026-09-16-left-button-mouse-design.md). Steering
 * is relative with auto-centre; the left button drives by gesture (hold = accelerate / steer / brake,
 * tap = gear / sound / fire, double click on the road = pause). Right button, wheel, middle and side
 * buttons do nothing. Buttons held: bit0 left, bit1 right, bit2 middle, bit3 X1, bit4 X2. */
typedef struct { s16 off; s16 off_y; u8 held; s16 wheel; s16 x, y; } MouseState;

static s16 mouse_off;
static s16 mouse_off_y;
static uint64_t mouse_off_ns;

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

/* PORT: on-screen keyboard owns clicks while set; getkey()/mouse_menu() leave them queued for
 * text_input_line instead of converting them into Enter/Esc. */
static bool mouse_ui_capture;

static MouseState mouse_poll(void)
{
    s16 dx, dy; u8 held; s16 wheel;
    host_mouse_read(&dx, &dy, &held, &wheel);
    uint64_t now = host_time_ns();
    if (mouse_off_ns == 0) mouse_off_ns = now;
    u32 dt = (u32)((now - mouse_off_ns) / 1000000u);
    mouse_off_ns = now;
    if (dt > 250u) dt = 250u;                               /* cap after a stall */
    mouse_off = mouse_steer_step(mouse_off, dx, dt);
    mouse_off_y = mouse_steer_step(mouse_off_y, dy, dt);
    MouseState s = { mouse_off, mouse_off_y, held, wheel, -1, -1 };
    host_mouse_pos(&s.x, &s.y);                             /* for the on-screen steering buttons */
    return s;
}

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
        bool tap = mouse_is_tap(press_ns, now);
        if (tap && press_cell == CELL_NONE && !press_used)
            pending_fire_ns = now;                       /* fire once the double window passes */
        if (tap) prev_tap_ns = now;                      /* only a tap can start a double click */
        press_ns = 0;
    }
    if (pending_fire_ns != 0 && now - pending_fire_ns >= (uint64_t)MOUSE_DOUBLE_MS * 1000000u) {
        pending_fire_ns = 0;
        fire_polls = FIRE_PULSE_POLLS;
    }
}

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

static u16 getkey_kbd_ctrl(u16 *dx)
{
    u16 k = kbd_drain_last();
    *dx = k;
    u8 al = (u8)k;
    if ((s8)al >= 0x20) return al;              /* ASCII, AH = 0 */
    if (al == 0) return k;                      /* extended: scan << 8 */
    /* PORT: AL >= 0x80 indexes past the CS:686B jump table in the original (crash); ignore the key. */
    if (al >= 0x80) return 0;
    switch (al) {                               /* CS:686B */
    case 0x0A:                                  /* Ctrl-J: joystick on (no modal_pause here) */
        DSB(DS_joy_enabled) = 1;
        if (DSB(DS_joy_calibrated) == 0) joy_calibrate_screen();
        return 0;
    case 0x0B:                                  /* Ctrl-K: keyboard only */
        DSB(DS_joy_enabled) = 0;
        return 0;
    case 0x10:                                  /* Ctrl-P: pause */
        DSB(DS_modal_pause) = 1;
        getkey_wait_dx(dx);
        DSB(DS_modal_pause) = 0;
        return 0;
    case 0x11:                                  /* Ctrl-Q: sound off */
        DSB(DS_snd_flags) &= 3;
        return 0;
    case 0x13:                                  /* Ctrl-S: sound on, resume loop at the stale pointer */
        DSB(DS_snd_flags) |= 4;
        if (DSB(DS_snd_flags) & 2) DSB(DS_snd_playing) |= 2;
        return 0;
    default:                                    /* Enter 0x0D, BS 0x08, Esc 0x1B, ... */
        return al;
    }
}

/* 0x67F8 getkey_kbd (mode 0) */
static u16 getkey_kbd(void)
{
    u16 k;
    if (!host_kbd_peek(NULL)) return 0;
    host_kbd_read(&k);
    if ((u8)k != 0) k &= 0x00FF;
    return k;
}

/* 0x680E getkey_kbd_joy_level (mode 2, unused) */
static u16 getkey_kbd_joy_level(u16 *dx)
{
    DSB(DS_kbd_shift_btn) = host_kbd_shift_flags() & 3;
    if (host_kbd_peek(NULL)) return getkey_kbd_ctrl(dx);
    u16 j = joy_read_dx(dx);
    if (j == 0) return 0;
    u8 hi = (u8)(j >> 4);
    DSB(DS_kbd_shift_btn) |= hi;
    if (hi) return 0x000D;
    return DSW((u16)(DS_joy_menu_scan + (j & 0x0F) * 2));
}

/* 0x68F4 getkey_kbd_joy_edge (mode 4, the one main selects) */
static u16 getkey_kbd_joy_edge(u16 *dx)
{
    DSB(DS_kbd_shift_btn) = host_kbd_shift_flags() & 3;
    if (host_kbd_peek(NULL)) return getkey_kbd_ctrl(dx);
    u16 j = joy_read_dx(dx);
    u8 hi = (u8)(j >> 4);
    DSB(DS_kbd_shift_btn) |= hi;
    u16 r = hi ? 0x000D : DSW((u16)(DS_joy_menu_scan + (j & 0x0F) * 2));
    if (r == DSW(DS_joy_menu_last)) return 0;
    DSW(DS_joy_menu_last) = r;
    return r;
}

/* PORT: menus with the left button only — a click elsewhere is Enter, the BACK cell is Esc and the
 * SND cell toggles sound. The right-click binding is gone. The cells are redrawn idempotently each
 * poll (menu screens are static, so the same bytes are rewritten). */
static void mouse_menu_cells(void)
{
    s16 ex, ey;
    int hover = host_mouse_pos(&ex, &ey) ? menu_cell_at(ex, ey) : MENU_CELL_NONE;
    /* Redraw both cells in full every poll. Repainting only the hovered cell leaves the previous
     * highlight behind (nothing else repaints the band) on static menu screens. The drawn box is
     * exactly the hit band, so nothing is clickable but invisible. */
    gfx_set_text_colours(0x0F, 0);
    gfx_fill_rect(232, 176, 52, 16, 0x00);
    gfx_fill_rect(290, 176, 22, 16, 0x00);
    if (hover == MENU_CELL_BACK)  gfx_fill_rect(232, 176, 52, 16, 0x08);
    if (hover == MENU_CELL_SOUND) gfx_fill_rect(290, 176, 22, 16, 0x08);
    draw_rect_outline(232, 176, 283, 191, 0x0F);
    draw_rect_outline(290, 176, 311, 191, 0x0F);
    gfx_draw_text("BACK", 244, 181);
    gfx_draw_text("SND", 295, 181);
}

static u16 mouse_menu(void)
{
    if (mouse_ui_capture) return 0;                         /* PORT: on-screen keyboard owns clicks */
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

u16 getkey(void)
{
    u16 dx = 0;
    return getkey_dx(&dx);
}

/* 0x67DD */
static u16 getkey_wait_dx(u16 *dx)
{
    u16 k;
    while ((k = getkey_dx(dx)) == 0) host_pump();
    return k;
}

u16 getkey_wait(void)
{
    u16 dx = 0;
    return getkey_wait_dx(&dx);
}

/* 0x6939 */
void kbd_flush(void)
{
    host_kbd_flush();
}

/* 0x8AFD */
void input_set_mode(u16 mode)
{
    DSW(DS_input_mode) = mode;
}

/* PORT: held-key driving controls. Direction codes as in the original (1 up, 2 up-right, 3 right,
 * 4 down-right, 5 down, 6 down-left, 7 left, 8 up-left); A/Z held = fire + up/down (clutch + shift). */
static bool is_held_key(u16 key)
{
    u8 al = (u8)key, scan = (u8)(key >> 8);
    if (al == 'a' || al == 'A' || al == 'z' || al == 'Z') return true;
    return al == 0 && scan >= 0x47 && scan <= 0x51;
}

/* Keys buffered since the last poll count as held for this tick, so a tap shorter than one 80 ms tick
 * still registers, as a buffered keystroke did in the original. */
enum { TAP_UP = 1, TAP_DOWN = 2, TAP_LEFT = 4, TAP_RIGHT = 8, TAP_A = 16, TAP_Z = 32 };

static u8 tap_bits(u16 key)
{
    u8 al = (u8)key;
    if (al == 'a' || al == 'A') return TAP_A;
    if (al == 'z' || al == 'Z') return TAP_Z;
    switch ((u8)(key >> 8)) {
    case 0x47: return TAP_UP | TAP_LEFT;
    case 0x48: return TAP_UP;
    case 0x49: return TAP_UP | TAP_RIGHT;
    case 0x4B: return TAP_LEFT;
    case 0x4D: return TAP_RIGHT;
    case 0x4F: return TAP_DOWN | TAP_LEFT;
    case 0x50: return TAP_DOWN;
    case 0x51: return TAP_DOWN | TAP_RIGHT;
    default:   return 0;
    }
}

static u16 held_controls(u8 taps)
{
    if (host_xt_key_down(0x1E) || (taps & TAP_A)) return 0x11;
    if (host_xt_key_down(0x2C) || (taps & TAP_Z)) return 0x15;
    bool up    = host_xt_key_down(0x48) || host_xt_key_down(0x47) || host_xt_key_down(0x49) || (taps & TAP_UP);
    bool down  = host_xt_key_down(0x50) || host_xt_key_down(0x4F) || host_xt_key_down(0x51) || (taps & TAP_DOWN);
    bool left  = host_xt_key_down(0x4B) || host_xt_key_down(0x47) || host_xt_key_down(0x4F) || (taps & TAP_LEFT);
    bool right = host_xt_key_down(0x4D) || host_xt_key_down(0x49) || host_xt_key_down(0x51) || (taps & TAP_RIGHT);
    if (up && down) up = down = false;
    if (left && right) left = right = false;
    if (up)   return left ? 8 : right ? 2 : 1;
    if (down) return left ? 6 : right ? 4 : 5;
    if (left) return 7;
    if (right) return 3;
    return 0;
}

static u16 input_poll_drive_bios(void);
static u16 drive_key(u16 dx);

/* 0x5C24 input_poll_drive */
u16 input_poll_drive(void)
{
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

static u16 input_poll_drive_bios(void)
{
    if (!host_kbd_peek(NULL)) {
        u16 dx = 0;
        u16 j = joy_read_dx(&dx);
        u16 r = DSB((u16)(DS_joy_dir_map + (j & 0x0F)));
        if (j & 0x30) r |= 0x10;
        return r;
    }
    return drive_key(kbd_drain_last());
}

/* 0x5C24 continued: decode the last buffered key (DX) */
static u16 drive_key(u16 dx)
{
    u8 al = (u8)dx;
    if (al == 'p' || al == 'P') goto pause;
    if (al == 'a' || al == 'A') return 0x11;                /* up + fire */
    if (al == 'z' || al == 'Z') return 0x15;                /* down + fire */
    if ((s8)al >= 0x20) {
        if (al >= '0' && al <= '9') return DSB((u16)(DS_digit_dir_map + (al - '0')));
        return (u16)(0xFF00 | al);
    }
    if (al == 0) {                                          /* extended key */
        s16 bx = (s16)((dx >> 8) - 0x46);
        if (bx <= 0 || bx >= 12) return 0;
        return DSB((u16)(DS_ext_scan_dir_map + bx));
    }
    /* PORT: AL >= 0x80 indexes past the CS:5CC5 jump table in the original (crash); ignore the key. */
    if (al >= 0x80) return 0;
    switch (al) {                                           /* CS:5CC5 */
    case 0x0A: {                                            /* Ctrl-J */
        DSB(DS_joy_enabled) = 1;
        if (DSB(DS_joy_calibrated) == 0) {
            DSB(DS_modal_pause) = 1;
            joy_calibrate_screen();                         /* push dx / pop dx around the call */
            DSB(DS_modal_pause) = 0;
        }
        break;
    }
    case 0x0B:                                              /* Ctrl-K */
        DSB(DS_joy_enabled) = 0;
        break;
    case 0x10:                                              /* Ctrl-P */
    pause:
        DSB(DS_modal_pause) = 1;
        getkey_wait_dx(&dx);                                /* clobbers DX like the original */
        DSB(DS_modal_pause) = 0;
        break;
    case 0x11:                                              /* Ctrl-Q: sound off */
        DSB(DS_snd_flags) &= 3;
        break;
    case 0x13:                                              /* Ctrl-S: sound on */
        DSB(DS_snd_flags) |= 4;
        if (DSB(DS_snd_flags) & 2) DSB(DS_snd_playing) |= 2;
        break;
    case 0x1B:                                              /* Esc */
        return 0xFFFF;
    default:
        break;
    }
    return (u16)(0xFF00 | (dx >> 8));                       /* AH = FF, AL = DH */
}

/* 0x95F4 joy_calibrate_screen (reduced) */
static void joy_calibrate_screen(void)
{
    if (input_poll_drive() & 0x10) {                        /* fire held on entry -> cancel (0x97C2) */
        DSB(DS_joy_enabled) = 0;
        return;
    }
    DSB(DS_joy_calibrated) = 1;
    /* PORT: the 3x3 grid screen (screen save, grid, "move the joystick" loop until fire, restore,
     * delay_ticks(100)) is dropped: the emulated joystick needs no calibration. */
}

/* 0x7864 */
u16 getkey_until_deadline(void)
{
    for (;;) {
        u16 k = getkey();
        if (k != 0) return k;
        if (ticks_elapsed(DSW(DS_deadline_start)) >= DSW(DS_deadline_len)) return 0;
        host_pump();
    }
}

/* 0x95B0 */
int menu_key(void)
{
    s16 k = (s16)getkey_until_deadline();
    if (k == 0) return -1;
    if (k == 0x1B) return 1;
    return k;
}

/* 0x95D9 (signed char compare, result sign-extended) */
int toupper_c(int c)
{
    s8 ch = (s8)c;
    if (ch >= 'a' && ch <= 'z') ch = (s8)(ch - 0x20);
    return ch;
}

/* 0x92A8 text_input_line — high-score name editor, PORT: mouse-capable on-screen keyboard. Keys kept
 * from the original: Enter 0x0D, Left 0x4B00, Right 0x4D00, Backspace 0x08, Del 0x5300, printable
 * 0x20..0x7A and the idle timeout. Insert/overwrite mode is dropped: the faithful editor body is not
 * ported. A clickable grid is added; right click clears and commits (an empty name is not recorded,
 * see scores_enter_name). */
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
    int last_hover = -2;                                  /* PORT: last drawn hover cell (live highlight) */
    mouse_ui_capture = true;                              /* PORT: grid owns the queued clicks */
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
            while (host_mouse_click(&ex, &ey, &btn, NULL)) {
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
        s16 hx, hy;
        int hover = host_mouse_pos(&hx, &hy) ? osk_hit(hx, hy) : -1;
        if (hover != last_hover) {                          /* PORT: redraw on motion, restart idle timeout */
            last_hover = hover;
            set_deadline(timeout);
            acted = true;
        }
        if (!acted) {
            if (ticks_elapsed(DSW(DS_deadline_start)) >= DSW(DS_deadline_len)) break;
            host_pump();
            continue;
        }
        gfx_clear_screen(0);
        osk_draw(buf, hover);
        host_present_now();
    }
done:
    mouse_ui_capture = false;                             /* PORT: release the click queue */
    {   /* PORT: drain, so no queued click outlives the editor (scores_show's menu_key would eat it) */
        s16 ex, ey;
        u8 b;
        while (host_mouse_click(&ex, &ey, &b, NULL)) { }
    }
    buf[len] = 0;
    /* TODO(verify): the original returns whatever AX draw_glyph left; the only caller ignores it. */
    return 0;
}
