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
 *   eap_startProgramQuadPage
 *   eap_getStatusProgramQuadPage
 * Local functions
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
 * Start the programming of a single quad-page in the C55 controller.\n
 *   The preconditions are checked (availability of controller for new write-page
 * operation), the write buffer is filled with the data and high voltage is enabled, which
 * makes the flash programming begin. The function is non-blocking and doesn't wait for the
 * termination of the program step. Use 
 *  @param[in] pPrgDataBuf
 * The buffer with the data to program and the target address in flash ROM.
 */
status_t eap_startProgramQuadPage(dib_pageProgramBuffer_t * const pPrgDataBuf)
{
    status_t returnCode; 
    
    /* Cases that program operation can start:
         1. No program and erase sequence:(PGM low and ERS low)
         2. Erase suspend with EHV low: (PGM low, ERS high, ESUS high, EHV low)
       Cases that program operation cannot start:
         1. Program in progress (PGM high & EHV high)
         2. Program in suspended state (PGM high & PSUS high)
         3. Program not in progress (PGM low), but erase in progress but not in suspend
       state. */
// TODO Simplify to: PGM=ERS=EHV=ESUS=PSUS=low
    const uint32_t mcr = C55FMC->MCR;
    if(((mcr & C55FMC_MCR_PGM_MASK) == 0u  
        ||  (mcr & (C55FMC_MCR_EHV_MASK|C55FMC_MCR_PSUS_MASK)) == 0u
       )
       && ((mcr & C55FMC_MCR_ERS_MASK) == 0u  ||  (mcr & C55FMC_MCR_ESUS_MASK) != 0u)
      )
    {
        /* Enable the flash block for programming, which the wanted quad-page sits in. */
// TODO Generalize
        assert(pPrgDataBuf->address >= 0xFC0000u  &&  pPrgDataBuf->address+128u <= 0xFC8000u);
        C55FMC->LOCK0 &= ~C55FMC_LOCK0_LOWLOCK(4u); /* Bit 2: 0xFC0000..0xFC8000. */
        C55FMC->SEL0 = C55FMC_LOCK0_LOWLOCK(4u); /* Bit 2: 0xFC0000..0xFC8000. */
        
        /* Set MCR[PGM] to start program operation. */
        C55FMC->MCR |= C55FMC_MCR_PGM_MASK;
        
        /* Read bit back to see if we didn't hurt the bit lock rules. */
        if((C55FMC->MCR & C55FMC_MCR_PGM_MASK) != 0u)
        {
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
            returnCode = STATUS_FLASH_IN_PROGRESS;
        }
        else
        {
            /* Unexpected state. */
// TODO Generalize
            C55FMC->LOCK0 |= C55FMC_LOCK0_LOWLOCK(4u); /* Bit 2: 0xFC0000..0xFC8000. */
            C55FMC->SEL0 &= ~C55FMC_LOCK0_LOWLOCK(4u); /* Bit 2: 0xFC0000..0xFC8000. */

            returnCode = STATUS_FLASH_ERR_UNEXPECTED_STATE;
        }
    }
    else
        returnCode = STATUS_BUSY;

    return returnCode;
    
} /* eap_startProgramQuadPage */


/**
 * Check the status of a programming operation, which had been initiated before by
 * eap_startProgramQuadPage(). 
 *   @return
 * Get the status of the operation. eap_startProgramQuadPage() can be called earliest again
 * when this function returns either success or failure - but not yet while it reports a
 * pending state.
 */
status_t eap_getStatusProgramQuadPage(void)
{
    status_t returnCode;
    
    /* There must be a program operation but no suspend or erase visible. */
    const uint32_t mcr = C55FMC->MCR;
    #define BITS_TO_BE_SET      (C55FMC_MCR_PGM_MASK|C55FMC_MCR_EHV_MASK)
    #define BITS_TO_BE_CLEARED  (C55FMC_MCR_ERS_MASK|C55FMC_MCR_PSUS_MASK|C55FMC_MCR_ESUS_MASK)
    if((mcr & BITS_TO_BE_SET) == BITS_TO_BE_SET  &&  (mcr & BITS_TO_BE_CLEARED) == 0u)
    #undef BITS_TO_BE_SET
    #undef BITS_TO_BE_CLEARED
    {
        /* Check MCR[DONE]. 1 means operation done. */
// TODO We need a timeout here. However, what to do if it elapses?
        if((mcr & C55FMC_MCR_DONE_MASK) != 0u)
        {
            /* Check MCR[PEG]. A zero bit indicates a programming error. */
            if((mcr & C55FMC_MCR_PEG_MASK) != 0u)
                returnCode = STATUS_SUCCESS;
            else
                returnCode = STATUS_ERROR_IN_PGM;
                
            /* High voltage and program mode are disabled in two steps. This is a rule from
               the bit locking. */
            C55FMC->MCR &= ~C55FMC_MCR_EHV_MASK;
            C55FMC->MCR &= ~C55FMC_MCR_PGM_MASK;
        }
        else
        {
            /* MCR[DONE]=0: The operation is still in progress. We wait till next clock
               tick for for the next check. */
            returnCode = STATUS_FLASH_IN_PROGRESS;
        }
    }
    else
    {
        /* We are in an unexpected state. Unconditionally turn high programming voltage and
           programming or erase mode off. */
        C55FMC->MCR &= ~C55FMC_MCR_EHV_MASK;
        C55FMC->MCR &= ~(C55FMC_MCR_PGM_MASK | C55FMC_MCR_ERS_MASK);

        returnCode = STATUS_FLASH_ERR_UNEXPECTED_STATE;
        
    } /* if(HW is in the expected state, which is programming?) */
    
    if(returnCode != STATUS_FLASH_IN_PROGRESS)
    {
// TODO Generalize and centralize
        C55FMC->LOCK0 |= C55FMC_LOCK0_LOWLOCK(4u); /* Bit 2: 0xFC0000..0xFC8000. */
        C55FMC->SEL0 &= ~C55FMC_LOCK0_LOWLOCK(4u); /* Bit 2: 0xFC0000..0xFC8000. */
    }

    return returnCode;
    
} /* eap_getStatusProgramQuadPage */


dib_pageProgramBuffer_t SDATA_OS(eap_prgDataBuf) =
{
    .address = 0xFC0000u,
    .data_b = 
    {
        [0 ... (DIB_C55FMC_SIZE_OF_QUAD_PAGE-1u)] = 0u,
    },
};
unsigned int eap_noWaitCycles = 0u;
status_t eap_resultGetStatus = STATUS_INVALID
       , eap_resultStartPrg = STATUS_INVALID;
bool eap_firstTest(void)
{
    for(unsigned int u=0u; u<DIB_C55FMC_SIZE_OF_QUAD_PAGE; ++u)
        eap_prgDataBuf.data_b[u] = u & 0xFFu;
    
    eap_resultStartPrg = eap_startProgramQuadPage(&eap_prgDataBuf);

    if(eap_resultStartPrg == STATUS_FLASH_IN_PROGRESS)
    {
        eap_noWaitCycles = 0u;
        while(true)
        {
            eap_resultGetStatus = eap_getStatusProgramQuadPage();
            if(eap_resultGetStatus == STATUS_FLASH_IN_PROGRESS)
                ++ eap_noWaitCycles;
            else
                break;
        }
    }
    
    return eap_resultStartPrg == STATUS_SUCCESS  &&  eap_resultGetStatus == STATUS_SUCCESS;
}
