# Tutorial series for stepping into advanced FPGA development
* Prerequisites
  * Already some experience with FPGA development in VHDL
  * Eager to learn 
* First you will learn how to setup your development environment
  * Native On Windows
  * Native On Linux
  * Or you can grab our provided Docker image on dockerhub and immediately start exploring. Our docker image provide everything required to follow the tutorial series.
* Basic idea is simple: Controlling the LED on various ways
  * VHDL Only
  * Bare Metal configuration via Avalon Memory Mapped Slave interface with Nios II CPU.
  * Bare Metal configuration via Avalon Memory Mapped Slave interface with the ARM CPU
  * Embedded Linux configuration with the ARM CPU
  * Control the LED from a PC through Ethernet
* On the way you will learn best practices of Intel FPGA development based (Cyclone V SoC) on the DE10-Nano board.
* You will learn about scripting and about Continous Development/Continous Integration CI/CD as well
* You will learn about clean coding, meaningful modularization, proper coding style
* You will learn to use open source VHDL simulator GHDL and various possiblities of simualtion framework like VUnit or cocotb.
