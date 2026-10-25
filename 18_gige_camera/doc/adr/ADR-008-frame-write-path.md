# ADR-008: Frame writes use the FPGA-to-HPS AXI bridge in M1

- **Status**: Accepted (M1). Revisit in M5.
- **Date**: 2026-10-25
- **Plan**: [GigE Vision camera product plan](../../../docs/plans/2026-10-25-gige-vision-camera.md), §4–5

## Context

The FPGA must write frames into HPS DDR. Cyclone V offers two paths:

1. **FPGA-to-SDRAM ports** (`f2h_sdram*`): direct ports into the SDRAM
   controller, the highest bandwidth and lowest latency. Their configuration
   (port widths and command/FIFO mappings: `cportwidth`, `cportwmap`,
   `cportrmap`, `rfifocmap`, `wfifocmap`, `fpgaportrst`) is written by the
   preloader from the handoff of the Quartus design it was built for. Applying
   a new configuration (`staticcfg.applycfg`) requires the SDRAM to be idle,
   which isn't the case once Linux runs from it.
2. **FPGA-to-HPS AXI bridge** (`f2h_axi_slave`): an AXI slave into the L3
   interconnect. FPGA masters see the HPS address map, so DDR is at its
   physical address. Enabling it needs only its bit in the Reset Manager's
   `BRGMODRST` cleared, which Linux can do at any time.

The board runs the `16_field_update` image. Its preloader configured the
SDRAM ports for that image's own design (read on the board, 2026-10-25:
`fpgaportrst = 0x1FF`, `cportwidth = 0x00044005`, …), and Linux loads the
camera bitstream later through `fpga_load`. Nothing guarantees those port
settings match a new design with a single 64-bit port.

## Decision

M1 writes frames through the **FPGA-to-HPS AXI bridge** (64-bit, Avalon-MM
burst master translated to AXI by Platform Designer). The design has no
FPGA-to-SDRAM ports. `camd --fpga` clears `BRGMODRST` bit 2 at start.

## Consequences

- Bandwidth is far beyond M1's needs: full-res Bayer8 at 14 fps is about
  70 MB/s.
- Frame traffic shares the L3 interconnect with the CPU. At the M5 target
  rates, with the HDMI reader added, this needs measuring.
- The bridge is not cache-coherent, which matches the uncached frame-buffer
  mapping chosen in ADR-001 (R8).
- **M5:** move to an FPGA-to-SDRAM port, together with an `18_gige_camera`
  Buildroot image whose preloader is built from this design's handoff, so
  the port configuration is right by construction.
