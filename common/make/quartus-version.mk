# common/make/quartus-version.mk — Multi-version Quartus runner shared by all
# project Makefiles.
#
# Each project Makefile sets these BEFORE including this fragment:
#   PROJECT_NAME, REVISION_NAME
#   QUARTUS_SUPPORTED ?= 23.1 25.1   # versions this project works with
#   QUARTUS_DEFAULT   ?= 23.1        # Docker fallback version
#   NEEDS_SOFTWARE_TOOLS ?= 0        # 1 if HPS projects need ARM gcc/gdb/scp
#
# Include with a relative path so REPO_ROOT is not needed in the include line:
#   quartus/Makefile:  include ../../common/make/quartus-version.mk
#   root Makefile:     include ../common/make/quartus-version.mk
#
# Exported after include:
#   QUARTUS_VERSION    effective version (auto-detected or QUARTUS_VERSION)
#   QUARTUS_HOME       local install root (empty when using Docker)
#   QUARTUS_BIN        dir containing quartus_sh / quartus_map etc.
#   QSYS_TOOL          qsys-script/qsys-generate wrapper (Platform Designer)
#   NIOS_TOOL          Nios EDS wrapper (nios2-bsp / niosv-bsp per version)
#   QROOT              repo root path as seen by the executing context:
#                      $(REPO_ROOT) in local mode, /work in Docker mode.
#                      Use $(QROOT)/... INSIDE QTOOL/SWTOOL command strings.
#                      Use $(REPO_ROOT)/... only for host-side references
#                      (docker -v mounts, scp, uname shim paths).
#   QTOOL              $(call QTOOL,cmd)  run a Quartus tool
#   SWTOOL             $(call SWTOOL,cmd) run a software tool in the tools container
#   QUARTUS_PROGRAM    JTAG-program the .sof (recipe)
#   usb-wsl, usb-windows  USB-Blaster attach/detach targets

REPO_ROOT ?= $(realpath $(dir $(lastword $(MAKEFILE_LIST)))/../..)

QUARTUS_SUPPORTED ?= 23.1
QUARTUS_DEFAULT   ?= 23.1
NEEDS_SOFTWARE_TOOLS ?= 0

ALTERA_LITE_ROOT ?= $(HOME)/tools/altera_lite
TOOLS_IMAGE ?= cvsoc/tools:1.0

USBIPD       ?= usbipd.exe
USBIPD_BUSID ?= 2-4
DEVICE_INDEX ?= 2

# ── Version resolution ────────────────────────────────────────────────────────
ifneq ($(strip $(QUARTUS_VERSION)),)
  # Explicit override: must be supported.
  ifeq ($(filter $(QUARTUS_VERSION),$(QUARTUS_SUPPORTED)),)
    $(error $(PROJECT_NAME) does not support Quartus $(QUARTUS_VERSION) (supported: $(QUARTUS_SUPPORTED)))
  endif
else
  # Auto-detect: local install first, then Docker default.
  DETECTED := $(shell $(REPO_ROOT)/common/scripts/detect-quartus.sh $(QUARTUS_SUPPORTED))
  ifneq ($(strip $(DETECTED)),)
    QUARTUS_VERSION := $(DETECTED)
  else
    QUARTUS_VERSION := $(QUARTUS_DEFAULT)
  endif
endif

# ── Runner selection ──────────────────────────────────────────────────────────
QUARTUS_HOME := $(ALTERA_LITE_ROOT)/$(QUARTUS_VERSION)std
ifeq ($(shell test -x "$(QUARTUS_HOME)/quartus/bin/quartus_sh" && echo yes),yes)
  QUARTUS_BIN   := $(QUARTUS_HOME)/quartus/bin
  QUARTUS_RUNNER := local
else
  QUARTUS_HOME  :=
  QUARTUS_BIN   :=
  QUARTUS_RUNNER := docker
endif

# ── QROOT: repo root as seen by the executing context ─────────────────────────
ifeq ($(QUARTUS_RUNNER),local)
  QROOT := $(REPO_ROOT)
else
  QROOT := /work
endif

# ── QTOOL: run a Quartus toolchain command in the project's quartus dir ──────
ifeq ($(QUARTUS_RUNNER),local)
  # Local: prepend the install's bin dirs to PATH and run directly.
  QTOOL = cd $(QROOT)/$(PROJECT_NAME)/quartus && \
          PATH="$(QUARTUS_BIN):$(QUARTUS_HOME)/quartus/sopc_builder/bin:$(QUARTUS_HOME)/nios2eds/bin:$(QUARTUS_HOME)/niosv/bin:$$PATH" $(1)
else
  # Docker: run inside the versioned Quartus container, workspace at /work.
  DOCKER_IMAGE ?= cvsoc/quartus:$(QUARTUS_VERSION)
  QTOOL = docker run --rm --user $$(id -u):$$(id -g) \
          -v $(REPO_ROOT):/work \
          $(DOCKER_IMAGE) \
          bash -c 'cd /work/$(PROJECT_NAME)/quartus && $(1)'
endif

# ── QSYS_TOOL / NIOS_TOOL: named tool groups (resolved via PATH by QTOOL) ─────
# These identify which toolchain family a project step belongs to; the actual
# binary is invoked through QTOOL so the correct version/context is used.
QSYS_TOOL = qsys-script qsys-generate
ifeq ($(QUARTUS_VERSION),23.1)
  NIOS_TOOL = nios2-bsp nios2-bsp-create-settings nios2-elf-gcc nios2-download nios2-terminal
else
  NIOS_TOOL = niosv-bsp niosv-app niosv-download
endif

# ── SWTOOL: run a software tool in the tools container ────────────────────────
SWTOOL = docker run --rm --user $$(id -u):$$(id -g) \
         -v $(REPO_ROOT):/work \
         $(TOOLS_IMAGE) \
         bash -c 'cd /work/$(PROJECT_NAME) && $(1)'

# ── USB-Blaster attach/detach (WSL2) ─────────────────────────────────────────
usb-wsl:
	@echo "Connecting USB-Blaster (busid $(USBIPD_BUSID)) to WSL2..."
	$(USBIPD) attach --wsl --busid $(USBIPD_BUSID) 2>/dev/null || true
	@sleep 1

usb-windows:
	@echo "Detaching USB-Blaster (busid $(USBIPD_BUSID)) from WSL2 → Windows..."
	$(USBIPD) detach --busid $(USBIPD_BUSID) 2>/dev/null || true
	@sleep 1

# ── QUARTUS_PROGRAM: JTAG-program the .sof ────────────────────────────────────
ifeq ($(QUARTUS_RUNNER),local)
QUARTUS_PROGRAM = jtagd; sleep 2; \
                  quartus_pgm -m jtag -o "p;$(REPO_ROOT)/$(PROJECT_NAME)/quartus/$(REVISION_NAME).sof@$(DEVICE_INDEX)"; \
                  kill $$(pgrep jtagd) 2>/dev/null || true
else
QUARTUS_PROGRAM = docker run --rm --user $$(id -u):$$(id -g) --privileged \
                  -v /dev/bus/usb:/dev/bus/usb \
                  -v $(REPO_ROOT):/work \
                  $(DOCKER_IMAGE) \
                  bash -c 'jtagd && sleep 2 && \
                    quartus_pgm -m jtag -o "p;/work/$(PROJECT_NAME)/quartus/$(REVISION_NAME).sof@$(DEVICE_INDEX)"; \
                    kill $$(pgrep jtagd) 2>/dev/null || true'
endif
