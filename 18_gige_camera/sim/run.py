#!/usr/bin/env python3
"""Run the cocotb RTL tests with GHDL.

  python3 sim/run.py              # all configurations
  python3 sim/run.py --width 2592 # one SENSOR_WIDTH

Each SENSOR_WIDTH changes the color-bar boundaries, so the tests run once
with a small width (fast) and once with the real D5M width.
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
        sources=[PROJECT / "hdl" / "cam_tpg_writer.vhd"],
        hdl_toplevel="cam_tpg_writer",
        build_args=["--std=08"],
        parameters={"SENSOR_WIDTH": width},
        build_dir=build_dir,
        always=True,
    )
    os.environ["SENSOR_WIDTH"] = str(width)
    results = runner.test(
        hdl_toplevel="cam_tpg_writer",
        test_module="test_cam_tpg_writer",
        test_dir=SIM_DIR,
        build_dir=build_dir,
        test_args=["--std=08", f"--workdir={build_dir}"],
        parameters={"SENSOR_WIDTH": width},
    )
    return results


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--width", type=int, action="append",
                   help="SENSOR_WIDTH generic (default: 640 and 2592)")
    args = p.parse_args()
    for width in args.width or [640, 2592]:
        print(f"=== cam_tpg_writer, SENSOR_WIDTH={width} ===", flush=True)
        run(width)


if __name__ == "__main__":
    sys.exit(main())
