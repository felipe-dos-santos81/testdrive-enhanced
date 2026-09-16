# AGENTS.md

Test Drive Enhanced: a C11 + SDL3 port of EGA *Test Drive* (1987). It is a faithful reimplementation
of `TDEGA.EXE` (driving, traffic, police, game flow) with a higher-resolution road renderer. It is not
an emulator. `ENGINE.md` is the authoritative spec for the faithful core — read it before touching
game or platform code.

## Build and verify

```bash
make help        # list targets
make build       # configure + compile into build/
make check       # load TDEGA.EXE and exit, no window opens
make run         # build, then play windowed
```

Manual equivalent: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build`,
then `./build/testdrive-enhanced --game-dir Game --check`.

- **There is no test suite.** Verification is a warning-free build plus `make check` exiting 0. Run
  both before claiming anything works; most changes also need a windowed run to be believed.
- `make run` takes `game_dir` (default `Game`), `scale`, `res_scale`, `frame_rate`, `bios-keys`.
- Single-file syntax check:
  `cc -std=c11 -Wall -Wextra -Wno-unused-parameter -fno-strict-aliasing -fsyntax-only -Isrc src/<file>.c`
- Game data (`Game/`, `TestDrive/`) is copyrighted, not redistributable, and gitignored. If `--check`
  cannot find the EXE, the data is missing rather than the build broken.
- `CMakeLists.txt` globs `src/**/*.c` recursively with `CONFIGURE_DEPENDS`, so a new file is compiled
  without editing it; `src/enhanced/*.c` is forced to `-O2` even in Debug builds.

## Hard rules for the faithful core

Rules ported from `test-drive-sdl3`; `ENGINE.md` has the detail.

- Game state lives in `mem[]` at the original addresses (`DSB`/`DSW`/`DSS`/`DSL`, `CSW`); never shadow
  it in C globals, and no `static` game state. For an offset with no symbol, write the raw offset with
  a comment.
- `src/symbols.h` is generated (`tools/gen_symbols.py` from `port/symbols.csv`) — do not edit it.
- Use `div32_16` / `idiv32_16` / `div16_8` / `idiv16_8` for every `DIV`/`IDIV`; keep fixed-width casts
  (`u8`/`s16`/…) exact, and preserve quirks the specs mark as faithful (buffer overruns included).
- Mark deviations `/* PORT: ... */`, unresolved doubts `/* TODO(verify): ... */`.
- Only `host.c`, `main.c` and `mem.c` include SDL. `platform/input.c` must not; game and platform code
  talk to the host through `host.h`.
- Do not reformat or restructure files owned by another module. Need a declaration that is not in a
  shared header? Add it to your own header and say so in your report.
- `src/enhanced/` is exempt from these rules. It hooks `run_stage` (`game/flow_stage.c`, `ENH:`) and
  draws over the composed frame through `gfx_set_overlay()` — a **single** slot installed by
  `enh_init()`, active only inside a stage.

## Entry points and wiring

`main.c`: args → `mem_load_exe()` → `host_init()` → `enh_init()` → `game_main()` (`game/flow.c`), which
calls `gfx_init()` + `timer_init()`, then the screens. The host runs one 100.0404 Hz tick and a worker
pool (`host_parallel_for`); the enhanced renderer is per-pixel work spread across that pool.

## Mouse and on-screen controls

Added on top of the faithful core; the simulation only ever sees the original input word (direction
nibble + bit `0x10` fire), which the mouse layer synthesises.

- Raw SDL mouse state belongs in `host.c` (deltas, held buttons, wheel, press queue with the event
  timestamp) and is exposed through `host.h` with no policy.
- Input policy and gesture state belong in `platform/input.c`; pure predicates and the shared button
  geometry in `platform/input.h`. A scratch self-check for those pure functions lives outside the repo
  and is never committed.
- Keep button coordinates in `input.h` (`drive_cell_at`, `drive_cell_x`, `menu_cell_at`) and have the
  renderer read them — never duplicate the numbers in drawing code.
- Draw where the screen is drawn: the driving strip in `enhanced.c` (overlay), menu and name-keyboard
  cells with `gfx` primitives, since the overlay is inactive outside a stage.
- Safety invariant: a click's fire is emitted with direction 0. Fire plus a direction shifts gear.
- Driving steers by absolute pointer position (`mouse_steer_abs` in `input.h`): x against the window
  centre, `MOUSE_OFF_THRESH` as the dead-zone half-width; on or below the strip, or with no pointer
  in the window, the car goes straight. The relative offset (`mouse_steer_step`) only serves the
  menus' vertical flick now. In demo mode the pointer direction is suppressed: `sim_tick` ends the
  demo on any nonzero word, so only gestures reach it there.
- `host_mouse_pos` is false until the pointer has entered the window and after it leaves or focus is
  lost. Never steer or highlight from a position the host does not vouch for.
- QUIT and GBOX reuse keyboard words rather than new paths: a `MOUSE_QUIT_MS` hold on QUIT returns
  `0xFFFF` (the Esc word), a GBOX tap returns `0xFF00 | 'd'` (the gear-box toggle character).
- The strip's pressed highlight follows the latched cell (`mouse_press_cell()`), not the pointer;
  `strip_cell_state` in `input.h` is the one rule for idle / hover / pressed.

## Repo conventions

- `docs/superpowers/specs/` and `docs/superpowers/plans/` are the committed design and plan records;
  `.superpowers/` is agent scratch and gitignored.
- Commit subjects are `Area: change` (e.g. `Mouse control: ...`, `Docs: ...`).
