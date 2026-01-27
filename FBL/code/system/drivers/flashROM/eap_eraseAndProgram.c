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
 *   eap_abortEraseAndProgram
 * Local functions
 *   disableAllFlashBlocks
 */

/*
 * Include files
 */

#include "eap_eraseAndProgram.h"

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "rom_flashRom.h"

/** @todo Define either MCU_MPC5748G, MCU_MPC5775B or MCU_MPC5775E to select the MCU, which
    this module is compiled for. */
#define MCU_MPC5748G

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


/*
 * Local prototypes
 */


/*
 * Data definitions
 */


/*
 * Function implementation
 */

/**
 * Helper: All flash blocks are (initially) protected against erasure and programming;
 * over-programming is disabled, too.
 */
static void disableAllFlashBlocks(void)
{
#if defined(MCU_MPC5748G)
# define LOCK_ALL_BLKS_0    0xBFFFFFFFu
# define LOCK_ALL_BLKS_1    0xFFFFFFFFu
# define LOCK_ALL_BLKS_2    0xFFFFFFFFu
# define LOCK_ALL_BLKS_3    0xFFFFFFFFu
# define SELECT_NO_BLK_0    0x00000000u
# define SELECT_NO_BLK_1    0x00000000u
# define SELECT_NO_BLK_2    0x00000000u
# define SELECT_NO_BLK_3    0x00000000u
#elif defined(MCU_MPC5775B) || defined(MCU_MPC5775E)
# error Specify flash block configuration for MPC5775B/E
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
 * Initialize the flash ROM driver.
 */
void eap_osInitFlashRomDriver(void)
{
    eap_abortEraseAndProgram();

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
 *   @param[in] addressTo
 * End address of blanked address range (exclusive).
 */
rom_errorCode_t eap_osStartEraseFlashBlocks(uint32_t addressFrom, uint32_t addressTo)
{
    rom_errorCode_t retCode;

    /* To initiate erasure, no other operation must be in progress. */
    const uint32_t mcr = C55FMC->MCR;
    #define BITS_TO_BE_CLEARED  (C55FMC_MCR_ERS_MASK     \
                                 |C55FMC_MCR_PGM_MASK    \
                                 |C55FMC_MCR_EHV_MASK    \
                                 |C55FMC_MCR_PSUS_MASK   \
                                 |C55FMC_MCR_ESUS_MASK   \
                                )
    if((mcr & BITS_TO_BE_CLEARED) == 0u)
    #undef BITS_TO_BE_CLEARED
    {
        /* Enable the flash blocks for erase, which are touched by the address range. */
        /* Set MCR[ERS] to start erase operation. */
        C55FMC->MCR |= C55FMC_MCR_ERS_MASK;

// TODO Generalize
        assert(addressFrom >= 0xFC0000u  &&  addressTo <= 0xFC8000u);
        C55FMC->LOCK0 &= ~C55FMC_LOCK0_LOWLOCK(4u); /* Bit 2: 0xFC0000..0xFC8000. */
        C55FMC->SEL0 = C55FMC_LOCK0_LOWLOCK(4u); /* Bit 2: 0xFC0000..0xFC8000. */

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
        /* Turn off high voltage and reset all operation request bits. */
        eap_abortEraseAndProgram();

// TODO Generalize and centralize
        C55FMC->LOCK0 |= C55FMC_LOCK0_LOWLOCK(4u); /* Bit 2: 0xFC0000..0xFC8000. */
        C55FMC->SEL0 &= ~C55FMC_LOCK0_LOWLOCK(4u); /* Bit 2: 0xFC0000..0xFC8000. */
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
        /* Turn off high voltage and reset all operation request bits. */
        eap_abortEraseAndProgram();

// TODO Generalize and centralize
        C55FMC->LOCK0 |= C55FMC_LOCK0_LOWLOCK(4u); /* Bit 2: 0xFC0000..0xFC8000. */
        C55FMC->SEL0 &= ~C55FMC_LOCK0_LOWLOCK(4u); /* Bit 2: 0xFC0000..0xFC8000. */
    }

    return retCode;
}


/**
 * Start the programming of a single quad-page in the C55 controller.\n
 *   The preconditions are checked (availability of controller for new write-page
 * operation), the write buffer is filled with the data and high voltage is enabled, which
 * makes the flash programming begin. The function is non-blocking and doesn't wait for the
 * termination of the program step. Use eap_osGetStatusProgramQuadPage() to find out, when
 * it has terminated.
 *  @return
 * Get the status: If everything succeeded, then it is pending (#rom_err_processPending),
 * otherwise an error message.
 *  @param[in] pPrgDataBuf
 * The buffer with the data to program and the target address in flash ROM.
 */
rom_errorCode_t eap_osStartProgramQuadPage(dib_pageProgramBuffer_t * const pPrgDataBuf)
{
    rom_errorCode_t retCode;

    /* To initiate programming, no other operation must be in progress. */
    const uint32_t mcr = C55FMC->MCR;
    #define BITS_TO_BE_CLEARED  (C55FMC_MCR_ERS_MASK     \
                                 |C55FMC_MCR_PGM_MASK    \
                                 |C55FMC_MCR_EHV_MASK    \
                                 |C55FMC_MCR_PSUS_MASK   \
                                 |C55FMC_MCR_ESUS_MASK   \
                                )
    if((mcr & BITS_TO_BE_CLEARED) == 0u)
    #undef BITS_TO_BE_CLEARED
    {
        /* Enable the flash block for programming, which the wanted quad-page sits in. */
// TODO Generalize
        assert(pPrgDataBuf->address >= 0xFC0000u  &&  pPrgDataBuf->address+128u <= 0xFC8000u);
        C55FMC->LOCK0 &= ~C55FMC_LOCK0_LOWLOCK(4u); /* Bit 2: 0xFC0000..0xFC8000. */

        /* Set MCR[PGM] to start program operation. */
        C55FMC->MCR |= C55FMC_MCR_PGM_MASK;

        /* We always write an entire quad-page at once. */

        /* Copy quad-page contents into the controller's data buffer. This is at the
           same time the required interlock write. */
        const uint32_t *pRd = &pPrgDataBuf->data_u32[0];
        volatile uint32_t *pWr = (volatile uint32_t*)pPrgDataBuf->address;
        // TODO Check before setting PGM?
        assert(((uintptr_t)pWr & (DIB_C55FMC_SIZE_OF_QUAD_PAGE-1u)) == 0u
               && rom_isValidFlashAddressRange((uint32_t)pWr, DIB_C55FMC_SIZE_OF_QUAD_PAGE)
              );

        _Static_assert(DIB_C55FMC_SIZE_OF_QUAD_PAGE % 4u == 0u, "Bad configuration");
        for(unsigned int u=0u; u<DIB_C55FMC_SIZE_OF_QUAD_PAGE/4u; ++u)
            * pWr++ = * pRd++;

        /* After filling the write buffer, we set MCR[EHV] to turn on the high voltage
           for flashing. See RM48, 74.5.1, p.3656. */
        C55FMC->MCR |= C55FMC_MCR_EHV_MASK;

        /* This is a non-blocking function. We don't wait for the result but will check
           it in the next clock tick. */
        retCode = rom_err_processPending;
    }
    else
    {
        /* Turn off high voltage and reset all operation request bits. */
        eap_abortEraseAndProgram();

// TODO Generalize and centralize
        C55FMC->LOCK0 |= C55FMC_LOCK0_LOWLOCK(4u); /* Bit 2: 0xFC0000..0xFC8000. */
        C55FMC->SEL0 &= ~C55FMC_LOCK0_LOWLOCK(4u); /* Bit 2: 0xFC0000..0xFC8000. */
        
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
        /* Turn off high voltage and reset all operation request bits. */
        eap_abortEraseAndProgram();

// TODO Generalize and centralize
        C55FMC->LOCK0 |= C55FMC_LOCK0_LOWLOCK(4u); /* Bit 2: 0xFC0000..0xFC8000. */
        C55FMC->SEL0 &= ~C55FMC_LOCK0_LOWLOCK(4u); /* Bit 2: 0xFC0000..0xFC8000. */
    }


    return retCode;

} /* eap_osGetStatusProgramQuadPage */


/**
 * This function stops all erase and program activities of the C55FMC.\n
 *   It can be called in case of errors or unexpected states.
 */
void eap_abortEraseAndProgram(void)
{
    /* The mode of operation bits are disabled in distinct steps. This is a rule from
       the bit locking. RM48, 74.6, Table 74-3, p.3679. */
    // TODO Abortion is likely not properly implemented. Compare conditions RM48, 74.5.1, p.3656, for resetting EHV. However, seems to affect only modes, we don't use anyway
    C55FMC->MCR &= ~C55FMC_MCR_PSUS_MASK;
    C55FMC->MCR &= ~C55FMC_MCR_ESUS_MASK;
    C55FMC->MCR &= ~C55FMC_MCR_EHV_MASK;
    C55FMC->MCR &= ~C55FMC_MCR_PGM_MASK;
    C55FMC->MCR &= ~C55FMC_MCR_ERS_MASK;

    /* Now we can restore the default settings for accesibility the of flash blocks. */
    disableAllFlashBlocks();

} /* eap_abortEraseAndProgram */


/***************************** Test, temporary code ******************/

dib_pageProgramBuffer_t BSS_OS(eap_prgDataBuf) =
{
    .address = 0u,
    .data_b =
    {
        [0 ... (DIB_C55FMC_SIZE_OF_QUAD_PAGE-1u)] = 0u,
    },
};
unsigned int eap_noWaitCyclesErase = 0u
           , eap_noWaitCyclesPgm = 0u
           , eap_cntFsm = 0u;
rom_errorCode_t eap_resultStartErase = rom_err_invalidErrorCode
              , eap_resultGetStatusErase = rom_err_invalidErrorCode
              , eap_resultStartPgm = rom_err_invalidErrorCode
              , eap_resultGetStatusPgm = rom_err_invalidErrorCode;
/** Return true if state machine requires continued calling for completing the process. */
bool eap_firstTest(bool start)
{
    static enum {idle, init, error, waitForErase, program, success, } state_ SECTION(.sdata.OS.var) = idle;
    
    if(start)
    {
        assert(state_ == idle  ||  state_ == success);
        state_ = init;
    }
    else
        assert(state_ == waitForErase  ||  state_ == program);
    
    if(state_ == init)
    {
        eap_cntFsm = 0u;
        eap_prgDataBuf.address = 0xFC0100u;
        static uint8_t SDATA_OS(startVal_) = 1u;
        for(unsigned int u=0u; u<DIB_C55FMC_SIZE_OF_QUAD_PAGE; ++u)
            eap_prgDataBuf.data_b[u] = (startVal_ + u) & 0xFFu;
        ++ startVal_;

        eap_resultStartErase = eap_osStartEraseFlashBlocks( eap_prgDataBuf.address
                                                          , eap_prgDataBuf.address
                                                            + DIB_C55FMC_SIZE_OF_QUAD_PAGE
                                                          );
        if(eap_resultStartErase == rom_err_processPending)
        {
            eap_noWaitCyclesErase = 0u;
            state_ = waitForErase;
        }
        else
            state_ = error;
    }        
    else if(state_ == waitForErase)
    {
        ++ eap_noWaitCyclesErase;
        eap_resultGetStatusErase = eap_osGetStatusEraseFlashBlocks();
        
        if(eap_resultGetStatusErase == rom_err_noError)
            state_ = program;
        else if(eap_resultGetStatusErase != rom_err_processPending)
            state_ = error;
    }
    else if(state_ == program)
    {
        eap_resultStartPgm = eap_osStartProgramQuadPage(&eap_prgDataBuf);

        if(eap_resultStartPgm == rom_err_processPending)
        {
            eap_noWaitCyclesPgm = 0u;
            while(true)
            {
                eap_resultGetStatusPgm = eap_osGetStatusProgramQuadPage();
                if(eap_resultGetStatusPgm == rom_err_processPending)
                    ++ eap_noWaitCyclesPgm;
                else
                {
                    if(eap_resultGetStatusPgm == rom_err_noError)
                        state_ = success;
                    else
                        state_ = error;

                    break;
                }
            }
        }
        else
            state_ = error;
    }
    
    return state_ != error  &&  state_ != success;
}
