# This Makefile is used to automate the compilation and analysis of a Quartus project.
# It provides targets for creating the project, performing analysis, fitting, assembling,
# performing static timing analysis, and compiling the project.
# Additionally, it includes a target for cleaning up generated files.

all: project compile check_timing

PROJECT_NAME = 00_led_blinking
REVISION_NAME = de10_nano

# Target for creating the Quartus project
project:
	quartus_sh -t de10_nano_project.tcl

# Target for performing analysis on the project
analysis:
	quartus_map $(PROJECT_NAME) -c $(REVISION_NAME)

# Target for fitting the project
fit:
	quartus_fit $(PROJECT_NAME) -c $(REVISION_NAME)

# Target for assembling the project
asm:
	quartus_asm $(PROJECT_NAME) -c $(REVISION_NAME)

# Target for performing static timing analysis on the project
sta:
	quartus_sta $(PROJECT_NAME) -c $(REVISION_NAME)

# Target for compiling the project
compile:
	quartus_sh --flow compile $(PROJECT_NAME) -c $(REVISION_NAME)

# Target for check timing slacks
check_timing:
	python ../scripts/check_timing_slacks.py $(REVISION_NAME).sta.rpt

# List of file endings to be removed during the clean target
FILE_ENDINGS_FOR_REMOVAL = .rpt .summary .qsf .qpf .qws .done .smsg .jdi .pin .sld .sof dump.txt

# Target for cleaning up generated files
clean:
	rm -rf db incremental_db
	rm -rf output_files
	rm -f $(foreach ENDING,$(FILE_ENDINGS_FOR_REMOVAL),$(wildcard *$(ENDING)))
