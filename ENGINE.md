# Engine — the faithful core

Everything outside `src/enhanced/` comes from the faithful SDL3 port in
[test-drive-sdl3](https://github.com/kylofon/test-drive-sdl3) (`tdport/`). That repository holds the
reverse-engineering material this code was written from: the specs (`port/spec/*.md`), the symbol list
(`port/symbols.csv`, which generates `src/symbols.h`) and the disassembly tools. Paths below that start
with `../port` or `../tools` refer to that repository.

The engine still follows the porting rules below, so that the simulation and game flow keep behaving like
the original. `src/enhanced/` is not bound by them; it hooks into `run_stage` (`game/flow_stage.c`, marked
`ENH:`) and draws over the composed screen through `gfx_set_overlay()`.

## Porting rules (from test-drive-sdl3)

Faithful reimplementation of **TDEGA.EXE**. Behaviour, timing, integer arithmetic and visible quirks
must match the original. Specs: `../port/spec/*.md` (read `../port/README.md` and `../port/RE_GUIDE.md`
first). Symbols: `../port/symbols.csv` → generated `src/symbols.h`. Ground truth when a spec is unclear:
`python ../tools/x86dis.py ../work/TDEGA_unp.exe dis <image hex offset> <len>`.

## Architecture

```
main.c        args, mem_load_exe(), host_init(), game_main()
mem.h/.c      real-mode memory: TDEGA.EXE image at segment 0x1000, DGROUP 0x1C9A, heap above   (coordinator)
host.h/.c     SDL3: window/present, 100.0404 Hz tick, BIOS keyboard queue, gamepad, mouse, speaker, files (coordinator)
symbols.h     generated DS_*/FN_* constants                                                       (coordinator)
platform/gfx.*      planar graphics: targets, blitters, fills, text, dissolve, scroll, palette     (graphics agent)
platform/timer.*    base timer ISR body, song interpreter, delays/deadlines, sound API             (platform agent)
platform/input.*    input_poll_drive, getkey family, joystick emulation, mouse gestures, name editor (platform agent)
platform/res.*      DOS block allocator, archive cache, PES/raw loaders, res_find, rand8, CRT rand (platform agent)
game/game.h         prototypes of game_flow / scene_render / simulation functions                  (coordinator)
game/flow*.c        game_flow spec: main (game_main), screens, run_game, run_stage, scores         (flow agent)
game/scene*.c       scene_render spec: projection, road/object drawing, cockpit, crash, ending     (scene agent)
game/sim*.c         simulation spec: stage setup 0x4792, driving ISR 0x3B1F and everything it runs (sim agent)
enhanced/*          enhanced road renderer, screen overlay and the on-screen driving strip (not bound by these rules)
```

Only `host.c`, `main.c` and `mem.c` include SDL (`mem.c` uses `SDL_LoadFile`/`SDL_malloc` to load the
EXE). Game and platform code talks to the host through `host.h`; `platform/input.c` in particular
never includes SDL.

## Input (mouse and on-screen controls)

The simulation only ever consumes the original input word: a direction nibble 1–8 plus bit `0x10`
(fire). Everything added for the mouse synthesises exactly that word, so the driving model stays
faithful and `ds_joy_enabled`/input-mode state is untouched.

* `host.c` owns the raw mouse state (motion deltas, dy for menus, held buttons, wheel, a press queue
  with the SDL event timestamp) and exposes it through `host.h`; it holds no policy.
* `platform/input.c` holds the policy: the press/hold/double-click state machine, the driving and menu
  mapping, and the on-screen button hit-tests. `platform/input.h` holds the pure predicates and the
  shared geometry (`drive_cell_at`, `menu_cell_at`) so drawing and hit-testing read one source of
  truth. A scratch self-check for those pure functions lives outside the repo (not committed).
* `enhanced.c` draws the driving strip from that geometry; the menu cells and the name keyboard draw
  themselves with `gfx` primitives, because the overlay is only active inside a stage.
* A tap's fire is always emitted with direction 0: fire with a direction would shift gear, so the
  invariant is deliberate.
* Steering is absolute: `mouse_steer_abs` maps the pointer's x against the window centre to the
  steer offset (dead zone `MOUSE_OFF_THRESH` each side); on or below the strip, or when the host
  reports no pointer (`host_mouse_pos` false before entry and after leave / focus loss), the car
  goes straight. In demo mode the pointer direction is dropped because the stage loop ends the demo
  on any nonzero input word.
* The QUIT cell (long hold) and GBOX cell (tap) emit the keyboard's own words, `0xFFFF` (Esc) and
  `0xFF00 | 'd'`, so the stage loop and `sim_input_charkey` need no mouse-specific path.
* Strip cells have idle / hover / pressed fills chosen by `strip_cell_state`; pressed follows the
  cell latched at press time (`mouse_press_cell()`), so sliding off a held cell keeps it lit while
  the action continues.

## Memory model (`mem.h`)

* The original's data stays **in `mem[]` at its original address**. Every global, table, string, sprite
  handle table, the road stream, the font and the car `.BIN` buffer (DS:268F) is read and written there:
  `DSW(DS_road_pos)`, `DSB(DS_run_state)`, `DSS(DS_car_x)`, `far_rd(DGROUP, DS_spr_dash)`.
  Names come from `symbols.h`. For an offset without a name, write the raw offset with a comment:
  `DSW(0x1873) /* heading_acc */`. Never shadow game state in C variables that outlive a function.
* Code-segment variables used by the hand-written asm (`CS:5A64` current target, `CS:3B18`, …) are
  `CSW(0x5A64)` etc.
* Far pointers are `FarPtr {off, seg}`, passed by value. Sprites, archives, song streams and target
  descriptors are all `FarPtr`s into `mem[]`. Segment `0xA000` plane segments mean the EGA screen
  (graphics module only).
* Strings given to text routines may be DGROUP strings: `(const char *)mp(DGROUP, 0x0B16)`.
* Fixed-width arithmetic: use `u8/s8/u16/s16/u32/s32` exactly as the original register widths; cast
  at every step where the original truncates. Signed right shift = `(s16)x >> n` (arithmetic on our
  compilers). Emulate carries/borrows explicitly where the asm uses `adc/sbb/rcl/rcr`.
* **Division** goes through `div32_16 / idiv32_16 / div16_8 / idiv16_8` (they return 0xFFFF like the
  game's INT 0 hook). Use them for every DIV/IDIV the original performs, not just the risky ones.
* Keep the original's buffer overruns and aliasing when a spec marks them as faithful quirks (e.g.
  the traffic list copy bug at 0x45D1 writes across DS:0945–0A14). Never write outside `mem[]`.

## Timing (`host.h`, `timer.h`)

* The host calls `timer_host_tick()` (installed by `timer_init`) at exactly 100.0404 Hz from
  `host_pump()`. It runs the driving ISR if one is installed (`timer_set_driving_isr`), otherwise
  `timer_isr()`. The driving ISR (`sim_timer_isr`) calls `timer_isr()` first, like the original far call.
* **Every busy-wait loop in the original must call `host_pump()` once per iteration** (delays,
  deadlines, key polls, "wait for fire"). Platform wait helpers already do; game code that loops on its
  own must too.
* `run_stage`'s per-frame loop calls `host_pump()` once at the top of each iteration, before
  `snapshot_sim_state()`. Ticks (and therefore the simulation) only advance inside `host_pump()`, which
  matches the original's asynchronous ISR closely enough and keeps the snapshot consistent.
* Screen output is composed from the EGA planes by `gfx_compose()` and presented by the host when
  something changed. Effects the original ran unpaced (dissolves in `stage_results`) call
  `host_present_now()` after each step.

## Porting a function

1. One C function per original function, named as in `symbols.h` (`FN_<name>`), with a leading comment
   `/* 0xADDR name — spec section */`. Asm routines with register arguments become C functions with
   explicit parameters/returns named after the registers they model (`s16 proj_x_main(s16 bx_x, s16 dx_z)`).
2. Follow the spec pseudocode; confirm against the disassembly wherever the spec says `likely`/`guess`,
   or where signedness, carries or evaluation order matter.
3. Mark deliberate deviations with `/* PORT: ... */` (e.g. dropped copy protection, removed port I/O).
   Mark unresolved doubts with `/* TODO(verify): ... */` and list them in your report.
4. No `static` game state: state lives in `mem[]` (a `static` is fine for pure host-side caches).
5. Don't reformat or restructure files owned by another module. If you need a declaration that isn't
   in a shared header, add it to **your own** header, and list it in your report.

## Build and checks

On macOS (the primary development platform): `make build`, `make check` (loads `TDEGA.EXE` and exits
without opening a window) or `make run`; the Makefile wraps the CMake commands below and takes
`game_dir`, `scale`, `res_scale`, `frame_rate` and `bios-keys`.

From the repository root with MinGW on PATH (`export PATH="/c/msys64/mingw64/bin:$PATH"`):

```
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/testdrive-enhanced.exe --game-dir Game --check
```

Files must **compile without warnings**. Check a single file with
`gcc -std=c11 -Wall -Wextra -Wno-unused-parameter -fno-strict-aliasing -fsyntax-only -Isrc -I/c/msys64/mingw64/include src/<file>.c`.
You may write throwaway verification scripts in your scratch area. Do not create HTML pages.
