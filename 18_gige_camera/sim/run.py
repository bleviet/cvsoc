#!/usr/bin/env python3
"""Run the cocotb RTL tests with GHDL.

  python3 sim/run.py              # all configurations
  python3 sim/run.py --width 2592 # cam_frame_writer at one SENSOR_WIDTH only
  python3 sim/run.py --hdmi       # hdmi_preview only

Each SENSOR_WIDTH changes the color-bar boundaries, so the writer tests run
once with a small width (fast) and once with the real D5M width.
hdmi_preview runs on a tiny screen (HDMI_GENERICS, matching
test_hdmi_preview.py) so a display frame takes a few thousand clocks.
"""

import argparse
import os
import sys
from pathlib import Path

from cocotb_tools.runner import get_runner

SIM_DIR = Path(__file__).resolve().parent
PROJECT = SIM_DIR.parent


def run(width):
    build_dir = SIM_DIR / "sim_build" / f"w{width}"
    runner = get_runner("ghdl")
    runner.build(
        sources=[PROJECT / "hdl" / f for f in
                 ("cam_pkg.vhd", "async_fifo.vhd", "d5m_capture.vhd", "cam_frame_writer.vhd")],
        hdl_toplevel="cam_frame_writer",
        build_args=["--std=08"],
        parameters={"SENSOR_WIDTH": width},
        build_dir=build_dir,
        always=True,
    )
    os.environ["SENSOR_WIDTH"] = str(width)
    results = runner.test(
        hdl_toplevel="cam_frame_writer",
        test_module="test_cam_frame_writer",
        test_dir=SIM_DIR,
        build_dir=build_dir,
        test_args=["--std=08", f"--workdir={build_dir}"],
        parameters={"SENSOR_WIDTH": width},
    )
    return results


HDMI_GENERICS = dict(H_ACTIVE=64, H_FP=4, H_SYNC=4, H_BP=8,
                     V_ACTIVE=8, V_FP=1, V_SYNC=1, V_BP=2)


def run_hdmi():
    build_dir = SIM_DIR / "sim_build" / "hdmi"
    runner = get_runner("ghdl")
    runner.build(
        sources=[PROJECT / "hdl" / "hdmi_preview.vhd"],
        hdl_toplevel="hdmi_preview",
        build_args=["--std=08"],
        parameters=HDMI_GENERICS,
        build_dir=build_dir,
        always=True,
    )
    return runner.test(
        hdl_toplevel="hdmi_preview",
        test_module="test_hdmi_preview",
        test_dir=SIM_DIR,
        build_dir=build_dir,
        test_args=["--std=08", f"--workdir={build_dir}"],
        parameters=HDMI_GENERICS,
    )


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--width", type=int, action="append",
                   help="SENSOR_WIDTH generic (default: 640 and 2592)")
    p.add_argument("--hdmi", action="store_true", help="hdmi_preview only")
    args = p.parse_args()
    if not args.hdmi:
        for width in args.width or [640, 2592]:
            print(f"=== cam_frame_writer, SENSOR_WIDTH={width} ===", flush=True)
            run(width)
    if args.hdmi or not args.width:
        print("=== hdmi_preview ===", flush=True)
        run_hdmi()


if __name__ == "__main__":
    sys.exit(main())
