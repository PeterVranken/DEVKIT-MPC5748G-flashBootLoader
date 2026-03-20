/**
 * @file eap_eraseAndProgram.c
 * Erase and program operations for the C55FMC.
 *
 * Copyright (C) 2026 Peter Vranken (mailto:Peter_Vranken@Yahoo.de)
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
/* Module interface
 *   eap_osInitFlashRomDriver
 *   eap_osStartEraseFlashBlocks
 *   eap_osGetStatusEraseFlashBlocks
 *   eap_osStartProgramQuadPage
 *   eap_osGetStatusProgramQuadPage
 * Local functions
 *   disableAllFlashBlocks
 *   enableFlashBlocks
 *   invalidateDCache
 *   isQuadPageBlank
 *   verifyQuadPage
 *   abortEraseAndProgram
 */

/*
 * Include files
 */

#include "eap_eraseAndProgram.h"

#if EAP_TEST_WITH_MOCKUP != 1

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "rom_flashRomDriver.h"
#include "rtos_ivorHandler.h"

/* Include the appropriate MCU header. */
#if defined(MCU_MPC5748G)
# include "MPC5748G.h"
#elif defined(MCU_MPC5775B)
# include "MPC5775B.h"
#elif defined(MCU_MPC5775E)
# include "MPC5775E.h"
#elif defined(MCU_MPC5777C)
# include "MPC5777C.h"
#else
# error Unsupported MCU configured
#endif

/*
 * Defines
 */


/*
 * Local type definitions
 */

/** The description of a flash block. */
typedef struct
{
    /** Address of first byte of flash block. */
    uint32_t addrFrom;

    /** Address of first byte (exclusive) of flash block. */
    uint32_t addrTo;

    /** The index of the RWW partition. */
    uint8_t idxPartition;

    /** The index of the lock or select register, which controls the block. */
    uint8_t idxLockReg;

    /** A bit mask with a single bit set. The bit masks the control bit for the given flash
        block in the related lock or select register \a idxLockReg. */
    uint32_t bitMaskLockReg;

} flashBlockDesc_t;

/*
 * Local prototypes
 */


/*
 * Data definitions
 */

static const flashBlockDesc_t RODATA(flashBlockDescAry)[] =
{
#if defined(MCU_MPC5748G)
//    {.addrFrom = 0x00FC0000u, .addrTo = 0x00FC8000u, .idxPartition = 0u, .idxLockReg = 0u, .bitMaskLockReg = 0x00040000u,},  /* 32KB Code Flash block */
    {.addrFrom = 0x00FC8000u, .addrTo = 0x00FD0000u, .idxPartition = 0u, .idxLockReg = 0u, .bitMaskLockReg = 0x00080000u,},  /* 32KB Code Flash block */
    {.addrFrom = 0x00FD0000u, .addrTo = 0x00FD8000u, .idxPartition = 1u, .idxLockReg = 0u, .bitMaskLockReg = 0x00100000u,},  /* 32KB Code Flash block */
    {.addrFrom = 0x00FD8000u, .addrTo = 0x00FE0000u, .idxPartition = 1u, .idxLockReg = 0u, .bitMaskLockReg = 0x00200000u,},  /* 32KB Code Flash block */
    {.addrFrom = 0x00FE0000u, .addrTo = 0x00FF0000u, .idxPartition = 0u, .idxLockReg = 0u, .bitMaskLockReg = 0x00400000u,},  /* 64KB Code Flash block */
    {.addrFrom = 0x00FF0000u, .addrTo = 0x01000000u, .idxPartition = 1u, .idxLockReg = 0u, .bitMaskLockReg = 0x01000000u,},  /* 64KB Code Flash block */
    {.addrFrom = 0x01000000u, .addrTo = 0x01040000u, .idxPartition = 6u, .idxLockReg = 2u, .bitMaskLockReg = 0x00000001u,},  /* 256KB Code Flash block */
    {.addrFrom = 0x01040000u, .addrTo = 0x01080000u, .idxPartition = 6u, .idxLockReg = 2u, .bitMaskLockReg = 0x00000002u,},  /* 256KB Code Flash block */
    {.addrFrom = 0x01080000u, .addrTo = 0x010C0000u, .idxPartition = 6u, .idxLockReg = 2u, .bitMaskLockReg = 0x00000004u,},  /* 256KB Code Flash block */
    {.addrFrom = 0x010C0000u, .addrTo = 0x01100000u, .idxPartition = 6u, .idxLockReg = 2u, .bitMaskLockReg = 0x00000008u,},  /* 256KB Code Flash block */
    {.addrFrom = 0x01100000u, .addrTo = 0x01140000u, .idxPartition = 6u, .idxLockReg = 2u, .bitMaskLockReg = 0x00000010u,},  /* 256KB Code Flash block */
    {.addrFrom = 0x01140000u, .addrTo = 0x01180000u, .idxPartition = 6u, .idxLockReg = 2u, .bitMaskLockReg = 0x00000020u,},  /* 256KB Code Flash block */
    {.addrFrom = 0x01180000u, .addrTo = 0x011C0000u, .idxPartition = 6u, .idxLockReg = 2u, .bitMaskLockReg = 0x00000040u,},  /* 256KB Code Flash block */
    {.addrFrom = 0x011C0000u, .addrTo = 0x01200000u, .idxPartition = 6u, .idxLockReg = 2u, .bitMaskLockReg = 0x00000080u,},  /* 256KB Code Flash block */
    {.addrFrom = 0x01200000u, .addrTo = 0x01240000u, .idxPartition = 7u, .idxLockReg = 2u, .bitMaskLockReg = 0x00000100u,},  /* 256KB Code Flash block */
    {.addrFrom = 0x01240000u, .addrTo = 0x01280000u, .idxPartition = 7u, .idxLockReg = 2u, .bitMaskLockReg = 0x00000200u,},  /* 256KB Code Flash block */
    {.addrFrom = 0x01280000u, .addrTo = 0x012C0000u, .idxPartition = 7u, .idxLockReg = 2u, .bitMaskLockReg = 0x00000400u,},  /* 256KB Code Flash block */
    {.addrFrom = 0x012C0000u, .addrTo = 0x01300000u, .idxPartition = 7u, .idxLockReg = 2u, .bitMaskLockReg = 0x00000800u,},  /* 256KB Code Flash block */
    {.addrFrom = 0x01300000u, .addrTo = 0x01340000u, .idxPartition = 7u, .idxLockReg = 2u, .bitMaskLockReg = 0x00001000u,},  /* 256KB Code Flash block */
    {.addrFrom = 0x01340000u, .addrTo = 0x01380000u, .idxPartition = 7u, .idxLockReg = 2u, .bitMaskLockReg = 0x00002000u,},  /* 256KB Code Flash block */
    {.addrFrom = 0x01380000u, .addrTo = 0x013C0000u, .idxPartition = 7u, .idxLockReg = 2u, .bitMaskLockReg = 0x00004000u,},  /* 256KB Code Flash block */
    {.addrFrom = 0x013C0000u, .addrTo = 0x01400000u, .idxPartition = 7u, .idxLockReg = 2u, .bitMaskLockReg = 0x00008000u,},  /* 256KB Code Flash block */
    {.addrFrom = 0x01400000u, .addrTo = 0x01440000u, .idxPartition = 8u, .idxLockReg = 2u, .bitMaskLockReg = 0x00010000u,},  /* 256KB Code Flash block */
    {.addrFrom = 0x01440000u, .addrTo = 0x01480000u, .idxPartition = 8u, .idxLockReg = 2u, .bitMaskLockReg = 0x00020000u,},  /* 256KB Code Flash block */
    {.addrFrom = 0x01480000u, .addrTo = 0x014C0000u, .idxPartition = 8u, .idxLockReg = 2u, .bitMaskLockReg = 0x00040000u,},  /* 256KB Code Flash block */
    {.addrFrom = 0x014C0000u, .addrTo = 0x01500000u, .idxPartition = 9u, .idxLockReg = 2u, .bitMaskLockReg = 0x00080000u,},  /* 256KB Code Flash block */
    {.addrFrom = 0x01500000u, .addrTo = 0x01540000u, .idxPartition = 9u, .idxLockReg = 2u, .bitMaskLockReg = 0x00100000u,},  /* 256KB Code Flash block */
    {.addrFrom = 0x01540000u, .addrTo = 0x01580000u, .idxPartition = 9u, .idxLockReg = 2u, .bitMaskLockReg = 0x00200000u,},  /* 256KB Code Flash block */

#elif defined(MCU_MPC5775B) || defined(MCU_MPC5775E) || defined(MCU_MPC5777C)
    /* Caution! In the RM75, 4.1, Table 4-2, p.98, the RWW partitions of the 16 256kB
       blocks are shown as 6 und 7. At RM75, 26.1.3.3, pp.867f, they are shown as 9 and 8.
       Even worse, the bits of the register are not clearly related to particular flash
       blocks. The only indication given is the name of the first 256kB block, "Boot code".
       This seems to indicate that the MSBit belongs to the flash block with lowest
       address, probably followed by blocks with higher address at less significant bits.
         NXP sample code: It expects the 16 blocks in the lower half word of the register!
       This is the pattern, we see also for the MPC5748G and the MPC5777C, which is anyway
       known as widely with the MPC5775B/E. Therefore, we assume Table 4-2 as wrong. */
    {.addrFrom = 0x00800000u, .addrTo = 0x00840000u, .idxPartition = 6u, .idxLockReg = 2u, .bitMaskLockReg = 0x00000001u,}, /* 256kB Large Boot             */
    {.addrFrom = 0x00840000u, .addrTo = 0x00880000u, .idxPartition = 6u, .idxLockReg = 2u, .bitMaskLockReg = 0x00000002u,}, /* 256kB Large Application code */
    {.addrFrom = 0x00880000u, .addrTo = 0x008C0000u, .idxPartition = 6u, .idxLockReg = 2u, .bitMaskLockReg = 0x00000004u,}, /* 256kB Large Application code */
    {.addrFrom = 0x008C0000u, .addrTo = 0x00900000u, .idxPartition = 6u, .idxLockReg = 2u, .bitMaskLockReg = 0x00000008u,}, /* 256kB Large Application code */
    {.addrFrom = 0x00900000u, .addrTo = 0x00940000u, .idxPartition = 6u, .idxLockReg = 2u, .bitMaskLockReg = 0x00000010u,}, /* 256kB Large Application code */
    {.addrFrom = 0x00940000u, .addrTo = 0x00980000u, .idxPartition = 6u, .idxLockReg = 2u, .bitMaskLockReg = 0x00000020u,}, /* 256kB Large Application code */
    {.addrFrom = 0x00980000u, .addrTo = 0x009C0000u, .idxPartition = 6u, .idxLockReg = 2u, .bitMaskLockReg = 0x00000040u,}, /* 256kB Large Application code */
    {.addrFrom = 0x009C0000u, .addrTo = 0x00A00000u, .idxPartition = 6u, .idxLockReg = 2u, .bitMaskLockReg = 0x00000080u,}, /* 256kB Large Application code */
    {.addrFrom = 0x00A00000u, .addrTo = 0x00A40000u, .idxPartition = 7u, .idxLockReg = 2u, .bitMaskLockReg = 0x00000100u,}, /* 256kB Large Application code */
    {.addrFrom = 0x00A40000u, .addrTo = 0x00A80000u, .idxPartition = 7u, .idxLockReg = 2u, .bitMaskLockReg = 0x00000200u,}, /* 256kB Large Application code */
    {.addrFrom = 0x00A80000u, .addrTo = 0x00AC0000u, .idxPartition = 7u, .idxLockReg = 2u, .bitMaskLockReg = 0x00000400u,}, /* 256kB Large Application code */
    {.addrFrom = 0x00AC0000u, .addrTo = 0x00B00000u, .idxPartition = 7u, .idxLockReg = 2u, .bitMaskLockReg = 0x00000800u,}, /* 256kB Large Application code */
    {.addrFrom = 0x00B00000u, .addrTo = 0x00B40000u, .idxPartition = 7u, .idxLockReg = 2u, .bitMaskLockReg = 0x00001000u,}, /* 256kB Large Application code */
    {.addrFrom = 0x00B40000u, .addrTo = 0x00B80000u, .idxPartition = 7u, .idxLockReg = 2u, .bitMaskLockReg = 0x00002000u,}, /* 256kB Large Application code */
    {.addrFrom = 0x00B80000u, .addrTo = 0x00BC0000u, .idxPartition = 7u, .idxLockReg = 2u, .bitMaskLockReg = 0x00004000u,}, /* 256kB Large Application code */
    {.addrFrom = 0x00BC0000u, .addrTo = 0x00C00000u, .idxPartition = 7u, .idxLockReg = 2u, .bitMaskLockReg = 0x00008000u,}, /* 256kB Large Application code */
# if defined(MCU_MPC5777C)
    /* The MPC7577C has the same memory architecture as the MPC5775B/E, only the further 16
       large blocks are populated. */
    {.addrFrom = 0x00C00000u, .addrTo = 0x00C40000u, .idxPartition = 8u, .idxLockReg = 2u, .bitMaskLockReg = 0x00010000u,}, /* 256kB Large Boot             */
    {.addrFrom = 0x00C40000u, .addrTo = 0x00C80000u, .idxPartition = 8u, .idxLockReg = 2u, .bitMaskLockReg = 0x00020000u,}, /* 256kB Large Application code */
    {.addrFrom = 0x00C80000u, .addrTo = 0x00CC0000u, .idxPartition = 8u, .idxLockReg = 2u, .bitMaskLockReg = 0x00040000u,}, /* 256kB Large Application code */
    {.addrFrom = 0x00CC0000u, .addrTo = 0x00D00000u, .idxPartition = 8u, .idxLockReg = 2u, .bitMaskLockReg = 0x00080000u,}, /* 256kB Large Application code */
    {.addrFrom = 0x00D00000u, .addrTo = 0x00D40000u, .idxPartition = 8u, .idxLockReg = 2u, .bitMaskLockReg = 0x00100000u,}, /* 256kB Large Application code */
    {.addrFrom = 0x00D40000u, .addrTo = 0x00D80000u, .idxPartition = 8u, .idxLockReg = 2u, .bitMaskLockReg = 0x00200000u,}, /* 256kB Large Application code */
    {.addrFrom = 0x00D80000u, .addrTo = 0x00DC0000u, .idxPartition = 8u, .idxLockReg = 2u, .bitMaskLockReg = 0x00400000u,}, /* 256kB Large Application code */
    {.addrFrom = 0x00DC0000u, .addrTo = 0x00E00000u, .idxPartition = 8u, .idxLockReg = 2u, .bitMaskLockReg = 0x00800000u,}, /* 256kB Large Application code */
    {.addrFrom = 0x00E00000u, .addrTo = 0x00E40000u, .idxPartition = 9u, .idxLockReg = 2u, .bitMaskLockReg = 0x01000000u,}, /* 256kB Large Application code */
    {.addrFrom = 0x00E40000u, .addrTo = 0x00E80000u, .idxPartition = 9u, .idxLockReg = 2u, .bitMaskLockReg = 0x02000000u,}, /* 256kB Large Application code */
    {.addrFrom = 0x00E80000u, .addrTo = 0x00EC0000u, .idxPartition = 9u, .idxLockReg = 2u, .bitMaskLockReg = 0x04000000u,}, /* 256kB Large Application code */
    {.addrFrom = 0x00EC0000u, .addrTo = 0x00F00000u, .idxPartition = 9u, .idxLockReg = 2u, .bitMaskLockReg = 0x08000000u,}, /* 256kB Large Application code */
    {.addrFrom = 0x00F00000u, .addrTo = 0x00F40000u, .idxPartition = 9u, .idxLockReg = 2u, .bitMaskLockReg = 0x10000000u,}, /* 256kB Large Application code */
    {.addrFrom = 0x00F40000u, .addrTo = 0x00F80000u, .idxPartition = 9u, .idxLockReg = 2u, .bitMaskLockReg = 0x20000000u,}, /* 256kB Large Application code */
    {.addrFrom = 0x00F80000u, .addrTo = 0x00FC0000u, .idxPartition = 9u, .idxLockReg = 2u, .bitMaskLockReg = 0x40000000u,}, /* 256kB Large Application code */
    {.addrFrom = 0x00FC0000u, .addrTo = 0x01000000u, .idxPartition = 9u, .idxLockReg = 2u, .bitMaskLockReg = 0x80000000u,}, /* 256kB Large Application code */
# endif
#else
# error Specify flash block configuration for still unknown MCU
#endif
};

/** Temporary storage of the quad-page buffer, which is currently being programmed. The
    buffer is stored at the start of progamming for verification when program mode is left. */
static const eap_quadPageProgramBuffer_t *BSS_OS(_pPrgDataBuf) = NULL;

/*
 * Function implementation
 */

/**
 * Helper: All flash blocks are (initially) protected against erasure and programming;
 * over-programming is disabled, too.
 */
static void disableAllFlashBlocks(void)
{
#if defined(MCU_MPC5748G) || defined(MCU_MPC5775B) || defined(MCU_MPC5775E)
# define LOCK_ALL_BLKS_0    0xBFFFFFFFu
# define LOCK_ALL_BLKS_1    0xFFFFFFFFu
# define LOCK_ALL_BLKS_2    0xFFFFFFFFu
# define LOCK_ALL_BLKS_3    0xFFFFFFFFu
# define SELECT_NO_BLK_0    0x00000000u
# define SELECT_NO_BLK_1    0x00000000u
# define SELECT_NO_BLK_2    0x00000000u
# define SELECT_NO_BLK_3    0x00000000u
#elif defined(MCU_MPC5777C)
# error Specify flash block configuration for MPC5777C
#endif

    /* Lock all blocks against erasure and programming. RM 74.1.1.1ff, pp.3639ff. */
    C55FMC->LOCK0 = LOCK_ALL_BLKS_0;
    C55FMC->LOCK1 = LOCK_ALL_BLKS_1;
    C55FMC->LOCK2 = LOCK_ALL_BLKS_2;
    C55FMC->LOCK3 = LOCK_ALL_BLKS_3;

    /* Unselect all blocks from erasure. Dito. */
    C55FMC->SEL0 = SELECT_NO_BLK_0;
    C55FMC->SEL1 = SELECT_NO_BLK_1;
    C55FMC->SEL2 = SELECT_NO_BLK_2;
    C55FMC->SEL3 = SELECT_NO_BLK_3;

} /* disableAllFlashBlocks */


/**
 * Enable those flash blocks, which are required for an intended erase or program activity.
 *   @param[in] addressFrom
 * The required blocks are specified by the address range, which is affected by the
 * intended activity. All flash blocks, which share at least one byte with the specified
 * address range, will be enabled, all others don't. This is the first address of the
 * range.\n
 *   Note, addressFrom and \a noBytes need to from a valid, supported address range in the
 * flashable area. This can be checked beforehand using rom_isValidFlashAddressRange().
 *   @param[in] noBytes
 * This is the length in Byte of the address range.\n
 *   Note, if the intended activity is programming, then this value needs to be
 * #EAP_C55FMC_SIZE_OF_QUAD_PAGE, the size of a single programmed (quad-)page.
 *   @param[in] isErase
 * A Boolean flag selects the intended activity. Pass \a true if the blocks are enabled for
 * erasure and \a false if they are enabled for programming.
 */
static void enableFlashBlocks(uint32_t addressFrom, uint32_t noBytes, bool isErase)
{
    const uint32_t addressTo = addressFrom + noBytes;

    /* We address to the lock and select registers as an array. */
    _Static_assert
        ( (uintptr_t)&C55FMC->LOCK1 == (uintptr_t)&C55FMC->LOCK0 + 1*sizeof(C55FMC->LOCK0)
          &&  (uintptr_t)&C55FMC->LOCK2 == (uintptr_t)&C55FMC->LOCK0 + 2*sizeof(C55FMC->LOCK0)
          &&  (uintptr_t)&C55FMC->LOCK3 == (uintptr_t)&C55FMC->LOCK0 + 3*sizeof(C55FMC->LOCK0)
          &&  (uintptr_t)&C55FMC->SEL1 == (uintptr_t)&C55FMC->SEL0 + 1*sizeof(C55FMC->SEL0)
          &&  (uintptr_t)&C55FMC->SEL2 == (uintptr_t)&C55FMC->SEL0 + 2*sizeof(C55FMC->SEL0)
          &&  (uintptr_t)&C55FMC->SEL3 == (uintptr_t)&C55FMC->SEL0 + 3*sizeof(C55FMC->SEL0)
        , "Addressing of registers as an array fails"
        );

    /* We always program single quad-pages. */
    assert(isErase ||  noBytes == EAP_C55FMC_SIZE_OF_QUAD_PAGE);

    /* If we see an overflow then we definitely have an invalid address space. (ROM at the
       very end of the address space is not supported by the implementation.) A valid adress
       ranges is a prerequiste of calling this function. */
    assert(addressTo >= addressFrom);

    /* We check for each flash block if it is touched and needs to be enabled. We don't do
       the opposite; we don't check if all enabled flash blocks really cover the complete
       desired address range. If the address range is valid, should be checked beforehand.
         The search loop is optimize for the frequent calls of this function in programming
       mode: We inspect the last recently enabled block first. */
    _Static_assert(sizeOfAry(flashBlockDescAry) <= 256u, "Overflow of uint8_t idxBlk_");
    static uint8_t SBSS_OS(idxBlk_) = 0u;
    for(unsigned int u=0u; u<sizeOfAry(flashBlockDescAry); ++u, ++idxBlk_)
    {
        if(idxBlk_ >= sizeOfAry(flashBlockDescAry))
            idxBlk_ = 0u;

        const flashBlockDesc_t * const pBlkDesc = &flashBlockDescAry[idxBlk_];
        if(pBlkDesc->addrFrom < addressTo  &&  pBlkDesc->addrTo > addressFrom)
        {
            /* This flash block needs to be unlocked. */
            volatile uint32_t * const lockRegAry = &C55FMC->LOCK0;
            assert((lockRegAry[pBlkDesc->idxLockReg] & pBlkDesc->bitMaskLockReg) != 0u);
            lockRegAry[pBlkDesc->idxLockReg] &= ~pBlkDesc->bitMaskLockReg;

            /* Erased blocks need to be explicitly selected. */
            if(isErase)
            {
                volatile uint32_t * const selectRegAry = &C55FMC->SEL0;
                assert((selectRegAry[pBlkDesc->idxLockReg] & pBlkDesc->bitMaskLockReg) == 0u);
                selectRegAry[pBlkDesc->idxLockReg] |= pBlkDesc->bitMaskLockReg;
                #if 1
                iprintf( "Select flash block %06lX..%06lX for erasure.\r\n"
                       , pBlkDesc->addrFrom
                       , pBlkDesc->addrTo
                       );
                #endif
            }
            else
            {
                /* We always program single quad-pages, so there can't be more than one
                   touched flash block. */
                #if 0
                iprintf( "Unlock flash block %06lX..%06lX for programming.\r\n"
                       , pBlkDesc->addrFrom
                       , pBlkDesc->addrTo
                       );
                #endif
                break;
            }
        }
    } /* for(All configured flash blocks.) */

} /* enableFlashBlocks */


/**
 * Invalidate the D-cache for all addresses, which belong to a given quad-page.\n
 *   D-cache invalidation is required, whenever the contents of the background memory
 * change for reasons not being loads and stores by the CPU, e.g., erasure or programming
 * of the flash array.
 *   @param[in] addrOfQuadPage
 * The address of the quad-page.
 */
static inline void invalidateDCache(uint32_t addrOfQuadPage)
{
    /* Address doesn't need to be forced to have the alignment of a cache line; quad-page
       have the even higher alignment constraint. We jsut check by assertion. */
    assert(EAP_GET_ADDR_OFFS_IN_QUAD_PAGE(addrOfQuadPage) == 0u);

#if defined(MCU_MPC5748G)
    /* The e200z4204n3 core's line size, see RM48 62.7.1, p.3111. */
    const uint32_t sizeofCacheLine = 32u;
#elif defined(MCU_MPC5775B) || defined(MCU_MPC5775E)
    /* The e200z759n3 core's line size, see RMZ7 11.2, pp.586f. */
    const uint32_t sizeofCacheLine = 32u;
#elif defined(MCU_MPC5777C)
# error Specify cache line size for MPC5777C
#endif

    uint32_t msr = rtos_osEnterCriticalSection();

    /* Flush data fetch and store operations. */
    __asm__ volatile ("msync");

    /* A quad-page of 128 Byte means four possible tag hits in the cache. */
    __asm__ volatile ("dcbi 0,%0" :: "r"(addrOfQuadPage + 0u*sizeofCacheLine));
    __asm__ volatile ("dcbi 0,%0" :: "r"(addrOfQuadPage + 1u*sizeofCacheLine));
    __asm__ volatile ("dcbi 0,%0" :: "r"(addrOfQuadPage + 2u*sizeofCacheLine));
    __asm__ volatile ("dcbi 0,%0" :: "r"(addrOfQuadPage + 3u*sizeofCacheLine));

    /* Force re-fill of all pipelines after cache manipulation. */
    __asm__ volatile ("msync");
    __asm__ volatile ("se_isync");

    rtos_osLeaveCriticalSection(msr);

} /* invalidateDCache */


/**
 * Check if a quad-page is blank.
 */
static inline bool isQuadPageBlank(uint32_t addrOfQuadPage)
{
    assert(EAP_GET_ADDR_OFFS_IN_QUAD_PAGE(addrOfQuadPage) == 0u);
    _Static_assert( EAP_C55FMC_SIZE_OF_QUAD_PAGE / sizeof(uint64_t) == 16u
                    &&  EAP_C55FMC_SIZE_OF_QUAD_PAGE % sizeof(uint64_t) == 0u
                  , "Bad implementation of blank check"
                  );
    const volatile uint64_t *pRd = (const volatile uint64_t*)addrOfQuadPage;
#if 0
    const unsigned int noDWords = EAP_C55FMC_SIZE_OF_QUAD_PAGE/sizeof(uint64_t);
    for(unsigned int idxDWd=0u; idxDWd<noDWords; ++idxDWd, ++pRd)
        if(*pRd != 0xFFFFFFFFFFFFFFFFull)
            return false;
#else
    if(*pRd++ != 0xFFFFFFFFFFFFFFFFull) return false;
    if(*pRd++ != 0xFFFFFFFFFFFFFFFFull) return false;
    if(*pRd++ != 0xFFFFFFFFFFFFFFFFull) return false;
    if(*pRd++ != 0xFFFFFFFFFFFFFFFFull) return false;
    if(*pRd++ != 0xFFFFFFFFFFFFFFFFull) return false;
    if(*pRd++ != 0xFFFFFFFFFFFFFFFFull) return false;
    if(*pRd++ != 0xFFFFFFFFFFFFFFFFull) return false;
    if(*pRd++ != 0xFFFFFFFFFFFFFFFFull) return false;
    if(*pRd++ != 0xFFFFFFFFFFFFFFFFull) return false;
    if(*pRd++ != 0xFFFFFFFFFFFFFFFFull) return false;
    if(*pRd++ != 0xFFFFFFFFFFFFFFFFull) return false;
    if(*pRd++ != 0xFFFFFFFFFFFFFFFFull) return false;
    if(*pRd++ != 0xFFFFFFFFFFFFFFFFull) return false;
    if(*pRd++ != 0xFFFFFFFFFFFFFFFFull) return false;
    if(*pRd++ != 0xFFFFFFFFFFFFFFFFull) return false;
    if(*pRd++ != 0xFFFFFFFFFFFFFFFFull) return false;
#endif

    assert((uint32_t)pRd == addrOfQuadPage + EAP_C55FMC_SIZE_OF_QUAD_PAGE);
    return true;

} /* isQuadPageBlank */


/**
 * Check if the falsh ROM contents of a quad-page are identical to some expected data.
 *   @return
 * Get \a true if the flash ROM contains the data in \a pPrgDataBuf->data_b. Get \a
 * false in case of any deviation.
 *   @param[in] pPrgDataBuf
 * A data buffer by reference, which conatins the expected contents of a quad-page.
 */
static bool verifyQuadPage(const eap_quadPageProgramBuffer_t * const pPrgDataBuf)
{
    assert( pPrgDataBuf != NULL
            &&  EAP_GET_ADDR_OFFS_IN_QUAD_PAGE(pPrgDataBuf->address) == 0u
          );
    _Static_assert( EAP_C55FMC_SIZE_OF_QUAD_PAGE / sizeof(uint64_t) == 16u
                    &&  EAP_C55FMC_SIZE_OF_QUAD_PAGE % sizeof(uint64_t) == 0u
                  , "Bad implementation of verify"
                  );
    const volatile uint64_t *pFlash = (const volatile uint64_t*)pPrgDataBuf->address;
    const uint64_t *pExptd = &pPrgDataBuf->data_u64[0];

    if(*pFlash++ != *pExptd++) return false;
    if(*pFlash++ != *pExptd++) return false;
    if(*pFlash++ != *pExptd++) return false;
    if(*pFlash++ != *pExptd++) return false;
    if(*pFlash++ != *pExptd++) return false;
    if(*pFlash++ != *pExptd++) return false;
    if(*pFlash++ != *pExptd++) return false;
    if(*pFlash++ != *pExptd++) return false;
    if(*pFlash++ != *pExptd++) return false;
    if(*pFlash++ != *pExptd++) return false;
    if(*pFlash++ != *pExptd++) return false;
    if(*pFlash++ != *pExptd++) return false;
    if(*pFlash++ != *pExptd++) return false;
    if(*pFlash++ != *pExptd++) return false;
    if(*pFlash++ != *pExptd++) return false;
    if(*pFlash++ != *pExptd++) return false;

    assert((uint32_t)pFlash == (uint32_t)pPrgDataBuf->address + EAP_C55FMC_SIZE_OF_QUAD_PAGE);
    return true;

} /* verifyQuadPage */



/**
 * This function stops all erase and program activities of the C55FMC.\n
 *   It can be called in case of errors or unexpected states.
 */
static void abortEraseAndProgram(void)
{
    /* The mode of operation bits are disabled in distinct steps. This is a rule from the
       bit locking. RM48, 74.6, Table 74-3, p.3679. */
    // TODO Abortion is likely not properly implemented. Compare conditions RM48, 74.5.1, p.3656, for resetting EHV. However, seems to affect only modes, we don't use anyway
    C55FMC->MCR &= ~C55FMC_MCR_PSUS_MASK;
    C55FMC->MCR &= ~C55FMC_MCR_ESUS_MASK;
    C55FMC->MCR &= ~C55FMC_MCR_EHV_MASK;
    C55FMC->MCR &= ~C55FMC_MCR_PGM_MASK;
    C55FMC->MCR &= ~C55FMC_MCR_ERS_MASK;

    /* Now we can restore the default settings for accesibility of the flash blocks. */
    disableAllFlashBlocks();

} /* abortEraseAndProgram */


/**
 * Initialize the flash ROM driver.
 */
void eap_osInitFlashRomDriver(void)
{
    abortEraseAndProgram();

#ifdef DEBUG
    for(unsigned int idxBlk=0u; idxBlk<sizeOfAry(flashBlockDescAry); ++idxBlk)
    {
        const flashBlockDesc_t * const pBlkDesc = &flashBlockDescAry[idxBlk];

        /* The flash driver generally doesn't consider overflow in address calculations and
           it uses end addresses exclusively. This makes the code fail for architectures,
           which have a flash ROM block at the very end of the implementation range of
           address variables, which uses type uint32_t. The end address of such a block
           would become 0 - which could be correct but which is simply not supported. (If
           your architecture by accident has such a flash block then the easiest way out
           would be sacrificing the last page of that block.) */
        assert(pBlkDesc->addrTo > 0u);

        /* A flash block must not be empty. */
        assert(pBlkDesc->addrFrom < pBlkDesc->addrTo);

        /* A flash-block must begin and end on a quad-page boundary. */
        assert(EAP_GET_ADDR_OFFS_IN_QUAD_PAGE(pBlkDesc->addrFrom) == 0u
               &&  EAP_GET_ADDR_OFFS_IN_QUAD_PAGE(pBlkDesc->addrTo) == 0u
              );

        /* We have 4 lock and select registers. */
        assert(pBlkDesc->idxLockReg < 4u);

        /* Each flash block is represented by excately one bit in these registers. */
        #define HAS_ONE_BIT_SET(x)  ((x)!=0u && ((x)&((x)-1u))==0u)
        assert(HAS_ONE_BIT_SET(pBlkDesc->bitMaskLockReg));
        #undef HAS_ONE_BIT_SET

# if defined(MCU_MPC5748G) || defined(MCU_MPC5775B) || defined(MCU_MPC5775E)
        /* For the MPC5748G and MPC5775B/E, the implementation of the address range
           validity is based on the fact, that all flash blocks form a single, contiguous
           address space. Gaps are not considered. */
        assert(idxBlk == 0u  ||  pBlkDesc->addrFrom == flashBlockDescAry[idxBlk-1].addrTo);
# endif
    } /* for(Check flash block specification table entries for plausibility) */

# if defined(MCU_MPC5748G) || defined(MCU_MPC5775B) || defined(MCU_MPC5775E)
    /* For the MPC5748G and MPC5775B/E, the implementation of the address range validity
       uses hard-coded address boundaries. We need to check consistency. */
    assert(rom_isValidFlashAddressRange(flashBlockDescAry[0].addrFrom, /*size*/ 0u)
           && !rom_isValidFlashAddressRange(flashBlockDescAry[0].addrFrom-1u, /*size*/ 0u)
           && rom_isValidFlashAddressRange
                                ( flashBlockDescAry[sizeOfAry(flashBlockDescAry)-1].addrTo
                                , /*size*/ 0u
                                )
           && !rom_isValidFlashAddressRange
                                ( flashBlockDescAry[sizeOfAry(flashBlockDescAry)-1].addrTo+1u
                                , /*size*/ 0u
                                )
          );
# elif defined(MCU_MPC5777C)
#  error Implement flash block table check for MPC5777C
# endif
#endif /* DEBUG */
} /* eap_osInitFlashRomDriver */


/**
 * Start the erasure of one or more flash blocks in the C55 controller.
 *   The preconditions are checked (availability of controller for erasure) the wanted
 * blocks are selected and the high voltage is enabled, which makes the flash erasure
 * begin. The function is non-blocking and doesn't wait for the termination of the erasure.
 * Use eap_osGetStatusEraseFlashBlocks() to find out, when it has terminated.
 *   @return
 * Get the status of the operation. eap_osStartEraseFlashBlocks() or
 * eap_osStartProgramQuadPage() can be called earliest again when this function returns
 * either success or failure - but not yet while it reports a pending state
 * (#rom_err_processPending).
 *   @param[in] addressFrom
 * The blocks to erase are specified by the address range, which needs to become blank. All
 * flash blocks, which share at least one byte with the specified address range, will be
 * erased, all others don't. This is the first blanked address.
 *   @param[in] noBytes
 * This is the length in Byte of the address range.
 */
rom_errorCode_t eap_osStartEraseFlashBlocks(uint32_t addressFrom, uint32_t noBytes)
{
    rom_errorCode_t retCode;
    const uint32_t mcr = C55FMC->MCR;

    if(!rom_isValidFlashAddressRange(addressFrom, noBytes))
    {
        retCode = rom_err_badAddressRange;
    }

    /* To initiate erasure, no other operation must be in progress. */
    #define BITS_TO_BE_CLEARED  (C55FMC_MCR_ERS_MASK     \
                                 |C55FMC_MCR_PGM_MASK    \
                                 |C55FMC_MCR_EHV_MASK    \
                                 |C55FMC_MCR_PSUS_MASK   \
                                 |C55FMC_MCR_ESUS_MASK   \
                                )
    else if((mcr & BITS_TO_BE_CLEARED) == 0u)
    #undef BITS_TO_BE_CLEARED
    {
        /* Enable the flash blocks for erase, which are touched by the address range. */
        /* Set MCR[ERS] to start erase operation. */
        C55FMC->MCR |= C55FMC_MCR_ERS_MASK;

        enableFlashBlocks(addressFrom, noBytes, /*isErase*/ true);

        /* The interlock write needs to be done prior to enabling the high voltage. We
           need to write anywhere into a flash block to be erased. For simplicity, we
           choose the first address of the range. RM48, 74.6.1.3, p.3686, 3. */
        *(volatile uint32_t*)addressFrom = 0xFFFFFFFFu;

        /* We set MCR[EHV] to turn on the high voltage for erasing. See RM48, 74.5.1,
           p.3656. */
        C55FMC->MCR |= C55FMC_MCR_EHV_MASK;

        /* This is a non-blocking function. We don't wait for the result but will check
           it in the next clock tick. */
        retCode = rom_err_processPending;
    }
    else
    {
        /* Turn off high voltage, reset all operation request bits and lock flash blocks. */
        abortEraseAndProgram();

        retCode = rom_err_unexpectedHwState;
    }

    return retCode;

} /* eap_osStartEraseFlashBlocks */


/**
 * Check the status of an erase operation, which had been initiated before by
 * eap_osStartEraseFlashBlocks().
 *   @return
 * Get the status of the operation. eap_osStartEraseFlashBlocks() or
 * eap_osStartProgramQuadPage() can be called earliest again when this function returns
 * either success or failure - but not yet while it reports a pending state
 * (#rom_err_processPending).
 */
rom_errorCode_t eap_osGetStatusEraseFlashBlocks(void)
{
    rom_errorCode_t retCode;

    /* Did we initiate an erase operation before? Without this bit set, MCR[DONE] would be
       meaningless. */
    const uint32_t mcr = C55FMC->MCR;
    #define BITS_TO_BE_SET      (C55FMC_MCR_ERS_MASK|C55FMC_MCR_EHV_MASK)
    #define BITS_TO_BE_CLEARED  (C55FMC_MCR_PGM_MASK|C55FMC_MCR_PSUS_MASK|C55FMC_MCR_ESUS_MASK)
    if((mcr & BITS_TO_BE_SET) == BITS_TO_BE_SET  &&  (mcr & BITS_TO_BE_CLEARED) == 0u)
    #undef BITS_TO_BE_SET
    #undef BITS_TO_BE_CLEARED
    {
        /* MCR[DONE] indicates completion. */
        if((mcr & C55FMC_MCR_DONE_MASK) != 0u)
        {
            /* Check MCR[PEG]. A zero bit indicates an erase error. */
            if((mcr & C55FMC_MCR_PEG_MASK) != 0u)
                retCode = rom_err_noError;
            else
                retCode = rom_err_c55FmcErrorInPeg;
        }
        else
        {
            /* MCR[DONE]=0: The operation is still in progress. We wait till next clock
               tick for the next check. */
            retCode = rom_err_processPending;
        }
    }
    else
        retCode = rom_err_unexpectedHwState;

    if(retCode != rom_err_processPending)
    {
        /* Turn off high voltage, reset all operation request bits and lock flash blocks. */
        abortEraseAndProgram();

        /* Regardless whether erasure succeeded or failed, flash ROM array contents may
           have altered and we have the (very high) risk of a D-cache inconsistency. We
           invalidate the entire cache. Note, the I-cache is not affected as we don't read
           any instruction from the erased flash blocks, whereas we will read the data for
           a blank check. */
        rtos_osInitializeDCache();
    }

    return retCode;

} /* eap_osGetStatusEraseFlashBlocks */


/**
 * Start the programming of a single quad-page in the C55 controller.\n
 *   The preconditions are checked (availability of controller for new write-page
 * operation), the write buffer is filled with the data and high voltage is enabled, which
 * makes the flash programming begin. The function is non-blocking and doesn't wait for the
 * termination of the program step. Use eap_osGetStatusProgramQuadPage() to find out, when
 * it has terminated.
 *   @return
 * Get the status: If everything succeeded, then it is pending (#rom_err_processPending),
 * otherwise an error message.
 *   @param[in] pPrgDataBuf
 * The buffer with the data to program and the target address in flash ROM.\n
 *   Caution: The buffer needs to be unmodified and available to the flash ROM driver until
 * programming of the quad-page has completed - either until
 * eap_osGetStatusProgramQuadPage() has reported the end of the operation (no matter
 * whether successful) or until the programming mode has been aborted using
 * abortEraseAndProgram().
 */
rom_errorCode_t eap_osStartProgramQuadPage(eap_quadPageProgramBuffer_t * const pPrgDataBuf)
{
    rom_errorCode_t retCode;
    const uint32_t mcr = C55FMC->MCR;

    if( EAP_GET_ADDR_OFFS_IN_QUAD_PAGE(pPrgDataBuf->address) != 0u
        || !rom_isValidFlashAddressRange(pPrgDataBuf->address, EAP_C55FMC_SIZE_OF_QUAD_PAGE)
      )
    {
        retCode = rom_err_badAddressRange;
    }

    /* To initiate programming, no other operation must be in progress. */
    #define BITS_TO_BE_CLEARED  (C55FMC_MCR_ERS_MASK     \
                                 |C55FMC_MCR_PGM_MASK    \
                                 |C55FMC_MCR_EHV_MASK    \
                                 |C55FMC_MCR_PSUS_MASK   \
                                 |C55FMC_MCR_ESUS_MASK   \
                                )
    else if((mcr & BITS_TO_BE_CLEARED) == 0u)
    #undef BITS_TO_BE_CLEARED
    {
//        /* Erasure of the flash array has not been done in sync with the D-cache contents.
//           Reading of the flash will not surely return the values, which are now physically
//           in the flash array. Before we can do a reliable blanks test, we need to
//           invalidate all data, which is cached for the addreses of the quad-page. */
//        invalidateDCache(pPrgDataBuf->address);

        if(!isQuadPageBlank(pPrgDataBuf->address))
            retCode = rom_err_quadPageNotBlank;
        else
        {
            /* Enable the flash block for programming, which the wanted quad-page sits in. */
            enableFlashBlocks( pPrgDataBuf->address
                             , EAP_C55FMC_SIZE_OF_QUAD_PAGE
                             , /*isErase*/ false
                             );

            /* Set MCR[PGM] to start program operation. */
            C55FMC->MCR |= C55FMC_MCR_PGM_MASK;

            /* We always write an entire quad-page at once. */

            /* Copy quad-page contents into the controller's data buffer. This is at the
               same time the required interlock write. */
            const uint32_t *pRd = &pPrgDataBuf->data_u32[0];
            volatile uint32_t *pWr = (volatile uint32_t*)pPrgDataBuf->address;

            _Static_assert(EAP_C55FMC_SIZE_OF_QUAD_PAGE % 4u == 0u, "Bad configuration");
            for(unsigned int u=0u; u<EAP_C55FMC_SIZE_OF_QUAD_PAGE/4u; ++u)
                * pWr++ = * pRd++;

            /* After filling the write buffer, we set MCR[EHV] to turn on the high voltage
               for flashing. See RM48, 74.5.1, p.3656. */
            C55FMC->MCR |= C55FMC_MCR_EHV_MASK;

            /* Save the reference to the input data buffer; we still need it at the end for a
               verify. */
            _pPrgDataBuf = pPrgDataBuf;

            /* This is a non-blocking function. We don't wait for the result but will check
               it in the next clock tick. */
            retCode = rom_err_processPending;

        } /* if(Quad-page is erased?) */
    }
    else
    {
        /* Turn off high voltage, reset all operation request bits and lock flash blocks. */
        abortEraseAndProgram();

        retCode = rom_err_unexpectedHwState;
    }

    return retCode;

} /* eap_osStartProgramQuadPage */


/**
 * Check the status of a programming operation, which had been initiated before by
 * eap_osStartProgramQuadPage().
 *   @return
 * Get the status of the operation. eap_osStartEraseFlashBlocks() or
 * eap_osStartProgramQuadPage() can be called earliest again when this function returns
 * either success or failure - but not yet while it reports a pending state
 * (#rom_err_processPending).
 */
rom_errorCode_t eap_osGetStatusProgramQuadPage(void)
{
    rom_errorCode_t retCode;

    /* There must be a program operation but no suspend or erase visible. */
    const uint32_t mcr = C55FMC->MCR;
    #define BITS_TO_BE_SET      (C55FMC_MCR_PGM_MASK|C55FMC_MCR_EHV_MASK)
    #define BITS_TO_BE_CLEARED  (C55FMC_MCR_ERS_MASK|C55FMC_MCR_PSUS_MASK|C55FMC_MCR_ESUS_MASK)
    if((mcr & BITS_TO_BE_SET) == BITS_TO_BE_SET  &&  (mcr & BITS_TO_BE_CLEARED) == 0u)
    #undef BITS_TO_BE_SET
    #undef BITS_TO_BE_CLEARED
    {
        /* Check MCR[DONE]. 1 means operation done. */
        if((mcr & C55FMC_MCR_DONE_MASK) != 0u)
        {
            /* Check MCR[PEG]. A zero bit indicates a programming error. */
            if((mcr & C55FMC_MCR_PEG_MASK) != 0u)
                retCode = rom_err_noError;
            else
                retCode = rom_err_c55FmcErrorInPeg;
        }
        else
        {
            /* MCR[DONE]=0: The operation is still in progress. We wait till next clock
               tick for the next check. */
            retCode = rom_err_processPending;
        }
    }
    else
        retCode = rom_err_unexpectedHwState;

    if(retCode != rom_err_processPending)
    {
        /* Turn off high voltage, reset all operation request bits and lock flash blocks. */
        abortEraseAndProgram();

        /* Before we continue with the verify, we invalidate all quad-page addresses in the
           D-cache - without we would likely not read the bytes physically programmed in
           the flash array but the still cached data from the interlock writes, and verify
           would then always succeed. */
        invalidateDCache(_pPrgDataBuf->address);

        /* After terminating high voltage and programming mode, the data from the partition
           is again readable and we can verify the programming result. */
        if(retCode == rom_err_noError  && !verifyQuadPage(_pPrgDataBuf))
            retCode = rom_err_verifyFailed;
    }

    return retCode;

} /* eap_osGetStatusProgramQuadPage */

#endif /* EAP_TEST_WITH_MOCKUP */
