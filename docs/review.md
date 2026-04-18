# Repository Review

> Reviewed: April 2026 | Reviewer: GitHub Copilot

---

## Overall Rating: 7 / 10

This is a well-started tutorial repository with a clear educational vision, clean VHDL code, and a thoughtful build automation approach. The foundation is solid, but the repo is still in its early stages: the more advanced teaching goals (simulation, CI/CD, SoC software, Ethernet) are described but not yet implemented.

---

## What the Repository Is

A structured tutorial series targeting FPGA developers with basic VHDL experience who want to level up to real-world Intel/Altera Cyclone V SoC development on the DE10-Nano board. The series promises to teach:

- Pure VHDL LED control (started)
- Bare Metal software with Nios II and ARM CPUs via Avalon MM interfaces
- Embedded Linux on the ARM HPS
- Ethernet-based PC-to-FPGA control
- Simulation (GHDL, VUnit, cocotb)
- Scripting and CI/CD workflows
- Clean coding and modularization

---

## Strengths

### 1. Clean, Modern VHDL
All VHDL source files use the **VHDL 2008** standard. Entities are well-structured with meaningful port/generic names using consistent naming conventions (`_i` suffix for inputs, `_o` for outputs, `G_` for generics, `C_` for constants). The code is concise, readable, and avoids anti-patterns.

**Example of good practice:** `led_blinking.vhd` uses generics for both clock frequency and blink frequency, making the module fully reusable without modification.

### 2. Script-Based Quartus Project Management
Quartus project files (`.qsf`, `.qpf`) are generated at build time via TCL scripts and are excluded from version control by `.gitignore`. This is a significant best practice that is often missed by beginners:
- No binary or auto-generated project files pollute the repository
- The TCL scripts serve as living documentation of the project configuration
- Projects are reproducible from a clean clone

### 3. Makefile-Driven Build Flow
Each project includes a `Makefile` that orchestrates the full Quartus compilation pipeline (`map → fit → asm → sta`). Targets are logically separated (individual steps can be run independently), and the `clean` target is comprehensive.

### 4. Shared IP Library
The `common/ip/` directory demonstrates good modularization: the `power_on_reset_generator` is a reusable, parameterized component shared by all projects instead of being duplicated. This pattern scales well as the series grows.

### 5. Automated Timing Verification
The `scripts/check_timing_slacks.py` script parses the Quartus STA report and fails the build with color-coded red output if any timing slack is negative. This integrates quality checking directly into the build process.

### 6. Code Style Enforcement
The `.editorconfig` file enforces consistent formatting (LF line endings, 2-space VHDL indentation, tab Makefile indentation) across all contributors and editors.

---

## Issues and Weaknesses

### Critical

**None** — the existing code is functionally correct and well-written for its scope.

### Major

#### M1: No Simulation Files
The `doc/README.md` of `00_led_blinking` lists a `sim/` directory in the project structure, but it does not exist. The main `README.md` prominently advertises GHDL, VUnit, and cocotb as key learning outcomes, yet not a single testbench exists in the repository. This is the largest gap between stated goals and current content.

#### M2: No CI/CD Pipeline
The README explicitly mentions "Continuous Development/Continuous Integration (CI/CD)" as a learning goal. No `.github/workflows/` or equivalent pipeline configuration exists. For a tutorial teaching CI/CD, the repository itself should demonstrate it.

#### M3: No Development Environment Documentation
The `about.md` mentions three setup options: Windows native, Linux native, and Docker. None of these are documented anywhere in the repo. There is no `Dockerfile`, no `docs/setup/` directory, and no instructions for installing the correct Quartus version or required tools. A new user cannot reproduce the build environment.

### Minor

#### m1: Inconsistency Between Projects
`00_led_blinking` has a `scripts/` directory with `check_timing_slacks.py` and a `check_timing` Makefile target. `01_led_running` has neither. All projects should follow the same conventions.

#### m2: Duplicate Files Across Projects
`de10_nano_pin_assignments.tcl` and `de10_nano.sdc` are byte-for-byte identical in both projects. These board-level files could live in `common/` and be sourced from there, reducing maintenance overhead.

#### m3: One-Cycle LED Glitch in `led_running.vhd`
When the running light reaches `x"80"`, the shift operation produces `x"00"`. Due to VHDL's signal update semantics, the guard `if led_running = x"00"` evaluates against the *previous* value, so the reset to `x"01"` only fires the *following* clock cycle. This means all 8 LEDs turn off for exactly one running-light period before the pattern restarts. For a tutorial codebase, this subtle signal/variable semantics point is worth either fixing or explicitly documenting as a teaching moment.

#### m4: No Root-Level Makefile
A root `Makefile` with a target like `make all` (building every project in sequence) would be convenient and is a prerequisite for any CI/CD integration.

#### m5: `01_led_running` Has No `doc/` Directory
`00_led_blinking` has a `doc/README.md`. `01_led_running` does not, creating inconsistency in the tutorial structure.

---

## Code Quality Summary

| Aspect               | Rating  | Notes                                                   |
|----------------------|---------|---------------------------------------------------------|
| VHDL Style           | ★★★★★   | Clean, modern VHDL 2008, consistent naming              |
| Modularity           | ★★★★☆   | Good shared IP pattern; pin/SDC files not yet shared    |
| Build Automation     | ★★★★☆   | Solid Makefile+TCL flow; lacks root Makefile            |
| Simulation           | ★☆☆☆☆   | Referenced in docs but entirely absent in code          |
| CI/CD                | ★☆☆☆☆   | Described as a goal; zero implementation               |
| Documentation        | ★★★☆☆   | Good high-level overview; missing setup/install guides  |
| Consistency          | ★★★☆☆   | Noticeable differences between project 00 and 01        |
| Reproducibility      | ★★★★☆   | TCL-based project creation is excellent; env docs missing|

---

## Summary

The repository demonstrates genuine craft in the parts that exist. The VHDL is idiomatic, the build toolchain is professional-grade, and the shared-IP pattern is something many experienced engineers skip. The gap is between the ambitious vision stated in `about.md`/`README.md` and the two simple VHDL-only projects currently present. The roadmap below proposes a path to close that gap incrementally.
