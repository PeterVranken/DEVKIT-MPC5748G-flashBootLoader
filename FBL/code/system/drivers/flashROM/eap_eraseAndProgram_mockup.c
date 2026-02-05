/**
 * @file eap_eraseAndProgram_mockup.c
 * Emulating implemetation of erase and program operations for the C55FMC. This file can be
 * used instead of eap_eraseAndProgram.c for testing the CCP protocol stack without
 * actually modifying the flash ROM array.
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
 *   setTiPendingInMs
 *   setTiPendingInUs
 *   isPending
 */

/*
 * Include files
 */

#include "eap_eraseAndProgram.h"

#if TEST_WITH_MOCKUP == 1

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "rom_flashRomDriver.h"
#include "stm_systemTimer.h"
/*
 * Defines
 */

/** Enable (1) or disable (0) logging of all programmed bytes. Logging significantly slows
    down the programming speed and can be used only with small data set. If set to 0, the
    the emulation behaves identically to the real hardware (with respect to timing). */
#define LOG_PROGRAMMED_DATA     0

/** The STM timer used for timeouts by zero based index. */
#define TIMER 2u

/** Helper: Define a wait time span in ms and rescale to the clock rate of the selected STM
    timer, #TIMER. Note, \a tiInMs must not exceed 6871947. (Overflow is not handled.) */
#define TI_MS(tiInMs) ((625u*(tiInMs) + 1u) / 2u)

/** Helper: Define a wait time span in ms and rescale to the clock rate of the selected STM
    timer, #TIMER. Note, \a tiInMs must not exceed 858993457. (Overflow is not handled.) */
#define TI_US(tiInUs) ((5u*(tiInUs) + 8u) >> 4)


/*
 * Local type definitions
 */


/*
 * Local prototypes
 */


/*
 * Data definitions
 */

/** Which operation is currently emulated? */
static enum {state_idle, state_erasing, state_programming} _state SECTION(.data.OS._state) =
                                                                                 state_idle;

/** When will the pending operation complete? Unit is #STM_TIMER_2_PERIOD_IN_NS, 3.2us. */
static uint32_t DATA_OS(_tiWaitEnd) = 0u;

/*
 * Function implementation
 */

/** 
 * Set time, which an emulated process lasts.
 *   @param[in] tiPendingInMs
 * The time to wait for in ms. Range is 0..6871947.
 */
static inline void setTiPendingInMs(uint32_t tiPendingInMs)
{
    assert(tiPendingInMs < 6871947u);
    _tiWaitEnd = stm_osGetSystemTime(TIMER) + TI_MS(tiPendingInMs);
}


/** 
 * Set time, which an emulated process lasts.
 *   @param[in] tiPendingInUs
 * The time to wait for in us. Range is 0..858993457.
 */
static inline void setTiPendingInUs(uint32_t tiPendingInUs)
{
    assert(tiPendingInUs < 858993457u);
    _tiWaitEnd = stm_osGetSystemTime(TIMER) + TI_US(tiPendingInUs);
}


/**
 * Check if wait time has elapsed.
 *   @return
 * Get \a true, if wait period is still ongoing and \a false if the process has completed.
 */
static inline bool isPending(void)
{
    return (int)(int32_t)(_tiWaitEnd - stm_osGetSystemTime(TIMER)) > 0;
}


/**
 * Initialize the flash ROM driver.
 */
void eap_osInitFlashRomDriver(void)
{
    _state = state_idle;
    _tiWaitEnd = stm_osGetSystemTime(TIMER) - 1u;
    
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

    if(!rom_isValidFlashAddressRange(addressFrom, noBytes))
    {
        retCode = rom_err_badAddressRange;
    }

    /* To initiate erasure, no other operation must be in progress. */
    else if(_state == state_idle)    
    {
        /* Erasure is emulated by being blocked for a while, which depends on the size of
           the block. We make the time dependent on the number of 256k blocks to erase. */
        const uint32_t tiEraseInMs = 1000u /*ms per block*/ * ((noBytes + 0x3FFFFu) >> 18);
        setTiPendingInMs(tiEraseInMs);

        iprintf( "Emulation: Erasing %lu Byte at 0x%06lX. This will take %lu ms.\r\n"
               , noBytes
               , addressFrom
               , tiEraseInMs
               );

        _state = state_erasing;
        retCode = rom_err_processPending;
    }
    else
    {
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

    if(_state == state_erasing)    
    {
        if(isPending())
            retCode = rom_err_processPending;
        else
        {
            _state = state_idle;
            retCode = rom_err_noError;
        }
    }
    else
    {
        retCode = rom_err_unexpectedHwState;
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
 * eap_abortEraseAndProgram().
 */
rom_errorCode_t eap_osStartProgramQuadPage(eap_quadPageProgramBuffer_t * const pPrgDataBuf)
{
    rom_errorCode_t retCode;

    if( EAP_GET_ADDR_OFFS_IN_QUAD_PAGE(pPrgDataBuf->address) != 0u
        || !rom_isValidFlashAddressRange(pPrgDataBuf->address, EAP_C55FMC_SIZE_OF_QUAD_PAGE)
      )
    {
        retCode = rom_err_badAddressRange;
    }
    else if(_state == state_idle)
    {
        /* Programming is emulated by printing the bytes to the console, which requires a
           lot of time. We need to delay the emulated process accordingly. We need 3
           characters per byte plus formatting characters for 8 rows, about 490 characters
           in total, which requires about 43 ms.
             Another test mode doesn't log the bytes and can be as fast a true flash
           programming, i.e., it'll be done in the next clock tick. */
#if LOG_PROGRAMMED_DATA == 1
        iprintf("Emulation: Programming quad-page:\r\n");
        uint32_t addr = pPrgDataBuf->address;
        uint8_t *pData = &pPrgDataBuf->data_b[0];
        for(unsigned int row=0u; row<8u; ++row)
        {
        
            iprintf( "%06lX: %02X %02X %02X %02X %02X %02X %02X %02X"
                           " %02X %02X %02X %02X %02X %02X %02X %02X\r\n"
                   , addr
                   , (unsigned)pData[ 0]
                   , (unsigned)pData[ 1]
                   , (unsigned)pData[ 2]
                   , (unsigned)pData[ 3]
                   , (unsigned)pData[ 4]
                   , (unsigned)pData[ 5]
                   , (unsigned)pData[ 6]
                   , (unsigned)pData[ 7]
                   , (unsigned)pData[ 8]
                   , (unsigned)pData[ 9]
                   , (unsigned)pData[10]
                   , (unsigned)pData[11]
                   , (unsigned)pData[12]
                   , (unsigned)pData[13]
                   , (unsigned)pData[14]
                   , (unsigned)pData[15]
                   );
            addr += 16u;
            pData += 16u;
        }
        setTiPendingInMs(44u);
#else
        setTiPendingInUs(300u);
#endif
        _state = state_programming;

        /* This is a non-blocking function. We don't wait for the result but will check
           it in the next clock tick. */
        retCode = rom_err_processPending;
    }
    else
    {
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

    if(_state == state_programming)    
    {
        if(isPending())
            retCode = rom_err_processPending;
        else
        {
            _state = state_idle;
            retCode = rom_err_noError;
        }
    }
    else
    {
        retCode = rom_err_unexpectedHwState;
    }

    return retCode;

} /* eap_osGetStatusProgramQuadPage */

#endif /* TEST_WITH_MOCKUP */
