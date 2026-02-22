/* This file defines a memory partitioning, which places the executable code (text sections)
   into RAM. Instead of a flashable binary, we get an equivalent binary that can be loaded
   into RAM by the debugger. Using RAM can have these advantages:
   - We save flash ROM programming cycles, which are limited
   - It reduces the turnaround time between code changes. Loading RAM is faster than
     erasing and re-flashing
   - The risk of bricking the CPU is lower if we work on code, which is critical in this
     respect; e.g. self-tests, security, reset logic, reset escalation
   The major drawback is the software size; code and data need to fit into RAM. We use a
   hard-coded distribution of both, 75% for code, 25% for data.
     Some makefile conditional code decides whether this file or the "normal" memory map
   definition memoryMapROM.ld is read by the linker. */
ASSERT(ld_linkInRAM == 1, "This memory map is intended for build in RAM only")
ld_textSegStartAddr = 0x40000000 ;
ld_textSegSize      = 3*64k ;
ld_dataSegStartAddr = ld_textSegStartAddr + ld_textSegSize ;
ld_dataSegSize      = 3*256k - ld_textSegSize ;

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
    memBoot(rx):  org = ld_textSegStartAddr, len = 0x1C
    memFlash(rx): org = ld_textSegStartAddr + LENGTH(memBoot),
                        len = ld_textSegSize - LENGTH(memBoot)
    memData(rwx): org = ld_dataSegStartAddr, len = ld_dataSegSize
}
