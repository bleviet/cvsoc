# Let's Dive into Advanced FPGA Fun Together! (`cvsoc`)

👋 **Hey Fellow FPGA Explorers!**

Got some VHDL know-how and a burning desire to learn more? You're in for a treat! Our tutorial series is all about taking your FPGA game to the next level on the **Terasic DE10-Nano (Intel Cyclone V SoC)**, and we're stoked to have you join us.

---

## 📚 Documentation Organized by the Diátaxis Framework

All documentation in this repository follows the **[Diátaxis Framework](docs/README.md)**, separating **Tutorials** (learning by doing), **Explanations** (deep architectural understanding), **How-To Guides** (solving specific problems), and **Reference** (fast factual lookup).

👉 **[Browse the Full Documentation Hub (`docs/README.md`)](docs/README.md)**

| Phase / Topic | Project Folder(s) | 🎓 Tutorial (Step-by-Step) | 🧠 In-Depth Explanation ("Why & How") | 🛠️ How-To Guides (Problem-Solving) |
| :--- | :--- | :--- | :--- | :--- |
| **Tooling & Containers** | `common/docker/`<br/>`common/make/` | • [Docker Dev Environment](docs/tutorial_docker_dev_environment.md)<br/>• [Docker GUI (X11/WSLg)](docs/tutorial_docker_gui.md)<br/>• [Multi-Version Quartus (23.1 & 25.1)](docs/tutorial_phase11_multi_version_quartus.md) | [Containerized Toolchains & Multi-Version Quartus Architecture](docs/explanation_docker_and_multi_version_quartus.md) | • [Troubleshoot JTAG & USB-Blaster](docs/howto_troubleshoot_jtag_and_usb_blaster.md)<br/>• [Program from Windows in WSL2](docs/howto_windows_wsl2_programmer.md) |
| **Phase 0 & 1:**<br/>Pure VHDL | `00_led_blinking/`<br/>`01_led_running/` | • [Phase 0: LED Blinking](docs/tutorial_phase0_led_blinking.md)<br/>• [Phase 1: LED Running](docs/tutorial_phase1_led_running.md) | [Pure VHDL Design, Reset Architecture & Static Timing Analysis](docs/explanation_phase0_phase1_vhdl_and_timing.md) | • [Troubleshoot JTAG & USB-Blaster](docs/howto_troubleshoot_jtag_and_usb_blaster.md) |
| **Phase 2:**<br/>Soft-Core CPUs | `04_nios2_led/`<br/>`04b_niosv_led/` | • [Phase 2: Nios II Soft-Core](docs/tutorial_phase2_nios2.md)<br/>• [04b: Nios V/m RISC-V](04b_niosv_led/doc/README.md)<br/>• [C++ Register Shell](04b_niosv_led/doc/REGISTER_SHELL_TUTORIAL.md) | [Soft-Core Processors in FPGA Fabric — Nios II vs. Nios V/m](docs/explanation_phase2_soft_cpu_nios2_and_niosv.md) | • [Migrate Nios II to Nios V/m](docs/howto_migrate_nios2_to_niosv.md)<br/>• [Add Custom Avalon-MM IP](docs/howto_add_custom_avalon_mm_peripheral.md) |
| **Phase 3:**<br/>ARM HPS Bare-Metal | `05_hps_led/` | • [Phase 3: HPS Bare-Metal](docs/tutorial_phase3_hps_baremetal.md)<br/>• [Hardware Debug Setup](docs/tutorial_debug_setup.md) | [Cyclone V HPS Silicon, Bridges, Watchdogs & DDR3 PHY Flow](docs/explanation_phase3_hps_baremetal_architecture.md) | • [Fix Error 174068, Bridge Aborts & Watchdogs](docs/howto_fix_hps_ddr3_fitter_and_bridge_errors.md) |
| **Phase 3B:**<br/>DDR3 & FPGA DMA | `14_ddr3_hps_test/`<br/>`15_ddr3_fpga_hps/` | • [Phase 3B: DDR3 Memory](docs/tutorial_phase3b_ddr3_memory.md)<br/>• [14: DDR3 HPS Test](14_ddr3_hps_test/doc/README.md)<br/>• [15: Shared DDR3 DMA](15_ddr3_fpga_hps/doc/README.md) | [Cyclone V DDR3 Subsystem, F2H SDRAM Bridge & Avalon-MM Masters](docs/explanation_phase3b_ddr3_and_avalon_master.md) | • [Add Custom Avalon-MM IP](docs/howto_add_custom_avalon_mm_peripheral.md)<br/>• [Fix Error 174068 & Bridge Aborts](docs/howto_fix_hps_ddr3_fitter_and_bridge_errors.md) |
| **Phase 4:**<br/>Hardware Interrupts | `06_nios2_interrupts/`<br/>`06b_niosv_interrupts/`<br/>`07_hps_interrupts/` | • [Phase 4: Hardware Interrupts](docs/tutorial_phase4_interrupts.md)<br/>• [06b: Nios V Interrupts](06b_niosv_interrupts/doc/README.md) | [Hardware Interrupt Subsystems — Nios II IIC, Nios V CLINT & ARM GICv1](docs/explanation_phase4_interrupt_subsystems.md) | • [Migrate Nios II to Nios V/m](docs/howto_migrate_nios2_to_niosv.md) |
| **Phase 5:**<br/>GDB & IDE Debugging | `08_nios2_debug/`<br/>`08b_niosv_debug/`<br/>`09_hps_debug/` | • [Phase 5: GDB Debugging](docs/tutorial_phase5_gdb_debugging.md)<br/>• [08b: Nios V GDB Debug](08b_niosv_debug/doc/README.md) | • [Hardware JTAG Debug Architecture](docs/explanation_phase5_jtag_and_gdb_debugging.md)<br/>• [IDE Debugging (VS Code & Neovim)](docs/explanation_ide_debugging.md) | • [Debug with VS Code](docs/howto_vscode_debugging.md)<br/>• [Troubleshoot JTAG & USB-Blaster](docs/howto_troubleshoot_jtag_and_usb_blaster.md) |
| **Phase 6:**<br/>Embedded Linux | `10_linux_led/` | • [Phase 6: Embedded Linux on HPS](docs/tutorial_phase6_embedded_linux.md) | [Embedded Linux Boot Chain, Buildroot, FPGA Manager & `/dev/mem`](docs/explanation_phase6_embedded_linux_and_fpga_manager.md) | • [Deploy Over Ethernet or Serial](docs/howto_deploy_to_board_over_serial_and_ethernet.md) |
| **Phase 7:**<br/>Ethernet, Protobuf & Web UI | `11_ethernet_hps_led/` | • [Phase 7: UDP, Nanopb Protobuf & Web Dashboard](docs/tutorial_phase7_ethernet.md) | [Embedded Networking, IPv6 SLAAC, Nanopb Protobuf & Async Gateways](docs/explanation_phase7_networking_protobuf_and_dashboard.md) | • [Extend Protobuf & Web Dashboard](docs/howto_extend_protobuf_and_web_dashboard.md)<br/>• [Deploying Without SSH](11_ethernet_hps_led/doc/deploying_without_ssh.md) |
| **Phase 8:**<br/>Zephyr RTOS | `12_zephyr_led/` | • [Phase 8: Zephyr RTOS LED Control](docs/tutorial_phase8_zephyr.md) | [Zephyr RTOS Architecture, Compile-Time Devicetree & Threads](docs/explanation_phase8_zephyr_rtos_architecture.md) | • [Phase 8 Q&A](docs/qa.md) |
| **Phase 9:**<br/>Secure Boot | `13_secure_boot/` | • [Phase 9: Educational Secure Boot](13_secure_boot/doc/README.md) | [Embedded Cybersecurity — AES-256 Bitstream Encryption & Signed FIT](docs/explanation_phase9_secure_boot_and_crypto.md) | • [Deploy Over Ethernet or Serial](docs/howto_deploy_to_board_over_serial_and_ethernet.md) |
| **Phase 10:**<br/>Network OTA Update | `16_field_update/` | • [Phase 10: Network Field Update & Rollback](16_field_update/doc/README.md) | [Atomic A/B Field Updates, Live FPGA Hot-Swap & Automatic Rollback](docs/explanation_phase10_ota_field_update_and_rollback.md) | • [Create & Test OTA Firmware Bundles](docs/howto_create_and_test_ota_firmware_bundles.md) |

### 📖 Reference Documents
- [**Command Reference**](docs/command-reference.md) — Complete CLI cheat sheet for Quartus, Platform Designer, Nios II/V, ARM HPS, Buildroot, and Zephyr
- [**Project Dependencies**](docs/project_dependencies.md) — Explicit binary and conceptual dependency graph across all project directories
- [**Project Roadmap**](docs/roadmap.md) — Architectural principles and phase progression

---

## Before We Start:

- **FPGA Familiarity:**
   - If you've tinkered with VHDL before, awesome! You're in the right crew.

- **Keen Learners:**
   - All you need is a curious mind and a willingness to dive into the world of advanced FPGA development.

## What's on the Agenda:

### 1. **LED Party:**
   - Start by mastering LED control using VHDL. It's like giving your DE10-Nano board a disco makeover.

### 2. **Double Trouble CPUs (Plus RISC-V!):**
   - Get ready for a CPU party! We'll explore Bare Metal config with the Avalon Memory Mapped Slave interface, teaming up with soft-core and hard-core CPUs:
      - Nios II CPU & RISC-V Nios V/m CPU
      - ARM Cortex-A9 Hard Processor System (HPS)

### 3. **Linux & RTOS Lights:**
   - Level up by configuring LEDs in an Embedded Linux (Buildroot) or Zephyr RTOS setup, guided by the ARM CPU.

### 4. **Ethernet, Protobuf, Security & OTA Magic:**
   - Ever dreamed of puppeteering your LEDs from a Web Dashboard over UDP/Protobuf, signing FIT images, and pushing live zero-JTAG OTA hardware+software updates with automatic rollback? We'll show you the ropes.

Ready to turn your DE10-Nano into an FPGA playground? Grab your board, and let's make some tech magic together! 🚀💡🤖
