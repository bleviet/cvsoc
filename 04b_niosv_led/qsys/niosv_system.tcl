package require -exact qsys 16.1

# ---------------------------------------------------------------------------
# Nios V/m LED demo system for DE10-Nano (Cyclone V 5CSEBA6U23I7)
# Components: Nios V/m CPU, 64 KB on-chip memory, JTAG UART, System ID, LED PIO
# Quartus 25.1 only (intel_niosv_m is not available in 23.1).
#
# Nios V/m exposes its Avalon-MM masters as "data_manager" and
# "instruction_manager", the interrupt receiver as "platform_irq_rx", and
# the clock/reset sinks as "clk"/"reset" (exported from the CPU's internal
# clock/reset bridges). The debug module ("dm_agent") and the internal
# timer/software-interrupt block ("timer_sw_agent") are connected back to
# the CPU's own masters, mirroring the Intel example designs.
# ---------------------------------------------------------------------------

create_system niosv_system
set_project_property DEVICE_FAMILY {Cyclone V}
set_project_property DEVICE {5CSEBA6U23I7}

# ── Clock bridge (50 MHz from top-level) ─────────────────────────────────────
add_instance clk_0 altera_clock_bridge
set_instance_parameter_value clk_0 NUM_CLOCK_OUTPUTS 1
set_instance_parameter_value clk_0 EXPLICIT_CLOCK_RATE {50000000}

# ── Reset bridge (active-high synchronous reset) ──────────────────────────────
add_instance reset_bridge altera_reset_bridge
set_instance_parameter_value reset_bridge NUM_RESET_OUTPUTS {1}
set_instance_parameter_value reset_bridge ACTIVE_LOW_RESET {0}

# ── Nios V/m (RV32I) CPU ──────────────────────────────────────────────────────
# Minimal core: defaults are used (pipelined, debug enabled, reset vector 0x0).
# enableAvalonInterface is set so the masters are Avalon-MM and drive the
# Avalon peripherals directly without an AXI adapter.
add_instance niosv intel_niosv_m
set_instance_parameter_value niosv enableAvalonInterface {True}

# ── On-chip memory: 64 KB (instruction + data) ───────────────────────────────
add_instance onchip_mem altera_avalon_onchip_memory2
set_instance_parameter_value onchip_mem memorySize {65536}
set_instance_parameter_value onchip_mem dataWidth {32}
set_instance_parameter_value onchip_mem singleClockOperation {1}

# ── JTAG UART ─────────────────────────────────────────────────────────────────
add_instance jtag_uart altera_avalon_jtag_uart

# ── System ID ─────────────────────────────────────────────────────────────────
add_instance sysid altera_avalon_sysid_qsys

# ── LED PIO (8-bit output) ────────────────────────────────────────────────────
add_instance led_pio altera_avalon_pio
set_instance_parameter_value led_pio width {8}
set_instance_parameter_value led_pio direction {Output}
set_instance_parameter_value led_pio resetValue {0}

# ── Clock connections ─────────────────────────────────────────────────────────
add_connection clk_0.out_clk reset_bridge.clk
add_connection clk_0.out_clk niosv.clk
add_connection clk_0.out_clk onchip_mem.clk1
add_connection clk_0.out_clk jtag_uart.clk
add_connection clk_0.out_clk sysid.clk
add_connection clk_0.out_clk led_pio.clk

# ── Reset connections ─────────────────────────────────────────────────────────
add_connection reset_bridge.out_reset niosv.reset
add_connection reset_bridge.out_reset onchip_mem.reset1
add_connection reset_bridge.out_reset jtag_uart.reset
add_connection reset_bridge.out_reset sysid.reset
add_connection reset_bridge.out_reset led_pio.reset

# ── Avalon-MM data bus ────────────────────────────────────────────────────────
add_connection niosv.data_manager onchip_mem.s1
add_connection niosv.data_manager jtag_uart.avalon_jtag_slave
add_connection niosv.data_manager sysid.control_slave
add_connection niosv.data_manager led_pio.s1
add_connection niosv.data_manager niosv.timer_sw_agent
add_connection niosv.data_manager niosv.dm_agent

# ── Avalon-MM instruction bus ─────────────────────────────────────────────────
add_connection niosv.instruction_manager onchip_mem.s1
add_connection niosv.instruction_manager niosv.dm_agent

# ── IRQ: niosv.platform_irq_rx is the receiver, peripherals are senders ───────
add_connection niosv.platform_irq_rx jtag_uart.irq
set_connection_parameter_value niosv.platform_irq_rx/jtag_uart.irq irqNumber {0}

# ── Base address map ──────────────────────────────────────────────────────────
set_connection_parameter_value niosv.data_manager/onchip_mem.s1               baseAddress {0x00000000}
set_connection_parameter_value niosv.instruction_manager/onchip_mem.s1        baseAddress {0x00000000}
set_connection_parameter_value niosv.data_manager/led_pio.s1                  baseAddress {0x00010010}
set_connection_parameter_value niosv.data_manager/jtag_uart.avalon_jtag_slave baseAddress {0x00010100}
set_connection_parameter_value niosv.data_manager/sysid.control_slave         baseAddress {0x00010108}
set_connection_parameter_value niosv.data_manager/niosv.timer_sw_agent        baseAddress {0x00010140}
set_connection_parameter_value niosv.data_manager/niosv.dm_agent              baseAddress {0x00100000}
set_connection_parameter_value niosv.instruction_manager/niosv.dm_agent       baseAddress {0x00100000}

# ── Reset vector (in on-chip RAM at 0x0) ─────────────────────────────────────
set_instance_parameter_value niosv resetSlave  {Absolute}
set_instance_parameter_value niosv resetOffset {0x00000000}

# ── Top-level port exports ────────────────────────────────────────────────────
add_interface clk clock sink
set_interface_property clk EXPORT_OF clk_0.in_clk

add_interface reset reset sink
set_interface_property reset EXPORT_OF reset_bridge.in_reset

add_interface led_external_connection conduit end
set_interface_property led_external_connection EXPORT_OF led_pio.external_connection

save_system niosv_system.qsys
puts "niosv_system.qsys saved"
