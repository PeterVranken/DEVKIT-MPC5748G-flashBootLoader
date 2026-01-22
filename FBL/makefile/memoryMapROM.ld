/* This file defines the memory map of physical memory for the linker. We have a single
   contiguous ROM area, where the linker will place the text sections and an also
   contiguous RAM area, where all data sections go.
     A alternative memory partitioning is defined in the other file memoryMapRAM.ld, which
   allows code execution from RAM (for ease of development and debugging). Some makefile
   conditional code decides whether this file or the other memory map definition is read by
   the linker.
     Different to the other samples, this application sees only 192kB of ROM. This is the
   space occupied by the first two RWW flash partitions. The FBL's code may use this space
   and at the same time, this space is not available to code the FBL can erase, down- or
   upload or program. */
ASSERT(ld_linkInRAM == 0, "This memory map is intended for build in ROM only")
ld_textSegStartAddr =  0x00F90000;
ld_textSegSize      =  0x00FC0000 - 0X00F90000;
ld_dataSegStartAddr =  0x40000000;
ld_dataSegSize      =  3*256k ;
