# Makefile for Test Drive Enhanced — SDL3 C11 port of Test Drive (1987)
# Targets are numbered by pipeline stage:
#   configure → 1 build → 2 check → 3 run → dev
SERVICE = Test Drive Enhanced

# Variables
BUILD_DIR = build
BUILD_TYPE ?= Release
GENERATOR ?= Ninja
CMAKE = cmake
CMAKE_C_COMPILER ?=
GAME_DIR ?= Game
SCALE ?=
RES_SCALE ?=
FRAME_RATE ?=
ARGS ?=
SDL_CFLAGS = $(shell pkg-config --cflags sdl3 2>/dev/null)
SYNTAX_FLAGS = -std=c11 -Wall -Wextra -Wno-unused-parameter -fno-strict-aliasing -Isrc $(SDL_CFLAGS)

ifeq ($(OS),Windows_NT)
EXE = .exe
else
EXE =
endif
BIN = $(BUILD_DIR)/testdrive-enhanced$(EXE)

.PHONY: help configure build check run syntax clean

# ── Environment ──────────────────────────────────────────────────────────────

help: ## Print this help message
	@printf '\033[01;32m${SERVICE} — SDL3 build, check and run\033[00;37m\n\n'
	@printf "\033[33mUsage:\033[0m\n  make [target] [arg=\"val\"...]\n\n\033[33mTargets:\033[0m\n"
	@grep -E '^[-a-zA-Z0-9_\.\/]+:.*?## .*$$' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*?## "}; \
		{printf "  \033[36m%-26s\033[0m %s\n", $$1, $$2}'

configure: ## Configure the CMake build tree (usage: make configure [BUILD_TYPE=Debug] [GENERATOR="Unix Makefiles"])
	$(CMAKE) -S . -B $(BUILD_DIR) -G "$(GENERATOR)" $(if $(CMAKE_C_COMPILER),-DCMAKE_C_COMPILER=$(CMAKE_C_COMPILER)) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

# ── Stage 1 · Build (CMake + Ninja) ──────────────────────────────────────────

build: configure ## [STEP 1] Build the game binary (usage: make build [BUILD_TYPE=Debug])
	$(CMAKE) --build $(BUILD_DIR)

# ── Stage 2 · Check (load TDEGA.EXE, no window) ──────────────────────────────

check: build ## [STEP 2] Verify TDEGA.EXE loads and exit, without opening a window (usage: make check [GAME_DIR=Game])
	$(BIN) --game-dir "$(GAME_DIR)" --check

# ── Stage 3 · Run ────────────────────────────────────────────────────────────

run: build ## [STEP 3] Run the game (usage: make run [GAME_DIR=Game] [SCALE=3] [RES_SCALE=4] [FRAME_RATE=60] [ARGS="--bios-keys"])
	$(BIN) --game-dir "$(GAME_DIR)" $(if $(SCALE),--scale $(SCALE)) $(if $(RES_SCALE),--res-scale $(RES_SCALE)) $(if $(FRAME_RATE),--frame-rate $(FRAME_RATE)) $(ARGS)

# ── Development ───────────────────────────────────────────────────────────────

syntax: ## Syntax-check sources without linking (usage: make syntax [FILE=src/host.c])
	@files="$${FILE:-$$(find src -name '*.c' | sort)}"; \
	for f in $$files; do \
		echo "  CC -fsyntax-only $$f"; \
		$(CC) $(SYNTAX_FLAGS) -fsyntax-only $$f || exit 1; \
	done
	@echo "Syntax check complete."

clean: ## Remove the build directory and generated binaries
	rm -rf $(BUILD_DIR)
	@echo "Cleanup complete."
