/*
 * xml_blob.c — embeds genicam/cvsoc_camera.xml into camd, so the GenICam
 * description always matches the binary that serves it.
 */
__asm__(
    "    .section .rodata\n"
    "    .global camd_xml_start\n"
    "    .global camd_xml_end\n"
    "    .balign 4\n"
    "camd_xml_start:\n"
    "    .incbin \"genicam/cvsoc_camera.xml\"\n"
    "camd_xml_end:\n"
    "    .previous\n");
