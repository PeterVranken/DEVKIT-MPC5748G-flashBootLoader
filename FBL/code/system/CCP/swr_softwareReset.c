/**
 * @file swr_softwareReset.c
 * Trigger a software reset, a functional, non-destructive reset, which doesn't kill the
 * RAM contents, so that we can exchange information between application and FBL across
 * resets.
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
 * Local functions
 */

/*
 * Include files
 */

#include "swr_softwareReset.h"

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "MPC5748G.h"
#include "rtos.h"

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
 * Start the countdown till a functional reset.\n
 *   To end up with a reset, this function should be called regularly once a Millisecond
 * until reset. A short delay is implemented such that a feedback can still be written to
 * the console.
 *   @param[in] bootFlag
 * A 32 Bit word, which is passed to the FBL across the demanded reset. Must be either
 * #SWR_BOOT_FLAG_START_FBL or #SWR_BOOT_FLAG_START_APP.
 */
void swr_osSoftwareReset(uint32_t bootFlag)
{
    /* Only allowing the two flags is a restriction of the FBL itself. The reset and
       startup code allows any value >= 0x10. (The lower numbers are reserved for error
       codes.) */
    assert(bootFlag == SWR_BOOT_FLAG_START_FBL  ||  bootFlag == SWR_BOOT_FLAG_START_APP);

    /* Restart the application by reset. The information, whether and what to start
       is passed in SRAM to the FBL across the reset. The startup code of the FBL
       will evaluate the SRAM location. */
    rtos_osSuspendAllInterrupts();

    /* Write arguments of reset at hard-coded address into RAM. We must do this
       only after suspending all IRQ handling, because we may corrupt the memories
       of some other part of the SW. No other piece of SW must ever get the chance
       to execute once we get here. */
    extern const uint8_t const ld_memRamStart[0];
    uint32_t *pResetInfo = (uint32_t*)ld_memRamStart;
    * pResetInfo++ = SWR_MAGIC_IS_BOOT_FLAG_VALID;
    * pResetInfo   = bootFlag;

    /* Request mode transition to RESET mode (0x0) by write of key, followed by
       inverted key. RM48 38.3.2, pp.1096f. */
    MC_ME->MCTL = MC_ME_MCTL_TARGET_MODE(0x0) | MC_ME_MCTL_KEY(0x5AF0);
    MC_ME->MCTL = MC_ME_MCTL_TARGET_MODE(0x0) | MC_ME_MCTL_KEY(0xA50F);

    /* Halt all SW execution until the reset is executed. */
    while(true)
        ;

} /* swr_osSoftwareReset */
