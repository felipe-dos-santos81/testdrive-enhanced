# Test Drive Enhanced

A modernised take on Accolade / Distinctive Software's *Test Drive* (1987, EGA version), running natively
on SDL3. The game engine is the faithful C reimplementation from
[test-drive-sdl3](https://github.com/kylofon/test-drive-sdl3), so the driving model, traffic, police and
game flow behave like the original. The road view on top of it is redrawn and is not bound by the original.

The original game data is not included. You need your own copy of the game files.

## What is different from the original

* **Draw distance:** 120 road rows instead of 40, with lighter, hazier road and cliffs in the distance.
* **Smooth motion at 60 fps:** rows move with the car's position inside a road unit, and steering drift is
  smoothed. The gear-shift panel close delay and the windscreen crash animation keep the original 8 Hz timing.
* **Edges:** 4× supersampled, resolved in linear light and lightly sharpened, instead of the original 16-pixel
  scenery blocks. The road height uses true perspective, so the rows nearest the car keep straight edges.
* **Scenery on the open side:** a horizon below eye level, with mountains above it and a valley floor below it
  that moves as you drive. Under the left road edge, a dark rim and a hillside falling away, so on left bends
  the far road sits on its own slope.
* **Cliff side:** the original's plain rock face, with the slant of its cliff-edge sprite, up to the top of the
  window. As in the original's scanline fill, everything right of the nearest road row is cliff.
* **Objects:** the original sprites, scaled continuously with distance and hidden behind hill crests and the
  cliff. Traffic that appears far away fades in.
* **Stage clock** in the top right, in the same seconds the results screen uses.

Pixels the original draws over the road view are kept: mirror, speeding ticket, "Pulling into…" text,
GAME OVER and the dashboard.

From the faithful port: held-key driving (`--bios-keys` restores key-repeat driving), no copy protection or
launcher password, no Hercules / CGA modes, a missing `SCORES` file starts an empty table.

## Requirements

* Your game files in a folder: `TDEGA.EXE`, `CARS.TXT`, `SCORES`, `TDSND.SND`, the `*.PES` archives and the car
  `*.BIN` / `*.SS` files. By default the game looks in `Game` under the working directory.
* CMake 3.24+, a C11 compiler and SDL 3.

## Build

In Git Bash or an MSYS2 MinGW64 shell:

```bash
export PATH="/c/msys64/mingw64/bin:$PATH"
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Run

```bash
./build/testdrive-enhanced.exe --game-dir Game
```

| Option | Meaning |
|---|---|
| `--game-dir DIR` | Folder with the original game files (default `Game`) |
| `--scale N` | Initial window size as a multiple of 320×240 (default 3) |
| `--frame-rate FPS` | Drawing rate while driving (default 60, `0` = unpaced) |
| `--bios-keys` | Original keyboard behaviour for driving: keys act only through key repeat |
| `--check` | Verify `TDEGA.EXE` loads and exit, without opening a window |

Alt+Enter toggles fullscreen. The window keeps the 4:3 aspect of a 200-line EGA monitor.

## Controls (from the original)

* Arrow keys / numeric keypad: steer, accelerate, brake and shift through the gear gate.
* Esc: quit the current drive or menu.
* Ctrl-J / Ctrl-K: joystick / keyboard control. A connected gamepad acts as the joystick (left stick or
  D-pad, A = fire).
* Ctrl-Q / Ctrl-S: sound off / on.

## Layout

* `src/enhanced/` — the road renderer, overlay and stage clock.
* `src/mem.*`, `src/host.*`, `src/platform/`, `src/game/` — the engine from test-drive-sdl3. See
  [ENGINE.md](ENGINE.md) for its architecture and rules.

## License

MIT, see [LICENSE](LICENSE). *Test Drive* and its data belong to their respective owners.
