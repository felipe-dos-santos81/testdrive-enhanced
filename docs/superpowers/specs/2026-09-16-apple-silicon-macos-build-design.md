# Apple Silicon build & run — design

Date: 2026-09-16
Status: approved

## Goal

Build and run Test Drive Enhanced natively on Apple Silicon (arm64 macOS), windowed, with the
cursor not locked. Document how, and how to obtain the original game data.

## Findings (measured, not assumed)

The repository already satisfies the functional goals with **zero source changes**:

| Requirement | State | Evidence |
|---|---|---|
| arm64 build | works | `build/testdrive-enhanced` is Mach-O 64-bit arm64, AppleClang 21 |
| SDL3 resolved | works | Homebrew `sdl3` 3.4.16; `find_package(SDL3 REQUIRED CONFIG)` succeeds, no prefix flag |
| windowed start | already | `src/host.c:50` creates `SDL_WINDOW_RESIZABLE`; no fullscreen flag |
| cursor not locked | already | no `SDL_SetRelativeMouseMode` / `SDL_SetWindowGrab` / `SDL_ShowCursor` anywhere in `src/` |
| game data | present | `TestDrive/` holds all 33 files; `--check` exits 0 |

`Project64` and mouse-lock clauses apply to nothing here: this is a native SDL3 C program with no
Windows headers, no emulator, and no mouse code. Threading is `SDL_CreateThread`.

One cosmetic defect remains: the linker warns on every link.

```
ld: warning: reducing alignment of section __DATA,__common from 0x8000 to 0x4000
    because it exceeds segment maximum alignment
```

## Non-goals

- No `.app` bundle / dock integration (`SDL_MAIN_HANDLED` stays).
- No change to the game, renderer, memory model semantics or SDL behaviour.
- No new dependencies.

## Changes

### B — README: macOS section and data source

In `README.md`:

1. Turn the single Build section into two, `### Windows (MSYS2 MinGW64)` (existing text unchanged)
   and `### macOS (Apple Silicon)`:

   ```bash
   brew install cmake ninja sdl3
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ./build/testdrive-enhanced --game-dir Game
   ```

   The documented data directory stays `Game` (the program's default). On this machine the data
   lives in `TestDrive/`, so the local command is `--game-dir TestDrive`; alternatively rename the
   directory to `Game`. The README keeps `Game` and does not mention `TestDrive`.

2. In Requirements, add the download pointer for the original data:
   the game can be obtained from
   https://www.abandonwaredos.com/abandonware-game.php?abandonware=Test+Drive&gid=1600

### C — silence the linker alignment warning

`src/mem.c:8`:

```c
u8 mem[MEM_SIZE] = { 0 };
```

Rationale: `mem` is a 0x110000-byte *tentative* definition, so it lands in the `__common` symbol
bucket, which Apple's `ld` wants aligned to 0x8000 — above the 16 KB `__DATA` segment maximum, so
`ld` reduces it and warns. Giving it an initializer makes it a defined zero-filled object instead
of a common symbol, which drops the oversized alignment request. Measured: warning gone, binary
still 233 KB (zero-fill, not file data), `nm` reports the symbol at `__DATA,__common` with no
alignment demand.

Semantics unchanged: `mem_load_exe()` already does `memset(mem, 0, sizeof mem)` (`src/mem.c:135`),
so the array is all-zero before use either way. This is preferred over an explicit
`_Alignas(0x4000)`, which was also measured to work, because it hard-codes an architecture-specific
page size while the initializer is architecture-independent.

### D — ignore the data directory

`.gitignore`: add `/TestDrive/` beside the existing `/Game/`. The original game files are
copyrighted and not redistributable; the directory currently sits untracked in the repository root,
so a broad `git add` would stage it.

## Verification

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build                # expect no warnings
./build/testdrive-enhanced --game-dir TestDrive --check   # expect exit 0
```

Plus one windowed launch to confirm the cursor is not captured:

```
./build/testdrive-enhanced --game-dir TestDrive
```

## Risks

- The GUI launch is interactive and needs a display; it is the only step not scriptable.
- `SDL_MAIN_HANDLED` means no dock icon or app menu; accepted, out of scope.
- The warning is cosmetic. If `ld`'s heuristic changes again the fix is local to `src/mem.c`.
