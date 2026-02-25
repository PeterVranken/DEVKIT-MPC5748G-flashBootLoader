/**
 * @file rom_flashRomDriver.c
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
 *   rom_osInitFlashRomDriver
 *   rom_isValidFlashAddressRange
 *   rom_osReadyToStartErase
 *   rom_osStartEraseFlashMemory
 *   rom_osReadyToStartProgram
 *   rom_osStartProgram
 *   rom_osFlushProgramDataBuffer
 *   rom_osFetchLastError
 *   rom_osFlashRomDriverMain
 * Local functions
 *   latchLastError
 */

/*
 * Include files
 */

#include "rom_flashRomDriver.h"

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

/** The buffer currently in use for caching data to program later into the flash array. */
static dib_pageProgramBuffer_t *BSS_OS(_pPgmInputBuf) = NULL;

/** The buffer currently being programmed into the flash array. */
static dib_pageProgramBuffer_t *SBSS_OS(_pPgmBuf) = NULL;

/** State variable: An erase command is in progress. */
static bool BSS_OS(_isBusyErasing) = false;

/** State variable: Using rom_osFlushProgramDataBuffer(), a half-way written input buffer \a
    _pPgmInputBuf has been submitted for programming. The next regular call of the main
    function will handle the flag. */
static bool SBSS_OS(_flushPgmInputBuf) = false;

/** Last error code received from underlaying flash ROM driver. Due to the asynchronous
    operation of HW and SW, this error code will be returned to the client code only with a
    delay of one action. */
#if ROM_TEST_BUILD_WITH_ERROR_INJECTION == 1
# warning Test build: Flash ROM driver owned variable rom_lastError is exposed to access by P1
rom_errorCode_t DATA_P1(rom_lastError) = rom_err_invalidErrorCode;
#else
static rom_errorCode_t DATA_OS(rom_lastError) = rom_err_invalidErrorCode;
#endif

/*
 * Function implementation
 */

/**
 * Store last error.\n
 *   This function should be called with the return value of the flash ROM driver API
 * calls. If the return value points to a problem then it is stored. The client code of the
 * flash ROM driver can query potentialls stored errors using rom_osFetchLastError().\n
 *   This way of reporting errors is required as in the flash ROM driver the HW operates
 * asynchronous to the SW API. The API calls can't directly and immediately return the
 * error codes.
 *   @param[in] apiReturnCode
 * The return code from an API call of the driver core (i.e., eap_xxx()), which is checked
 * for a fault status and stored in case.
 */
static void latchLastError(rom_errorCode_t apiReturnCode)
{
    /* All codes but noError and pending are reported error and they are stored if there is
       no other error stored yet. We don't overwrite an already stored error as it is
       probable that later errors are just a consequence of earlier ones. */
    if(rom_lastError == rom_err_noError  &&  apiReturnCode != rom_err_processPending)
        rom_lastError = apiReturnCode;

} /* latchLastError */


/**
 * Initialize the complete flash driver, including the sub-ordinated modules eap and dib.
 */
void rom_osInitFlashRomDriver(void)
{
    dib_osInitBufferManagement();
    eap_osInitFlashRomDriver();

    _isBusyErasing = false;
    _pPgmInputBuf = NULL;
    _pPgmBuf = NULL;
    _flushPgmInputBuf = false;
    rom_lastError = rom_err_noError;

} /* rom_osInitFlashRomDriver */


/**
 * Check if a memory address range is completely in the portion of the flash ROM, which is
 * managed by the FBL.
 *   @param[in] address
 * The first address of the memory area.
 *   @param[in] size
 * The length of the memory area in Byte.
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
    return address >= 0x00FC8000u  &&  endAddr <= 0x01580000u;

#elif defined(MCU_MPC5775B) || defined(MCU_MPC5775E)
# error Implement rom_isValidFlashAddressRange for MPC5775B/E
#elif defined(MCU_MPC5777C)
# error Implement rom_isValidFlashAddressRange for MPC5777C
#endif
} /* rom_isValidFlashAddressRange */


/**
 * Check the state of the flash driver with respect to flash erasure.\n
 *   The returned response check the state of the driver if a previous erase or program
 * command is still pending and needs to be completed before an intended next erasure may
 * begin.
 *   @return
 * Get \a true if the driver is idle; a new start earse command can be accepted. If \a
 * false is returned then a call of rom_osStartEraseFlashMemory() will probably fail.
 *   @remark
 * The function ignores half-way written program input buffers. If such a buffer exists,
 * then it's likely that the erasure of the flash will happen, before this buffer is
 * completed and released for programming. It would potentially written later into the
 * erased flash array. In case of doubts, call rom_osFlushProgramDataBuffer() prior to
 * checking the readiness for erase.
 */
bool rom_osReadyToStartErase(void)
{
    if(_isBusyErasing)
        return false;
    else
    {
        /* No erasure pending. We check if a pending program request may require waiting
           for completion. */
        return _pPgmBuf == NULL  && !_flushPgmInputBuf;
    }
} /* rom_osReadyToStartErase */


/**
 * Initiate erasure of a portion of the flash ROM.\n
 *   The demanded address range doesn't need to be identical with one or more contiguous
 * flash blocks. If this is not the case, then the command will erase more bytes then
 * specified. All flash blocks, which share at least one byte with the specified address
 * range, will be erased.\n
 *   The function just triggers the operation; on return the flash is not yet erased. Use
 * rom_osReadyToStartProgram() or rom_osReadyToStartErase() to see, when the operation is
 * completed to the extend needed to initiate the next program or erase command,
 * respectively.
 *   @return
 * Get \a true if the command could be started. If the driver is busy or if the address
 * range spans more bytes, which aren't in any of the supported flash blocks, then \a false
 * is returned.
 *   @param[in] address
 * The first address to be erased.
 *   @param[in] noBytes
 * The number of bytes to erase at \a address.
 */
bool rom_osStartEraseFlashMemory(uint32_t address, uint32_t noBytes)
{
    bool success = true;
    rom_errorCode_t errCode = rom_err_noError;
    
    if(success && !rom_isValidFlashAddressRange(address, noBytes))
    {
        success = false;
        errCode = rom_err_badAddressRange;
    }
    if(success && !rom_osReadyToStartErase())
    {
        success = false;
        errCode = rom_err_driverNotReady;
    }
    if(success)
    {
        errCode = eap_osStartEraseFlashBlocks(address, noBytes);
        success = _isBusyErasing = errCode == rom_err_processPending;
    }

    latchLastError(errCode);
    return success;

} /* rom_osStartEraseFlashMemory */


/**
 * Check the state of the flash driver with respect to flash programming.\n
 *   Note, the returned response relates only to the state of the driver, but doesn't check
 * all pre-conditions for programming. In particular, this function doesn't perform a blank
 * check of the flash array.
 *   @return
 * Get \a true if the flash array programming part of the driver is idle; a new start
 * program command can be accepted. If \a false is returned then a call of
 * rom_osStartProgram() will probably fail.
 */
bool rom_osReadyToStartProgram(void)
{
    /* The driver is surely free to accept the next write command if there are at least two
       input buffers left. (A single write can't span more than two flash quad pages.) No
       matter, if an erasure is potentially ongoing. */
    return dib_getNoFreeInputBuffers() >= 2u;
}


/**
 * Initiate programming a number of bytes.\n
 *   The target \a address is an arbitrary address, which points into a quad-page.
 * Buffering always holds zero or one quad-page. Prior to the first call of this function
 * or after a call of rom_osFlushProgramDataBuffer(), there is no page. Once using this
 * function, always the last used quad-page is held. If \a address points into the
 * currently held quad-page then the data of this page can be completed or modified. Once
 * an address points to another quad-page then the one held so far is considered fianlized
 * and it is submitted for programming. The other one becomes the held one.\n
 *   Bytes of a quad-page, which have not at all been written are set to 0xFF or, with
 * other words, they remain unprogrammed, when the page is evetually submitted for
 * programming.\n
 *   This pattern supports the most common use-case: Writing all bytes to program in
 * strictly rising address order. In this case no attention needs to be drawn to the
 * quad-page structure of the flash ROM array. No matter if first and last address are
 * quad-page boundaries, no matter if data is contiguous or if there are gaps in the
 * addresses, the result is always as expected. Only a call of
 * rom_osFlushProgramDataBuffer() is required after the very last call of this function to
 * ensure that the last held quad-page is still submitted for programming.\n
 *   Caution, if the function is called with addresses going up and down. This can easily
 * lead to quad-pages, which are submitted repeatedly for programming, and this will almost
 * certainly invalidate the quad-page after programming. ECC bits won't be set correct and
 * access to the quad-page after programming will cause exceptions. The only way out is
 * then the erasure of the complete flash block such a quad-page resides in.
 *   @return
 * Get \a true if all bytes were put into the input buffer. If \a false is returned then at
 * least one byte is lost and the overall flash programming process has failed.
 *   @param[in] address
 * The address in the flash ROM, where to program the first byte from array \a
 * pDataToProgram.
 *   @param[in] pDataToProgram
 * The bytes to program. The data is copied into some internal buffer space, so the data
 * needs to be valid only during the call of this function.
 *   @param[in] noBytes
 * The number of bytes to program. The allowed range is 1..(#EAP_C55FMC_SIZE_OF_QUAD_PAGE+1).
 */
bool rom_osStartProgram(uint32_t address, const uint8_t *pDataToProgram, uint32_t noBytes)
{
    /* We can check the preconditions under which we surely know, that we have sufficient
       buffer space for accepting the command. */
    if(rom_osReadyToStartProgram() && rom_isValidFlashAddressRange(address, noBytes))
    {
        if(_pPgmInputBuf == NULL)
        {
            _pPgmInputBuf = dib_osAcquireInputBuffer();
            assert(_pPgmInputBuf != NULL);
        }

        const uint32_t noBytesWritten = dib_osWriteDataIntoBuffer( _pPgmInputBuf
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
            dib_osReleaseBuffer(_pPgmInputBuf, /*submitForProgramming*/ true);

            /* Those bytes, which couldn't be written into the previous buffer are written
               into the next one. We know, that we will get another one because we had
               checked this as precondition for accepting the write job. */
            _pPgmInputBuf = dib_osAcquireInputBuffer();
            assert(_pPgmInputBuf != NULL);
#ifdef DEBUG
            const uint32_t noBytesWritten2nd =
#endif
            dib_osWriteDataIntoBuffer( _pPgmInputBuf
                                     , address + noBytesWritten
                                     , noBytes - noBytesWritten
                                     , pDataToProgram + noBytesWritten
                                     );

            /* The function is specified to allow at maximum EAP_C55FMC_SIZE_OF_QUAD_PAGE+1
               bytes to write at once. If this assertion fires then this pre-condition of
               the function call has been violated in a harmful, data loss-causing way.
               Violations of the pre-condition, which are harmless and don't impact proper
               functionality aren't reported:
                 Only EAP_C55FMC_SIZE_OF_QUAD_PAGE+1 will fit even under worst conditions.
                 Under worst conditions, the current buffer can't be used at all; it has
               another quad-page address. The first byte to write is the last one of the
               first acquired quad-page input buffer, so that the rest of the bytes will
               entirely fill the second acquired input buffer. Acquisition of more input
               buffers is imaginable but not guaranteed.
                 More bytes than EAP_C55FMC_SIZE_OF_QUAD_PAGE+1 will most often fit but
               there is no worst case guarantee. In particular, for the common use-case of
               strictly sequentially writing all bytes, even 2*EAP_C55FMC_SIZE_OF_QUAD_PAGE
               would always fit. */
            assert(noBytesWritten2nd + noBytesWritten == noBytes);
        }

        return true;
    }
    else
    {
        /* The data might still fit in the remaining buffer space, but we don't take the
           risk of getting stuck after copying the data partially. We reject the command. */
        return false;
    }
} /* rom_osStartProgram */


/**
 * All data, which has already been handed over to the flash ROM driver using
 * rom_osStartProgram() is submitted for programming. This implies that, if the last filled
 * quad-page is not completed yet, this quad-page is finalized. Not yet written bytes in
 * this page can't be written later any more.\n
 *   This operation needs to be used at the end of data transmission, after the very last
 * call of rom_osStartProgram(), in order to make all written data be programmed. (Instead of
 * infinitely waiting for possibly more bytes to go into the last recently begun
 * quad-page.)\n
 *   It is not required to call this function in case of address gaps in the programmed
 * data. If an address gap leads to writing to another quad-page even before the previous
 * one is completed then the flush operation is unconditionally done internally for the
 * left quad-page. However, an explicit call of this function would also not do any harm.
 */
void rom_osFlushProgramDataBuffer(void)
{
    if(_pPgmInputBuf != NULL)
        _flushPgmInputBuf = true;
    else
        assert(!_flushPgmInputBuf);

} /* rom_osFlushProgramDataBuffer */


/**
 * Get the last recently seen error in the flash ROM driver.\n
 *   In the flash ROM driver, the HW operates asynchronous to the public API. Therefore,
 * the public API calls can't directly and immediately return the error codes to the client
 * code. Instead, it'll regularly use this API to check if an error had been happened in
 * the meantime.\n
 *   Calling this API resets a stored error by side-effect. A second call won't return the
 * error again.
 *   @return
 * Get the last recently occured error or \a rom_err_noError, if no error happened since
 * the last call of this API.
 */
rom_errorCode_t rom_osFetchLastError(void)
{
    const rom_errorCode_t errCode = rom_lastError;
    rom_lastError = rom_err_noError;
    return errCode;

} /* rom_osFetchLastError */


/**
 * Main function of flash ROM driver. To be called regularly in order to keep the main
 * state machine running.
 */
void rom_osFlashRomDriverMain(void)
{
    /* If client code had requested to program the half-way filled input buffer then push it
       back into the pool of pending program data buffers. */
    if(_flushPgmInputBuf)
    {
        dib_osReleaseBuffer(_pPgmInputBuf, /*submitForProgramming*/ true);
        _pPgmInputBuf = NULL;
        _flushPgmInputBuf = false;
    }

    if(_isBusyErasing)
    {
        /* rom_osStartEraseFlashMemory() had initiated an erase command. Now, we have to
           check the underlying driver, if the operation has completed. */
        const rom_errorCode_t errCode = eap_osGetStatusEraseFlashBlocks();
        latchLastError(errCode);
        _isBusyErasing = errCode == rom_err_processPending;
    }
    if(!_isBusyErasing)
    {
        /* If we have a quad-page in programming mode, then check the driver status if
           programming of the quad-page has completed. */
        if(_pPgmBuf != NULL)
        {
            const rom_errorCode_t errCode = eap_osGetStatusProgramQuadPage();
            latchLastError(errCode);
            if(errCode != rom_err_processPending)
            {
                dib_osReleaseBuffer(_pPgmBuf, /*submitForProgramming*/ false);
                _pPgmBuf = NULL;
            }
        }

        /* If we were not busy or have just completed a quad-page then we check if more
           filled input buffers are waiting for programming of other quad-pages.
             Note, it is most important for the state machine that this happens still in
           the same invocation of main. As long as there are filled input buffers, _pPgmBuf
           must not become NULL outside this function call. So it's not an else of the
           preceding if. */
        while(_pPgmBuf == NULL)
        {
            /* Check for potential new flash jobs. */
            dib_pageProgramBuffer_t * const pPgmBuf = dib_osAcquireProgramBuffer();
            if(pPgmBuf != NULL)
            {
                const rom_errorCode_t errCode = eap_osStartProgramQuadPage
                                                            (dib_getBufferPayload(pPgmBuf));
                latchLastError(errCode);
                if(errCode == rom_err_processPending)
                {
                    /* We got a filled buffer and could hand it over to the driver for
                       programming. Done. */
                    _pPgmBuf = pPgmBuf;
                }
                else
                {
                    /* We got a filled buffer but the driver can't process it. It is
                       dropped and an error will be reported to the client code. */
                    dib_osReleaseBuffer(pPgmBuf, /*submitForProgramming*/ false);
                }
            }
            else
            {
                /* We are idle, but there is no further input (yet). No error, we are done
                   for now. */
                break;
            }
        } /* while(We are idle and the pool may contain another filled input buffer.) */

    } /* if(Waiting for an erase command to complete?) */

} /* rom_osFlashRomDriverMain */