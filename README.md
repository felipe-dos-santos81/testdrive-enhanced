# Test Drive Enhanced — SDL3

An enhanced version of the EGA *Test Drive* (1987) by Accolade / Distinctive Software, running natively on
SDL3. The game itself is the faithful C reimplementation from
[test-drive-sdl3](https://github.com/kylofon/test-drive-sdl3), so driving, traffic, police and the game flow
work as in the original. The view through the windscreen is redrawn at a higher resolution, further draw
distance and 60 fps. It is not an emulator - the original data is not redistributed, and you need to get it yourself.

This is a fork of [kylofon/testdrive-enhanced](https://github.com/kylofon/testdrive-enhanced), simplified for
an Apple Silicon port and built for educational purposes only.

## Requirements

* The original game files, copied into `Game` (see [Game data](#game-data)).
* CMake 3.24+, a C11 compiler and SDL 3.
* A multi-core CPU for the default resolution. Rendering is split across all cores.

## Game data

The original data is not redistributed here; the EGA *Test Drive* release can be obtained from
[Abandonware DOS](https://www.abandonwaredos.com/abandonware-game.php?abandonware=Test+Drive&gid=1600).
Create a `Game` folder in the repository root, next to this README, and copy the files into it:

```
testdrive-enhanced/
└── Game/
    ├── TDEGA.EXE
    ├── CARS.TXT
    ├── SCORES
    ├── TDSND.SND
    ├── *.PES
    └── *.BIN, *.SS
```

`Game` is the default: with the files in place, `./build/testdrive-enhanced` finds them. To keep them
elsewhere, pass the folder with `--game-dir DIR`.

## Build

### Windows (MSYS2 MinGW64)

From the repository root, in Git Bash or an MSYS2 MinGW64 shell:

```bash
export PATH="/c/msys64/mingw64/bin:$PATH"
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### macOS (Apple Silicon)

```bash
brew install cmake ninja sdl3
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Run

```bash
./build/testdrive-enhanced.exe --game-dir Game
```

On macOS the binary has no `.exe` suffix:

```bash
./build/testdrive-enhanced --game-dir Game
```

| Option | Meaning |
|---|---|
| `--game-dir DIR` | Folder with the original game files (default `Game`) |
| `--scale N` | Initial window size as a multiple of 320×240 (default 3) |
| `--res-scale N` | Output resolution as a multiple of 320×200 (default 4 = 1280×800, range 1–8; lower it on slower CPUs) |
| `--frame-rate FPS` | Drawing rate while driving (default 60, `0` = unpaced) |
| `--bios-keys` | Original keyboard behaviour for driving: keys act only through key repeat (see below) |
| `--check` | Verify `TDEGA.EXE` loads and exit, without opening a window |

Alt+Enter toggles fullscreen. The window keeps the 4:3 aspect of a 200-line EGA monitor.

## Controls (from the original)

* Arrow keys / numeric keypad: steer, accelerate, brake and shift through the gear gate, as in the original.
* Esc: quit the current drive or menu.
* Ctrl-J / Ctrl-K: joystick / keyboard control. A connected gamepad acts as the joystick (left stick or
  D-pad, A = fire).
* Ctrl-Q / Ctrl-S: sound off / on.

## Changes from original

### Road view

* **Resolution:** the road is drawn at 4× the original resolution by default, with smoothed edges. The cockpit,
  mirror and sprites keep their original pixel art, scaled up.
* **Draw distance:** 120 road rows instead of 40. The road and cliffs get lighter and hazier in the distance.
* **Smooth motion:** 60 fps instead of 8. The road moves continuously instead of one road unit at a time, and
  steering drift is smoothed. The rows nearest the car keep straight edges.
* **Horizon:** on the open side, a horizon below eye level with mountains above it and a valley floor below it
  that moves as you drive. A hillside falls away under the left road edge, so on left bends the far road sits
  on its own slope.
* **Cliff:** the original's plain rock face, with the slant of its cliff-edge sprite, reaching the top of the
  window. The grass mounds at its foot are always green. The original's colour depended on what was behind them.
* **Objects:** signs, poles, traffic and the police car are scaled smoothly with distance. They switch to the
  more detailed sprites further away than in the original, and are hidden behind hill crests and the cliff.
  Traffic that appears in the distance fades in.
* **Stage clock** in the top right, counting the same seconds the results screen shows.
* **Kept from the original:** the mirror, dashboard, speeding ticket, "Pulling into…" messages, windscreen
  cracks and GAME OVER.

### Game (from the faithful port)

* **Held-key driving:** arrows / keypad and A / Z are read while held, not only through key repeat
  (`--bios-keys` restores the original).
* **Timing:** the gear-shift panel close delay and the windscreen crash animation keep the original 8 fps timing.
  The driving model is unchanged. For example, the brake does nothing while the tyres are skidding, as in the original.
* **Removed:** copy protection, the TD.EXE launcher password, and Hercules / CGA modes.
* **Missing SCORES:** starts with an empty table instead of exiting.
* **Extended-ASCII keys:** ignored instead of crashing.

## Layout

See `ENGINE.md` for the architecture and the rules the engine follows. In short:
* `src/enhanced/` holds the enhanced road renderer, the screen overlay and the stage clock.
* `src/mem.*` emulates the real-mode address space the game ran in.
* `src/host.*` wraps SDL3 and runs the render worker threads.
* `src/platform/` holds the EGA graphics, timer/sound, input and resource layers.
* `src/game/` holds game flow, scene rendering and simulation.

Reverse-engineering tools, specs and file formats are in
[test-drive-sdl3](https://github.com/kylofon/test-drive-sdl3).

## License

MIT, see [LICENSE](LICENSE). *Test Drive* and its data belong to their respective owners.

## Support

https://buymeacoffee.com/krzysztofkania
