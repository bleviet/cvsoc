# LED Blinking Project

## Overview
This is a kickstart project that makes all the LEDs on the DE10-Nano board blink. The project is implemented purely in VHDL and uses a script-based project creation and compilation process with a Makefile.

## Project Structure
The project is structured as follows:

- `doc/`: Contains documentation related to the project.
- `hdl/`: Contains the VHDL source files for the project.
  - `de10_nano_top.vhd`: The top-level module for the project.
  - `led_blinking.vhd`: The VHDL file that implements the LED blinking functionality.
- `quartus/`: Contains Quartus project files and scripts.
  - `de10_nano.sdc`: The Synopsys Design Constraints (SDC) file for the project.
  - `de10_nano_pin_assignments.tcl`: The pin assignments for the DE10-Nano board.
  - `de10_nano_project.tcl`: The TCL script to create the Quartus project.
  - `Makefile`: The Makefile for automating the project compilation and analysis.
- `sim/`: Contains simulation files for the project.

## How to Build
To build this project, navigate to the `quartus/` directory and run the following command in your terminal:

```sh
make all
```
