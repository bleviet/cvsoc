# `cvsoc` Documentation Hub (Diátaxis Framework)

Welcome to the documentation for **`cvsoc`** — a hands-on engineering series for mastering Intel/Altera **Cyclone V SoC** development on the **Terasic DE10-Nano** board.

This documentation is structured according to the **[Diátaxis Framework](https://diataxis.fr/)**, which organizes technical documentation into four distinct quadrants based on what you need right now:

```
                        STUDY (Acquisition)                  WORK (Application)
               ┌────────────────────────────────────┬────────────────────────────────────┐
               │                                    │                                    │
   ACTION      │   1. TUTORIALS                     │   2. HOW-TO GUIDES                 │
   (Practical  │   Learning-oriented walkthroughs   │   Goal-oriented recipes for        │
    steps)     │   that guide you step-by-step      │   solving specific real-world      │
               │   through building each phase.     │   problems and troubleshooting.    │
               │                                    │                                    │
               ├────────────────────────────────────┼────────────────────────────────────┤
               │                                    │                                    │
   COGNITION   │   3. EXPLANATIONS                  │   4. REFERENCE                     │
   (Theoretical│   Understanding-oriented deep      │   Information-oriented lookup      │
    knowledge) │   dives into silicon architecture, │   tables for CLI commands, memory  │
               │   protocols, and "why it works".   │   maps, and project dependencies.  │
               │                                    │                                    │
               └────────────────────────────────────┴────────────────────────────────────┘
```

---

## Complete Phase-by-Phase Matrix (Tutorials ↔ Explanations ↔ How-To Guides)

Every tutorial in the repository is paired with an in-depth **Explanation** and relevant **How-To Guides**:

| Phase / Topic | Project Folder(s) | 🎓 Tutorial (Learning-Oriented) | 🧠 In-Depth Explanation (Understanding-Oriented) | 🛠️ Related How-To Guides (Problem-Oriented) |
| :--- | :--- | :--- | :--- | :--- |
| **Tooling & Containers** | `common/docker/`<br/>`common/make/` | • [Docker Dev Environment](tutorial_docker_dev_environment.md)<br/>• [Docker GUI (X11/WSLg)](tutorial_docker_gui.md)<br/>• [Phase 11: Multi-Version Quartus (23.1 & 25.1)](tutorial_phase11_multi_version_quartus.md) | [Explanation: Containerized FPGA Toolchains and Multi-Version Quartus Architecture](explanation_docker_and_multi_version_quartus.md) | • [Troubleshoot JTAG, USB-Blaster & `jtagd`](howto_troubleshoot_jtag_and_usb_blaster.md)<br/>• [Program from Windows When Working in WSL2](howto_windows_wsl2_programmer.md) |
| **Phase 0 & Phase 1:**<br/>Pure VHDL & Timing | `00_led_blinking/`<br/>`01_led_running/`<br/>`common/ip/` | • [Phase 0: LED Blinking](tutorial_phase0_led_blinking.md)<br/>• [Phase 1: LED Running](tutorial_phase1_led_running.md) | [Explanation: Pure VHDL Design, Reset Architecture, and Static Timing Analysis](explanation_phase0_phase1_vhdl_and_timing.md) | • [Troubleshoot JTAG, USB-Blaster & `jtagd`](howto_troubleshoot_jtag_and_usb_blaster.md) |
| **Phase 2:**<br/>Soft-Core CPUs (Nios II & Nios V) | `04_nios2_led/`<br/>`04b_niosv_led/` | • [Phase 2: Nios II Soft-Core CPU](tutorial_phase2_nios2.md)<br/>• [04b: Nios V/m RISC-V LED Demo](../04b_niosv_led/doc/README.md)<br/>• [Interactive C++ Register Shell](../04b_niosv_led/doc/REGISTER_SHELL_TUTORIAL.md) | [Explanation: Soft-Core Processors in FPGA Fabric — Nios II vs. Nios V/m](explanation_phase2_soft_cpu_nios2_and_niosv.md) | • [Migrate a Soft-Core Project from Nios II to Nios V/m](howto_migrate_nios2_to_niosv.md)<br/>• [Add a Custom Avalon-MM Peripheral (`_hw.tcl`)](howto_add_custom_avalon_mm_peripheral.md) |
| **Phase 3:**<br/>ARM HPS Bare-Metal | `05_hps_led/` | • [Phase 3: HPS Bare-Metal LED Control](tutorial_phase3_hps_baremetal.md)<br/>• [Hardware Debug & JTAG Setup](tutorial_debug_setup.md) | [Explanation: Cyclone V HPS Silicon Architecture, Bridges, Watchdogs, and the DDR3 PHY Compile Flow](explanation_phase3_hps_baremetal_architecture.md) | • [Fix HPS DDR3 Fitter Error 174068, Bridge Data Aborts & Watchdogs](howto_fix_hps_ddr3_fitter_and_bridge_errors.md)<br/>• [Add a Custom Avalon-MM Peripheral (`_hw.tcl`)](howto_add_custom_avalon_mm_peripheral.md) |
| **Phase 3B:**<br/>DDR3 & FPGA DMA Master | `14_ddr3_hps_test/`<br/>`15_ddr3_fpga_hps/` | • [Phase 3B: DDR3 Memory & Shared Access](tutorial_phase3b_ddr3_memory.md)<br/>• [14: DDR3 HPS Test](../14_ddr3_hps_test/doc/README.md)<br/>• [15: Shared DDR3 FPGA+HPS](../15_ddr3_fpga_hps/doc/README.md) | [Explanation: Cyclone V DDR3 Subsystem, F2H SDRAM Bridge, and Custom Avalon-MM Masters](explanation_phase3b_ddr3_and_avalon_master.md) | • [Add a Custom Avalon-MM Peripheral (`_hw.tcl`)](howto_add_custom_avalon_mm_peripheral.md)<br/>• [Fix HPS DDR3 Fitter Error 174068, Bridge Data Aborts & Watchdogs](howto_fix_hps_ddr3_fitter_and_bridge_errors.md) |
| **Phase 4:**<br/>Hardware Interrupts | `06_nios2_interrupts/`<br/>`06b_niosv_interrupts/`<br/>`07_hps_interrupts/` | • [Phase 4: Hardware Interrupts on Nios II and HPS](tutorial_phase4_interrupts.md)<br/>• [06b: Nios V/m Interrupt Demo](../06b_niosv_interrupts/doc/README.md) | [Explanation: Hardware Interrupt Subsystems — Nios II IIC, Nios V RISC-V CLINT, and ARM GICv1](explanation_phase4_interrupt_subsystems.md) | • [Migrate a Soft-Core Project from Nios II to Nios V/m](howto_migrate_nios2_to_niosv.md) |
| **Phase 5:**<br/>Remote GDB & IDE Debugging | `08_nios2_debug/`<br/>`08b_niosv_debug/`<br/>`09_hps_debug/` | • [Phase 5: Software Debugging with GDB](tutorial_phase5_gdb_debugging.md)<br/>• [08b: Nios V/m RISC-V GDB Debugging](../08b_niosv_debug/doc/README.md) | • [Explanation: Hardware JTAG Debug Architecture — Nios II, Nios V, and ARM CoreSight DAP](explanation_phase5_jtag_and_gdb_debugging.md)<br/>• [Explanation: IDE Debugging for Embedded Targets — VS Code and Neovim](explanation_ide_debugging.md) | • [Debug Nios II and ARM HPS with VS Code](howto_vscode_debugging.md)<br/>• [Troubleshoot JTAG, USB-Blaster & `jtagd`](howto_troubleshoot_jtag_and_usb_blaster.md) |
| **Phase 6:**<br/>Embedded Linux on HPS | `10_linux_led/` | • [Phase 6: Embedded Linux on HPS (Buildroot)](tutorial_phase6_embedded_linux.md) | [Explanation: Embedded Linux Boot Architecture, Buildroot, FPGA Manager, and `/dev/mem` MMIO](explanation_phase6_embedded_linux_and_fpga_manager.md) | • [Deploy Binaries, Kernel Modules & Bitstreams Over Ethernet or Serial](howto_deploy_to_board_over_serial_and_ethernet.md) |
| **Phase 7:**<br/>Ethernet, Protobuf & Web UI | `11_ethernet_hps_led/` | • [Phase 7: Ethernet LED Control over UDP, Nanopb Protobuf (§7.5) & Web Dashboard (§7.6)](tutorial_phase7_ethernet.md) | [Explanation: Embedded Networking, IPv6 SLAAC, Nanopb Protocol Buffers, and Async Web Gateways](explanation_phase7_networking_protobuf_and_dashboard.md) | • [Extend the Nanopb Protobuf Schema and Web Dashboard](howto_extend_protobuf_and_web_dashboard.md)<br/>• [Deploy Over Ethernet or Serial](howto_deploy_to_board_over_serial_and_ethernet.md)<br/>• [Field Guide: Deploying Without SSH](../11_ethernet_hps_led/doc/deploying_without_ssh.md) |
| **Phase 8:**<br/>Zephyr RTOS | `12_zephyr_led/` | • [Phase 8: Zephyr RTOS LED Control on DE10-Nano](tutorial_phase8_zephyr.md) | [Explanation: Zephyr RTOS Architecture, Compile-Time Devicetree, and Thread Synchronization](explanation_phase8_zephyr_rtos_architecture.md) | • [Phase 8 Q&A Reference](qa.md)<br/>• [Phase 8 Host Dependencies](INSTALL_DEPS_PHASE8.md) |
| **Phase 9:**<br/>Cybersecurity & Secure Boot | `13_secure_boot/` | • [Phase 9: Educational Secure Boot (AES-256 + Signed FIT)](../13_secure_boot/doc/README.md) | [Explanation: Embedded Cybersecurity — Bitstream Encryption (AES-256) and Signed U-Boot FIT Images](explanation_phase9_secure_boot_and_crypto.md) | • [Deploy Over Ethernet or Serial](howto_deploy_to_board_over_serial_and_ethernet.md) |
| **Phase 10:**<br/>Network OTA Field Update | `16_field_update/` | • [Phase 10: Network Field Update & Automatic Rollback (No JTAG)](../16_field_update/doc/README.md) | [Explanation: Atomic A/B Field Updates, Live FPGA Hot-Swap, and Automatic Rollback](explanation_phase10_ota_field_update_and_rollback.md) | • [Create, Test, and Troubleshoot OTA Firmware Bundles and Rollbacks](howto_create_and_test_ota_firmware_bundles.md) |

---

## 1. 🎓 Tutorials (Learning-Oriented)

Start here if you are learning Cyclone V SoC development step by step from the ground up:

1. **Environment Setup**
   - [Building a Custom FPGA Development Docker Image (`cvsoc/quartus:23.1`)](tutorial_docker_dev_environment.md)
   - [Visual Design Exploration — Launching Quartus & Platform Designer from Docker](tutorial_docker_gui.md)
   - [Multi-Version Quartus Support (`23.1` Docker + `25.1` Local + `cvsoc/tools:1.0`)](tutorial_phase11_multi_version_quartus.md)
2. **Pure VHDL Track**
   - [Phase 0 — Your First FPGA Design: LED Blinking on the DE10-Nano (`00_led_blinking`)](tutorial_phase0_led_blinking.md)
   - [Phase 1 — Shifting Light: LED Running on the DE10-Nano (`01_led_running`)](tutorial_phase1_led_running.md)
3. **Bare-Metal Soft-Core & Hard-Processor Track**
   - [Phase 2 — Nios II Soft-Core CPU with LED Control (`04_nios2_led`)](tutorial_phase2_nios2.md)
   - [Phase 2B — Nios V/m RISC-V Soft-Core & Interactive C++ Register Shell (`04b_niosv_led`)](../04b_niosv_led/doc/REGISTER_SHELL_TUTORIAL.md)
   - [Phase 3 — HPS Bare-Metal LED Control (`05_hps_led`)](tutorial_phase3_hps_baremetal.md)
   - [Hardware Debug Setup — Programming & Running on the DE10-Nano from WSL2](tutorial_debug_setup.md)
   - [Phase 3B — DDR3 Memory Validation & Shared FPGA-HPS DMA (`14_ddr3_hps_test`, `15_ddr3_fpga_hps`)](tutorial_phase3b_ddr3_memory.md)
   - [Phase 4 — Hardware Interrupts on Nios II and HPS (`06`, `06b`, `07`)](tutorial_phase4_interrupts.md)
   - [Phase 5 — Software Debugging with GDB (`08`, `08b`, `09`)](tutorial_phase5_gdb_debugging.md)
4. **Operating Systems, Networking & Production Deployment Track**
   - [Phase 6 — Embedded Linux on HPS with Buildroot (`10_linux_led`)](tutorial_phase6_embedded_linux.md)
   - [Phase 7 — Ethernet LED Control over UDP, Nanopb Protobuf, and Web Dashboard (`11_ethernet_hps_led`)](tutorial_phase7_ethernet.md)
   - [Phase 8 — Zephyr RTOS LED Control on DE10-Nano (`12_zephyr_led`)](tutorial_phase8_zephyr.md)
   - [Phase 9 — Educational Secure Boot: Bitstream Encryption & Signed FIT Images (`13_secure_boot`)](../13_secure_boot/doc/README.md)
   - [Phase 10 — Zero-JTAG Network Field Update & Automatic Rollback (`16_field_update`)](../16_field_update/doc/README.md)

---

## 2. 🧠 Explanations (Understanding-Oriented)

Read these when you want to understand *why* the hardware, silicon, protocols, and build systems work the way they do:

- [Containerized FPGA Toolchains and Multi-Version Quartus Architecture](explanation_docker_and_multi_version_quartus.md)
- [Pure VHDL Design, Reset Architecture, and Static Timing Analysis (Phases 0 & 1)](explanation_phase0_phase1_vhdl_and_timing.md)
- [Soft-Core Processors in FPGA Fabric — Nios II vs. Nios V/m (Phase 2)](explanation_phase2_soft_cpu_nios2_and_niosv.md)
- [Cyclone V HPS Silicon Architecture, Bridges, Watchdogs, and the DDR3 PHY Compile Flow (Phase 3)](explanation_phase3_hps_baremetal_architecture.md)
- [Cyclone V DDR3 Subsystem, F2H SDRAM Bridge, and Custom Avalon-MM Masters (Phase 3B)](explanation_phase3b_ddr3_and_avalon_master.md)
- [Hardware Interrupt Subsystems — Nios II IIC, Nios V RISC-V CLINT, and ARM GICv1 (Phase 4)](explanation_phase4_interrupt_subsystems.md)
- [Hardware JTAG Debug Architecture — Nios II, Nios V RISC-V, and ARM CoreSight DAP (Phase 5)](explanation_phase5_jtag_and_gdb_debugging.md)
- [IDE Debugging for Embedded Targets — VS Code and Neovim (Phase 5)](explanation_ide_debugging.md)
- [Embedded Linux Boot Architecture, Buildroot, FPGA Manager, and `/dev/mem` MMIO (Phase 6)](explanation_phase6_embedded_linux_and_fpga_manager.md)
- [Embedded Networking, IPv6 SLAAC, Nanopb Protocol Buffers, and Async Web Gateways (Phase 7)](explanation_phase7_networking_protobuf_and_dashboard.md)
- [Zephyr RTOS Architecture, Compile-Time Devicetree, and Thread Synchronization (Phase 8)](explanation_phase8_zephyr_rtos_architecture.md)
- [Embedded Cybersecurity — Bitstream Encryption (AES-256) and Signed U-Boot FIT Images (Phase 9)](explanation_phase9_secure_boot_and_crypto.md)
- [Atomic A/B Field Updates, Live FPGA Hot-Swap, and Automatic Rollback (Phase 10)](explanation_phase10_ota_field_update_and_rollback.md)

---

## 3. 🛠️ How-To Guides (Problem-Oriented)

Use these step-by-step recipes when you have a specific task to accomplish or a problem to fix:

- [How-to: Troubleshoot JTAG, USB-Blaster II, and `jtagd` Issues Across WSL2, Linux, and Docker](howto_troubleshoot_jtag_and_usb_blaster.md)
- [How-to: Program the DE10-Nano from Windows When Working in WSL2](howto_windows_wsl2_programmer.md)
- [How-to: Debug Nios II and ARM HPS with VS Code](howto_vscode_debugging.md)
- [How-to: Create and Integrate a Custom Avalon-MM Slave or Master Peripheral in Platform Designer](howto_add_custom_avalon_mm_peripheral.md)
- [How-to: Fix HPS DDR3 Fitter Error 174068, Bridge Data Aborts, and Watchdog Resets](howto_fix_hps_ddr3_fitter_and_bridge_errors.md)
- [How-to: Migrate a Soft-Core Project from Nios II (Quartus 23.1) to Nios V/m RISC-V (Quartus 25.1)](howto_migrate_nios2_to_niosv.md)
- [How-to: Deploy Binaries, Kernel Modules, and Bitstreams to the DE10-Nano Over Ethernet or Serial](howto_deploy_to_board_over_serial_and_ethernet.md)
- [How-to: Extend the Nanopb Protobuf Schema and Web Dashboard with New Commands](howto_extend_protobuf_and_web_dashboard.md)
- [How-to: Create, Test, and Troubleshoot OTA Firmware Bundles and Automatic Rollbacks](howto_create_and_test_ota_firmware_bundles.md)
- [Field Guide: Deploying to an Embedded Board Without SSH (`11_ethernet_hps_led`)](../11_ethernet_hps_led/doc/deploying_without_ssh.md)

---

## 4. 📖 Reference (Information-Oriented)

Factual lookup tables for commands, dependencies, and project planning:

- [Command Reference (`quartus_sh`, `qsys-script`, `nios2-bsp`, `openocd`, `buildroot`, `west`)](command-reference.md)
- [Project Dependencies & Binary Artifact Graph](project_dependencies.md)
- [Phase 8: Zephyr RTOS — Q&A](qa.md)
- [Phase 8: Host Dependencies Installation Reference](INSTALL_DEPS_PHASE8.md)
- [Project Roadmap](roadmap.md)
- [About the Series](about.md)
