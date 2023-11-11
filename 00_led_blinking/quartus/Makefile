all: project compile

PROJECT_NAME = 00_led_blinking
REVISION_NAME = de10_nano

project:
	quartus_sh -t de10_nano_project.tcl

analysis:
	quartus_map $(PROJECT_NAME) -c $(REVISION_NAME)

fit:
	quartus_fit $(PROJECT_NAME) -c $(REVISION_NAME)

asm:
	quartus_asm $(PROJECT_NAME) -c $(REVISION_NAME)

sta:
	quartus_sta $(PROJECT_NAME) -c $(REVISION_NAME)

compile:
	quartus_sh --flow compile $(PROJECT_NAME) -c $(REVISION_NAME)


FILE_ENDINGS_FOR_REMOVAL = .rpt .summary .qsf .qpf .qws .done .smsg .jdi .pin .sld .sof dump.txt
clean:
	rm -rf db incremental_db
	rm -rf output_files
	rm -f $(foreach ENDING,$(FILE_ENDINGS_FOR_REMOVAL),$(wildcard *$(ENDING)))
