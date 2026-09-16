# Makefile for Test Drive Enhanced — native SDL3 port of EGA Test Drive (1987)
# Targets follow the build/run cycle:
#   help → configure → build → check → run → clean
SERVICE = Test Drive Enhanced

# Variables
BUILD_DIR ?= build
BUILD_TYPE ?= Release
game_dir ?= Game
BINARY = $(BUILD_DIR)/testdrive-enhanced
CMAKE ?= cmake
CMAKE_FLAGS = -S . -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
scale ?= 3
res_scale ?= 4
frame_rate ?= 60

.PHONY: help configure build check rebuild run clean

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

build: configure ## Compile the native arm64 binary into build/
	$(CMAKE) --build $(BUILD_DIR)

rebuild: ## Wipe the build directory and compile from scratch
	rm -rf $(BUILD_DIR)
	$(CMAKE) $(CMAKE_FLAGS)
	$(CMAKE) --build $(BUILD_DIR)

check: build ## Verify TDEGA.EXE loads from the game directory and exit, without opening a window
	$(BINARY) --game-dir $(game_dir) --check

# ── Run ───────────────────────────────────────────────────────────────────────
# Needs the original game files: put them in $(game_dir), or pass game_dir=DIR.
# On this machine the files live in TestDrive, so: make run game_dir=TestDrive

run: build ## Play windowed (usage: make run [game_dir=TestDrive] [scale=3] [res-scale=4] [frame-rate=60] [bios-keys=1])
	$(BINARY) --game-dir $(game_dir) --scale $(scale) --res-scale $(res_scale) --frame-rate $(frame_rate) $(if $(bios-keys),--bios-keys)

# ── Housekeeping ──────────────────────────────────────────────────────────────

clean: ## Remove the build directory
	rm -rf $(BUILD_DIR)
	@echo "Cleanup complete."
