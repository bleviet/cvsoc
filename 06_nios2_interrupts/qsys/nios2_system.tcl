package require -exact qsys 12.0

# ---------------------------------------------------------------------------
# Nios II interrupt demo system for DE10-Nano (Cyclone V 5CSEBA6U23I7)
#
# Extends 04_nios2_led with a 2-bit button PIO and interrupt handling.
#
# Components:
#   - Nios II/e CPU with HAL IRQ controller
#   - On-chip memory (32 KB, code + data)
#   - JTAG UART (printf output)
#   - System ID
#   - LED PIO (8-bit output)
#   - Button PIO (2-bit input, edge-capture IRQ for KEY[0] and KEY[1])
#
# Memory map (data bus):
#   0x00000000  on-chip RAM     32 KB
#   0x00010010  led_pio         16 B   (IRQ —)
#   0x00010020  button_pio      16 B   (IRQ 1, falling-edge)
#   0x00010100  jtag_uart        8 B   (IRQ 0)
#   0x00010108  sysid            8 B
#   0x00010800  nios2 debug      2 KB
# ---------------------------------------------------------------------------

create_system nios2_system
set_project_property DEVICE_FAMILY {Cyclone V}
set_project_property DEVICE {5CSEBA6U23I7}

# ── Clock bridge (50 MHz from top-level) ─────────────────────────────────────
add_instance clk_0 altera_clock_bridge
set_instance_parameter_value clk_0 NUM_CLOCK_OUTPUTS 1

# ── Reset bridge (active-high synchronous reset) ──────────────────────────────
add_instance reset_bridge altera_reset_bridge
set_instance_parameter_value reset_bridge NUM_RESET_OUTPUTS {1}
set_instance_parameter_value reset_bridge ACTIVE_LOW_RESET {0}

# ── Nios II/e (tiny) CPU ──────────────────────────────────────────────────────
add_instance nios2 altera_nios2_gen2
set_instance_parameter_value nios2 impl {Tiny}
set_instance_parameter_value nios2 setting_preciseIllegalMemAccessException {0}

# ── On-chip memory: 32 KB (instruction + data) ───────────────────────────────
add_instance onchip_mem altera_avalon_onchip_memory2
set_instance_parameter_value onchip_mem memorySize {32768}
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

# ── Button PIO (2-bit input, falling-edge IRQ) ────────────────────────────────
# KEY[0] and KEY[1] are active-low; falling edge = button pressed.
# captureEdge enables the edge-capture register (cleared by writing any value).
# generateIRQ + irqType=EDGE: IRQ is asserted whenever the edge-capture register
# has a bit set that is also set in the IRQ mask register.
add_instance button_pio altera_avalon_pio
set_instance_parameter_value button_pio width        {2}
set_instance_parameter_value button_pio direction    {Input}
set_instance_parameter_value button_pio resetValue   {0}
set_instance_parameter_value button_pio captureEdge  {1}
set_instance_parameter_value button_pio edgeType     {FALLING}
set_instance_parameter_value button_pio generateIRQ  {1}
set_instance_parameter_value button_pio irqType      {EDGE}

# ── Clock connections ─────────────────────────────────────────────────────────
add_connection clk_0.out_clk reset_bridge.clk
add_connection clk_0.out_clk nios2.clk
add_connection clk_0.out_clk onchip_mem.clk1
add_connection clk_0.out_clk jtag_uart.clk
add_connection clk_0.out_clk sysid.clk
add_connection clk_0.out_clk led_pio.clk
add_connection clk_0.out_clk button_pio.clk

# ── Reset connections ─────────────────────────────────────────────────────────
add_connection reset_bridge.out_reset nios2.reset
add_connection reset_bridge.out_reset onchip_mem.reset1
add_connection reset_bridge.out_reset jtag_uart.reset
add_connection reset_bridge.out_reset sysid.reset
add_connection reset_bridge.out_reset led_pio.reset
add_connection reset_bridge.out_reset button_pio.reset

# ── Avalon-MM data bus ────────────────────────────────────────────────────────
add_connection nios2.data_master onchip_mem.s1
add_connection nios2.data_master jtag_uart.avalon_jtag_slave
add_connection nios2.data_master sysid.control_slave
add_connection nios2.data_master led_pio.s1
add_connection nios2.data_master button_pio.s1
add_connection nios2.data_master nios2.debug_mem_slave

# ── Avalon-MM instruction bus ─────────────────────────────────────────────────
add_connection nios2.instruction_master onchip_mem.s1
add_connection nios2.instruction_master nios2.debug_mem_slave

# ── IRQ connections ───────────────────────────────────────────────────────────
# nios2.irq is the IRQ receiver (internal interrupt controller).
# Lower irqNumber = higher priority.
add_connection nios2.irq jtag_uart.irq
set_connection_parameter_value nios2.irq/jtag_uart.irq irqNumber {0}

add_connection nios2.irq button_pio.irq
set_connection_parameter_value nios2.irq/button_pio.irq irqNumber {1}

# ── Base address map ──────────────────────────────────────────────────────────
set_connection_parameter_value nios2.data_master/onchip_mem.s1                  baseAddress {0x00000000}
set_connection_parameter_value nios2.instruction_master/onchip_mem.s1           baseAddress {0x00000000}
set_connection_parameter_value nios2.data_master/led_pio.s1                     baseAddress {0x00010010}
set_connection_parameter_value nios2.data_master/button_pio.s1                  baseAddress {0x00010020}
set_connection_parameter_value nios2.data_master/jtag_uart.avalon_jtag_slave    baseAddress {0x00010100}
set_connection_parameter_value nios2.data_master/sysid.control_slave            baseAddress {0x00010108}
set_connection_parameter_value nios2.data_master/nios2.debug_mem_slave          baseAddress {0x00010800}
set_connection_parameter_value nios2.instruction_master/nios2.debug_mem_slave   baseAddress {0x00010800}

# ── Reset / exception vectors (both in on-chip RAM) ──────────────────────────
set_instance_parameter_value nios2 resetSlave      {onchip_mem.s1}
set_instance_parameter_value nios2 resetOffset     {0x00000000}
set_instance_parameter_value nios2 exceptionSlave  {onchip_mem.s1}
set_instance_parameter_value nios2 exceptionOffset {0x00000020}

# ── Top-level port exports ────────────────────────────────────────────────────
add_interface clk clock sink
set_interface_property clk EXPORT_OF clk_0.in_clk

add_interface reset reset sink
set_interface_property reset EXPORT_OF reset_bridge.in_reset

add_interface led_external_connection conduit end
set_interface_property led_external_connection EXPORT_OF led_pio.external_connection

add_interface button_external_connection conduit end
set_interface_property button_external_connection EXPORT_OF button_pio.external_connection

save_system nios2_system.qsys
puts "nios2_system.qsys saved"
