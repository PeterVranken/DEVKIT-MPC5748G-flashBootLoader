/**
 * @file rom_flashRom.c
 * A flash ROM driver for MPC5748G and MPC5775B/E. Supports user mode operations, erasure
 * and programming of the flash array.
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
 *   rom_isFlashDriverBusy
 *   rom_isValidFlashAddressRange
 *   rom_startEraseFlashMemory
 *   rom_startProgram
 * Local functions
 */

/*
 * Include files
 */

#include "rom_flashRom.h"

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "dib_dataInputBuffer.h"
#include "eap_eraseAndProgram.h"

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

/** Temporarily used variable for emulation of erasure. */
static unsigned int _tiBusy = 0u;

/** The buffer currently in use for caching data to program later into the flash array. */
dib_pageProgramBuffer_t *_pDataInputBuf = NULL;

/** The buffer currently being programmed into the flash array. */
dib_pageProgramBuffer_t *_pPrgBuf = NULL;

/*
 * Function implementation
 */

/**
 * Check the state of the flash driver.\n
 *   @return
 * Get \a false if the driver is idle; a new erase or program command can be accepted. If
 * \a false is returned then a new erase or program command will probably fail.
 */
bool rom_isFlashDriverBusy(void)
{
    /* The driver is surely free to accept the next write command if there are at least two
       input buffers left. (A single write can't span more than two flash quad pages.) */
    return _tiBusy > 0u  ||  dib_getNoFreeInputBuffers() < 2u;
}


/**
 * Check if a memory address range is completely in the portion of the flash ROM, which is
 * managed by the FBL.
 *   @param[in] address
 * The first address of the memory area.
 *   @param[in] size
 * The length of the memory range in Byte.
 */
bool rom_isValidFlashAddressRange(uint32_t address, uint32_t size)
{
#if defined(MCU_MPC5748G)
    const uint32_t endAddr = address + size;

    /* We can handle the overflow at the end of the 32 Bit address space very easily,
       because the very last address in the address space is in no way manageable flash
       ROM. Caution, this might be different on other devices. Our driver implementation
       will fail at many code locations in this case. */
    if(endAddr < address)
        return false;

    /* The hard-coded limits are checked for consistency with the flash block configuration
       in the driver initialization. */
    return address >= 0x00FA0000u  &&  endAddr <= 0x01580000u;

#elif defined(MCU_MPC5775B) || defined(MCU_MPC5775E)
# error Implement rom_isValidFlashAddressRange for MPC5775B/E
#elif defined(MCU_MPC5777C)
# error Implement rom_isValidFlashAddressRange for MPC5777C
#endif
} /* rom_isValidFlashAddressRange */


/**
 * Initiate erasure of a portion of the flash ROM.\n
 *   The demanded address range doesn't need to be identical with one or more contiguous
 * flash blocks. If this is not the case, then the command will erase more bytes then
 * specified. All flash blocks, which share at least one byte with the specified address
 * range, will be erased.\n
 *   The function just triggers the operation; on return the flash is not yet erased. Use
 * rom_isFlashDriverBusy() to see, when the operation is completed.
 *   @return
 * Get \a true if the command could be started. If the driver is busy or if the address
 * range spans more bytes, which aren't in any of the supported flash blocks, then \a false
 * is returned.
 *   @param address
 * The first address to be erased.
 *   @param noBytes
 * The number of bytes to erase at \a address.
 */
bool rom_startEraseFlashMemory(uint32_t address, uint32_t noBytes)
{
    if(_tiBusy == 0u  && rom_isValidFlashAddressRange(address, noBytes))
    {
        /* Scaling such that erasing all 4MB takes 10s. */
        _tiBusy = (uint32_t)(5000ull * (uint64_t)noBytes / 2097152ull) + 1000u;
        return true;
    }
    else
        return false;
}


/* Initiate programming a number of bytes. */
bool rom_startProgram(uint32_t address, const uint8_t *pDataToProgram, uint32_t noBytes)
{
    /* We can check the preconditions under which we surely know, that we have sufficient
       buffer space for accepting the command. */
    if(!rom_isFlashDriverBusy() && rom_isValidFlashAddressRange(address, noBytes))
    {
        if(_pDataInputBuf == NULL)
        {
            _pDataInputBuf = dib_acquireInputBuffer();
            assert(_pDataInputBuf != NULL);
        }

        const uint32_t noBytesWritten = dib_writeDataIntoBuffer( _pDataInputBuf
                                                               , address
                                                               , noBytes
                                                               , pDataToProgram
                                                               );
        assert(noBytesWritten <= noBytes);
        if(noBytesWritten < noBytes)
        {
            /* The bytes didn't all fit into the current buffer. Because the driver assumes
               that the bytes are written in the order of increasing addresses, it is clear
               that the buffer used so far is full and can be passed to the programming. */
            dib_releaseBuffer(_pDataInputBuf, /*submitForProgramming*/ true);

            /* Those bytes, which couldn't be written into the previous buffer are written
               into the next one. We know, that we will get another one because we had
               checked this as precondition for accepting the write job. */
            _pDataInputBuf = dib_acquireInputBuffer();
            assert(_pDataInputBuf != NULL);
#ifdef DEBUG
            const uint32_t noBytesWritten2nd =
#endif
            dib_writeDataIntoBuffer( _pDataInputBuf
                                   , address + noBytesWritten
                                   , noBytes - noBytesWritten
                                   , pDataToProgram + noBytesWritten
                                   );
            assert(noBytesWritten2nd + noBytesWritten == noBytes);
        }

        return true;
    }
    else
    {
        /* The data might still fit in the remaining buffer space, but we don't take the
           risk of getting stuck after coyping the data partially. We reject the command. */
        return false;
    }
} /* rom_startProgram */


/**
 * All data, which has already been handed over to the flash ROM driver using
 * rom_startProgram() is submitted for programming. This implies that, if the last filled
 * quad-page is not completed yet, this quad-page is finalized. Not yet written bytes in
 * this page can't be written later any more.\n
 *   This operation needs to be used at the end of data transmission, after the very last
 * call of rom_startProgram(), in order to make all written data be programmed. (Instead of
 * infinitely waiting for possibly more bytes to go into the last recently begun
 * quad-page.)\n
 *   It is not required to call this function in case of address gaps in the programmed
 * data. If an address gap leads to writing to another quad-page even before the previous
 * one is completed then the flush operation is unconditionally done internally for the
 * left quad-page. However, an explicit call of this function would also not do any harm.
 */
void rom_flushProgramDataBuffer(void)
{
    if(_pDataInputBuf != NULL)
    {
        dib_releaseBuffer(_pDataInputBuf, /*submitForProgramming*/ true);
        _pDataInputBuf = NULL;
    }
} /* rom_flushProgramDataBuffer */


bool SBSS_OS(rom_startTest) = false;
bool SDATA_OS(rom_continueTest) = false;

void rom_flashRomMain(void)
{
    assert(!rom_startTest || !rom_continueTest);
    if(rom_startTest || rom_continueTest)
    {
        rom_continueTest = eap_firstTest(rom_startTest);
        rom_startTest = false;
    }

    /* Emulation of erasure. */
    if(_tiBusy > 0u)
        -- _tiBusy;

    /* Emulation of programming. */
    #define DELAY_PER_ROW   20u
    static unsigned int SDATA_OS(noBytesLeft_);
    static uint32_t SDATA_OS(wrAddr_);
    static const uint8_t * SDATA_OS(pData_);
    static unsigned int SDATA_OS(cntDelay_);

    if(_pPrgBuf == NULL)
    {
        /* Check for potential new flash jobs. */
        _pPrgBuf = dib_acquireProgramBuffer();

        if(_pPrgBuf != NULL)
        {
            eap_quadPageProgramBuffer_t * const pPageBuf = dib_getBufferPayload(_pPrgBuf);
            noBytesLeft_ = EAP_C55FMC_SIZE_OF_QUAD_PAGE;
            wrAddr_ = pPageBuf->address;
            pData_ = &pPageBuf->data_b[0];
            cntDelay_ = DELAY_PER_ROW;
        }
    }
    if(_pPrgBuf != NULL)
    {
#if 1 /* This path: Emulate a programming, which is slower than data transmission and which
         slows down the CCP communication. */

        if(cntDelay_ > 0u)
        {
            -- cntDelay_;
        }
        else if(noBytesLeft_ > 0u)
        {
            iprintf( "P: %06lX  %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\r\n"
                   , wrAddr_
                   , (unsigned)pData_[ 0]
                   , (unsigned)pData_[ 1]
                   , (unsigned)pData_[ 2]
                   , (unsigned)pData_[ 3]
                   , (unsigned)pData_[ 4]
                   , (unsigned)pData_[ 5]
                   , (unsigned)pData_[ 6]
                   , (unsigned)pData_[ 7]
                   , (unsigned)pData_[ 8]
                   , (unsigned)pData_[ 9]
                   , (unsigned)pData_[10]
                   , (unsigned)pData_[11]
                   , (unsigned)pData_[12]
                   , (unsigned)pData_[13]
                   , (unsigned)pData_[14]
                   , (unsigned)pData_[15]
                   );
            wrAddr_ += 16u;
            pData_ += 16u;
            noBytesLeft_ -= 16;

            if(noBytesLeft_ > 0u)
                cntDelay_ = DELAY_PER_ROW;
        }
        else
        {
            dib_releaseBuffer(_pPrgBuf, /*submitForProgramming*/ false);
            _pPrgBuf = NULL;
        }
        
#else /* This path: Emulate programming in realistic speed. Measurements show a programming
         time of 0.3ms per quad-page: We will be ready already in the next 1ms-clock-tick.
         CCP communication limits the speed, not programming. */
        dib_releaseBuffer(_pPrgBuf, /*submitForProgramming*/ false);
        _pPrgBuf = NULL;
#endif
    }
} /* rom_flashRomMain */