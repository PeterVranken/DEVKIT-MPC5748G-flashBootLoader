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

/* Definition of memory regions using absolute addresses of physical memories. The symbols
   with segment start addresses and sizes have already been defined in another linker
   script and depending on the build target, RAM or flash ROM. */
MEMORY
{
    /* RM, sec. 3.2: f8c000 is the first inspected boot sector. However, this area
       is marked "Small HSM Code Block". According to RM, 3.3, table 3-2, F90000 is the
       beginning of the generally usable flash ROM and it's at the same time one of
       boot header locations.
         The FBL should preferably use the boot header location FC0000. It
       has the highest priority so that a loaded application won't take
       precedence over the FBL. Unfortunately, this location is the beginning of a 32k
       flash block, which belongs into the same partition as two other ones, and one of the
       others is not even adjacent. Which effectively means that the FBL cannot use the 32k
       for code or data. If we block the complete partition 0 from being flashable by the
       FBL then it could place code and data into these blocks but no the adjacent
       flashable ROM area would loose 192kB (partition 0 plus 2*32kB of encapsulated blocks
       from partition 1). */
    memBootUnused(rx): org = ld_textSegStartAddr, len = 0x1C
    memBoot(rx):       org = 0x00FC0000, len = 0x1C
    memFlash(rx):      org = ld_textSegStartAddr + LENGTH(memBootUnused),
                             len = ld_textSegSize - LENGTH(memBootUnused)
    memData(rwx):      org = ld_dataSegStartAddr, len = ld_dataSegSize
}
