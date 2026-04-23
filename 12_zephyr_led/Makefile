# Makefile for 12_zephyr_led — Phase 8: Zephyr RTOS LED demo on DE10-Nano HPS
#
# Orchestrates the West workspace and build for the Zephyr application.
#
# Usage (from this directory — 12_zephyr_led/):
#   make init     — Initialise west workspace (once, downloads ~1.5 GB)
#   make deps     — Install Python deps from Zephyr requirements.txt
#   make build    — Build the application (west build -b cyclonev_socdk)
#   make flash    — Flash via USB-Blaster II (west flash; board must be connected)
#   make debug    — Launch OpenOCD + GDB (west debug; board must be connected)
#   make clean    — Remove build/ directory
#   make pristine — Remove build/ AND re-fetch west workspace
#   make help     — Show this help
#
# Environment variables (override with make VAR=value):
#   BOARD         — Zephyr board target (default: cyclonev_socdk)
#   WEST_OPTS     — Extra options passed to west build
#   ZEPHYR_SDK_INSTALL_DIR — Path to Zephyr SDK (auto-detected if in default locations)
#
# The west workspace is created at zephyrproject/ (inside this directory).
# The application source lives in app/ (the west 'self' project per west.yml).

PROJECT_DIR   := $(CURDIR)
# west places .west/ at the project root (12_zephyr_led/).
# zephyr/ is a sibling of app/ inside this directory.
WORKSPACE_DIR := $(PROJECT_DIR)
APP_DIR       := $(PROJECT_DIR)/app
BUILD_DIR     := $(PROJECT_DIR)/build

BOARD         ?= cyclonev_socdk
WEST          := west
WEST_OPTS     ?=

# Auto-detect Zephyr SDK if installed in the default location
ZEPHYR_SDK_INSTALL_DIR ?= $(HOME)/zephyr-sdk-1.0.1
export ZEPHYR_SDK_INSTALL_DIR

PATCHES_DIR   := $(APP_DIR)/patches
ZEPHYR_DIR    := $(WORKSPACE_DIR)/zephyr
# Sentinel file — recreated whenever patches are (re)applied
PATCH_STAMP   := $(BUILD_DIR)/.patches_applied

.PHONY: all init deps build patch flash debug clean pristine help

all: build

# ── Workspace initialisation ────────────────────────────────────────────────
# Creates zephyrproject/ and checks out Zephyr v3.7.0 plus all west projects.
# Must be run once before 'make build'.
init: $(WORKSPACE_DIR)/.west/config

$(WORKSPACE_DIR)/.west/config:
	@echo "══════════════════════════════════════════════════════════════"
	@echo "  Initialising Zephyr west workspace (this downloads ~1.5 GB)"
	@echo "══════════════════════════════════════════════════════════════"
	$(WEST) init -l $(APP_DIR)
	$(WEST) update
	@$(MAKE) --no-print-directory patch
	@echo ""
	@echo "Workspace ready. Run 'make deps', then 'make build'."

# ── Python dependencies ──────────────────────────────────────────────────────
# Installs Zephyr's Python requirements after the workspace has been initialised.
deps: $(WORKSPACE_DIR)/.west/config
	pip3 install --user -r $(WORKSPACE_DIR)/zephyr/scripts/requirements.txt
	@echo "Python dependencies installed."

# ── Build ────────────────────────────────────────────────────────────────────
build: $(WORKSPACE_DIR)/.west/config
	@echo "Building for board: $(BOARD)"
	$(WEST) build -b $(BOARD) $(WEST_OPTS) \
	    --build-dir $(BUILD_DIR) \
	    $(APP_DIR)
	@echo ""
	@echo "═══════════════════════════════════════════════════════════════"
	@echo "  Build complete!"
	@echo "  ELF: $(BUILD_DIR)/zephyr/zephyr.elf"
	@echo "  BIN: $(BUILD_DIR)/zephyr/zephyr.bin"
	@echo ""
	@echo "  Flash to board: make flash"
	@echo "  Debug:          make debug"
	@echo "═══════════════════════════════════════════════════════════════"

# ── Patch upstream Zephyr ────────────────────────────────────────────────────
# Apply local patches to the west-managed zephyr/ checkout.
# Patches live in app/patches/ and are tracked in bleviet/cvsoc.
# The stamp file ensures patches are only applied once per build/ lifetime;
# run 'make patch' explicitly after 'make pristine' or 'west update'.
patch: $(PATCH_STAMP)

$(PATCH_STAMP): $(wildcard $(PATCHES_DIR)/*.patch) | $(WORKSPACE_DIR)/.west/config
	@echo "Applying local patches to zephyr/ ..."
	@cd $(ZEPHYR_DIR) && git apply --check $(PATCHES_DIR)/*.patch 2>/dev/null \
	    && git apply $(PATCHES_DIR)/*.patch \
	    || echo "  (patches already applied or not applicable — skipping)"
	@mkdir -p $(BUILD_DIR)
	@touch $(PATCH_STAMP)

# ── Flash ────────────────────────────────────────────────────────────────────
# Requires:
#   - DE10-Nano powered on and connected via USB-Blaster II
#   - USB-Blaster II forwarded to WSL2 (usbipd)
#   - Preloader (u-boot-spl) in zephyr board support dir (see doc/README.md)
flash: $(PATCH_STAMP) $(BUILD_DIR)/zephyr/zephyr.elf
	$(WEST) flash --build-dir $(BUILD_DIR)

# ── Debug ─────────────────────────────────────────────────────────────────────
debug: $(BUILD_DIR)/zephyr/zephyr.elf
	$(WEST) debug --build-dir $(BUILD_DIR)

# ── Rebuild only (no west update) ─────────────────────────────────────────────
rebuild:
	$(WEST) build --build-dir $(BUILD_DIR) $(WEST_OPTS)

# ── Clean ────────────────────────────────────────────────────────────────────
clean:
	rm -rf $(BUILD_DIR)
	@echo "Cleaned: $(BUILD_DIR)"

pristine: clean
	rm -rf $(WORKSPACE_DIR)
	@echo "Cleaned: $(WORKSPACE_DIR)"

# ── Help ──────────────────────────────────────────────────────────────────────
help:
	@echo "Phase 8 — Zephyr RTOS LED Demo (DE10-Nano)"
	@echo ""
	@echo "Targets:"
	@echo "  init      — Initialise west workspace (once; downloads ~1.5 GB)"
	@echo "  deps      — Install Python deps (run after init)"
	@echo "  build     — Build for cyclonev_socdk board"
	@echo "  flash     — Flash via USB-Blaster II (board must be connected)"
	@echo "  debug     — OpenOCD + GDB debug session"
	@echo "  patch     — (Re)apply local patches to zephyr/ checkout"
	@echo "  clean     — Remove build/ directory"
	@echo "  pristine  — Remove build/ and zephyrproject/ (full reset)"
	@echo "  help      — Show this message"
	@echo ""
	@echo "Board:        $(BOARD)"
	@echo "Workspace:    $(WORKSPACE_DIR)"
	@echo "Build dir:    $(BUILD_DIR)"
	@echo "Zephyr SDK:   $(ZEPHYR_SDK_INSTALL_DIR)"
	@echo ""
	@echo "Prerequisites: see docs/INSTALL_DEPS_PHASE8.md"
