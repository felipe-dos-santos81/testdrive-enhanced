# Makefile for Test Drive Enhanced — native SDL3 port of EGA Test Drive (1987)
# Targets follow the build/run cycle:
#   help → configure → build → check → run → clean
SERVICE = Test Drive Enhanced

# Variables
BUILD_DIR ?= build
BUILD_TYPE ?= Release
GENERATOR ?= Ninja
CMAKE ?= cmake
CMAKE_C_COMPILER ?=
game_dir ?= Game
scale ?= 3
res_scale ?= 4
frame_rate ?= 60
ARGS ?=
SDL_CFLAGS = $(shell pkg-config --cflags sdl3 2>/dev/null)
SYNTAX_FLAGS = -std=c11 -Wall -Wextra -Wno-unused-parameter -fno-strict-aliasing -Isrc $(SDL_CFLAGS)

ifeq ($(OS),Windows_NT)
EXE = .exe
else
EXE =
endif
BINARY = $(BUILD_DIR)/testdrive-enhanced$(EXE)
CMAKE_FLAGS = -S . -B $(BUILD_DIR) -G "$(GENERATOR)" $(if $(CMAKE_C_COMPILER),-DCMAKE_C_COMPILER=$(CMAKE_C_COMPILER)) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

.PHONY: help configure build check rebuild run syntax clean

# ── Environment ──────────────────────────────────────────────────────────────

help: ## Print this help message
	@printf '\033[01;32m${SERVICE} — native SDL3 port\033[00;37m\n\n'
	@printf "\033[33mUsage:\033[0m\n  make [target] [arg=\"val\"...]\n\n\033[33mTargets:\033[0m\n"
	@grep -E '^[-a-zA-Z0-9_\.\/]+:.*?## .*$$' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*?## "}; \
		{printf "  \033[36m%-26s\033[0m %s\n", $$1, $$2}'

# ── Build ─────────────────────────────────────────────────────────────────────

configure: ## Configure the CMake build directory (build)
	$(CMAKE) $(CMAKE_FLAGS)

build: configure ## Compile the binary into build/ (usage: make build [BUILD_TYPE=Debug])
	$(CMAKE) --build $(BUILD_DIR)

rebuild: ## Wipe the build directory and compile from scratch
	rm -rf $(BUILD_DIR)
	$(CMAKE) $(CMAKE_FLAGS)
	$(CMAKE) --build $(BUILD_DIR)

check: build ## Verify TDEGA.EXE loads from the game directory and exit, without opening a window
	$(BINARY) --game-dir "$(game_dir)" --check

# ── Run ───────────────────────────────────────────────────────────────────────
# Needs the original game files: put them in $(game_dir), or pass game_dir=DIR.
# On this machine the files live in TestDrive, so: make run game_dir=TestDrive

run: build ## Play windowed (usage: make run [game_dir=TestDrive] [scale=3] [res_scale=4] [frame_rate=60] [bios-keys=1] [ARGS="--foo"])
	$(BINARY) --game-dir "$(game_dir)" --scale $(scale) --res-scale $(res_scale) --frame-rate $(frame_rate) $(if $(bios-keys),--bios-keys) $(ARGS)

# ── Development ───────────────────────────────────────────────────────────────

syntax: ## Syntax-check sources without linking (usage: make syntax [FILE=src/host.c])
	@files="$${FILE:-$$(find src -name '*.c' | sort)}"; \
	for f in $$files; do \
		echo "  CC -fsyntax-only $$f"; \
		$(CC) $(SYNTAX_FLAGS) -fsyntax-only $$f || exit 1; \
	done
	@echo "Syntax check complete."

# ── Housekeeping ──────────────────────────────────────────────────────────────

clean: ## Remove the build directory
	rm -rf $(BUILD_DIR)
	@echo "Cleanup complete."
