#!/usr/bin/env python3
"""Patch altdq_dqs2_acv_connect_to_hard_phy_cyclonev.sv.

qsys-generate declares USE_TERMINATION_CONTROL in the module but never
forwards it to the cyclonev_io_obuf primitives, so Quartus Fitter raises
Error 174068 when SERIESTERMINATIONCONTROL is connected on a buffer that does
not have use_termination_control="true".

The parent module (hps_sdram_p0_altdqdqs.v) overrides the module parameter
to "true" via defparam, but Quartus cannot propagate defparam-overridden
string values into primitive-level parameters at elaboration time.

Fix: hardcode use_termination_control="true" on every cyclonev_io_obuf
instance that has seriesterminationcontrol connected to a real signal.
"""
import sys

# Each tuple: (old_text, new_text, instance_name)
PATCHES = [
    (
        # obuf_os_bar_0 – differential strobe-bar output (bidir strobe case)
        '\t\t\tcyclonev_io_obuf\n'
        '\t\t\t#(\n'
        '\t\t\t  \t.sim_dynamic_termination_control_is_connected("true"),\n'
        '\t\t\t\t.bus_hold("false"),\n'
        '\t\t\t\t.open_drain_output("false")\n'
        '\t\t\t) obuf_os_bar_0',
        '\t\t\tcyclonev_io_obuf\n'
        '\t\t\t#(\n'
        '\t\t\t  \t.sim_dynamic_termination_control_is_connected("true"),\n'
        '\t\t\t\t.use_termination_control("true"),\n'
        '\t\t\t\t.bus_hold("false"),\n'
        '\t\t\t\t.open_drain_output("false")\n'
        '\t\t\t) obuf_os_bar_0',
        'obuf_os_bar_0',
    ),
    (
        # obuf_os_0 – bidirectional DQS strobe output
        '\t\tcyclonev_io_obuf\n'
        '\t\t#(\n'
        '\t\t  \t.sim_dynamic_termination_control_is_connected("true"),\n'
        '\t\t\t.bus_hold("false"),\n'
        '\t\t\t.open_drain_output("false")\n'
        '\t\t) obuf_os_0',
        '\t\tcyclonev_io_obuf\n'
        '\t\t#(\n'
        '\t\t  \t.sim_dynamic_termination_control_is_connected("true"),\n'
        '\t\t\t.use_termination_control("true"),\n'
        '\t\t\t.bus_hold("false"),\n'
        '\t\t\t.open_drain_output("false")\n'
        '\t\t) obuf_os_0',
        'obuf_os_0',
    ),
    (
        # data_out (bidir) – per-bit DQ output in bidir generate loop
        '\t\t\tcyclonev_io_obuf\n'
        '\t\t\t#(\n'
        '\t\t\t  \t.sim_dynamic_termination_control_is_connected("true")\n'
        '\t\t\t  ) data_out (',
        '\t\t\tcyclonev_io_obuf\n'
        '\t\t\t#(\n'
        '\t\t\t  \t.sim_dynamic_termination_control_is_connected("true"),\n'
        '\t\t\t  \t.use_termination_control("true")\n'
        '\t\t\t  ) data_out (',
        'data_out (bidir)',
    ),
    (
        # data_out (output) – per-bit output in unidirectional generate loop
        # Changes inline instantiation to parameterized form
        '\t\t\t\tcyclonev_io_obuf data_out (',
        '\t\t\t\tcyclonev_io_obuf\n'
        '\t\t\t\t#(\n'
        '\t\t\t\t\t.use_termination_control("true")\n'
        '\t\t\t\t) data_out (',
        'data_out (output)',
    ),
    (
        # obuf_1 – DM (data mask) output in extra_output_pad_gen generate loop
        '\t\tcyclonev_io_obuf obuf_1 (',
        '\t\tcyclonev_io_obuf\n'
        '\t\t#(\n'
        '\t\t\t.use_termination_control("true")\n'
        '\t\t) obuf_1 (',
        'obuf_1',
    ),
]


def patch(filepath: str) -> None:
    with open(filepath, 'r') as fh:
        content = fh.read()

    changed = False
    for old, new, name in PATCHES:
        if new in content:
            print("  already patched: " + name)
        elif old in content:
            content = content.replace(old, new, 1)
            print("  patched " + name)
            changed = True
        else:
            print("  WARNING: pattern not found for " + name + " - skipping")

    if changed:
        with open(filepath, 'w') as fh:
            fh.write(content)
        print("  wrote " + filepath)
    else:
        print("  no changes needed: " + filepath)


if __name__ == '__main__':
    if len(sys.argv) != 2:
        print("Usage: " + sys.argv[0] + " <path/to/altdq_dqs2_acv_connect_to_hard_phy_cyclonev.sv>")
        sys.exit(1)
    patch(sys.argv[1])
