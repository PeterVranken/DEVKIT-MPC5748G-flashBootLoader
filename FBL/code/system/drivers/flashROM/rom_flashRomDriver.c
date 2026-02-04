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
 *   rom_osFlashRomDriverMain
 * Local functions
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

/*
 * Function implementation
 */

/**
 * Initialize the complete flash driver, including the sub-ordinated modules eap and dib.
 */
void rom_osInitFlashRomDriver(void)
{
#warning Implement dib_osInitDataInputBuffer
//    dib_osInitDataInputBuffer();
    eap_osInitFlashRomDriver();
    
    _isBusyErasing = false;
    _pPgmInputBuf = NULL;
    _pPgmBuf = NULL;
    _flushPgmInputBuf = false;

} /* rom_osInitFlashRomDriver */


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
    return address >= 0x00FC0000u  &&  endAddr <= 0x01580000u;

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
    if(rom_osReadyToStartErase() && rom_isValidFlashAddressRange(address, noBytes))
    {
        /* End address: Overflow protection sits in rom_isValidFlashAddressRange(). */
        _isBusyErasing = eap_osStartEraseFlashBlocks(address, noBytes) 
                         == rom_err_processPending;
        return _isBusyErasing;
    }
    else
        return false;

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
 * Initiate programming a number of bytes.
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
 * The number of bytes to program. The allowed range is 1..#EAP_C55FMC_SIZE_OF_QUAD_PAGE.
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
        // TODO Return programming error back (to next start request).
        _isBusyErasing = eap_osGetStatusEraseFlashBlocks() == rom_err_processPending;
    }
    else
    {
        /* If we have a quad-page in programming mode, then check the driver status if
           programming of the quad-page has completed. */
        if(_pPgmBuf != NULL)
        {
            if(eap_osGetStatusProgramQuadPage() != rom_err_processPending)
            {
                dib_osReleaseBuffer(_pPgmBuf, /*submitForProgramming*/ false);
                // TODO Return programming error back (to next start request).
                _pPgmBuf = NULL;
            }
        }

        /* If we were not busy or have just completed a quad-page then we check if more
           filled input buffers are waiting for programming of other quad-pages.
             Note, it is most important for the state machine that this happens still in
           the same invocation of main. As long as there are filled input buffers, _pPgmBuf
           must not become NULL outside this function call. So it's not an else of the
           previous if. */
        while(_pPgmBuf == NULL)
        {
            /* Check for potential new flash jobs. */
            dib_pageProgramBuffer_t * const pPgmBuf = dib_osAcquireProgramBuffer();
            if(pPgmBuf != NULL)
            {
                if(eap_osStartProgramQuadPage(dib_getBufferPayload(pPgmBuf))
                   == rom_err_processPending
                  )
                {
                    /* We got a filled buffer and could hand it over to the driver for
                       programming. Done. */
                    _pPgmBuf = pPgmBuf;
                }
                else
                {
                    /* We got a filled buffer but the driver can't process it. It is
                       dropped and an error will be reported to the client code. */
                    // TODO Return programming error back (to next start request).
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
    

// This code could still be valid for testing state machine without actually flashing
//    /* Emulation of programming. */
//    #define DELAY_PER_ROW   20u
//    static unsigned int SDATA_OS(noBytesLeft_);
//    static uint32_t SDATA_OS(wrAddr_);
//    static const uint8_t * SDATA_OS(pData_);
//    static unsigned int SDATA_OS(cntDelay_);
//
//    if(_pPgmBuf == NULL)
//    {
//        /* Check for potential new flash jobs. */
//        _pPgmBuf = dib_osAcquireProgramBuffer();
//
//        if(_pPgmBuf != NULL)
//        {
//            eap_quadPageProgramBuffer_t * const pPageBuf = dib_getBufferPayload(_pPgmBuf);
//            noBytesLeft_ = EAP_C55FMC_SIZE_OF_QUAD_PAGE;
//            wrAddr_ = pPageBuf->address;
//            pData_ = &pPageBuf->data_b[0];
//            cntDelay_ = DELAY_PER_ROW;
//        }
//    }
//    if(_pPgmBuf != NULL)
//    {
//#if 0 /* This path: Emulate a programming, which is slower than data transmission and which
//         slows down the CCP communication. */
//
//        if(cntDelay_ > 0u)
//        {
//            -- cntDelay_;
//        }
//        else if(noBytesLeft_ > 0u)
//        {
//            iprintf( "P: %06lX  %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\r\n"
//                   , wrAddr_
//                   , (unsigned)pData_[ 0]
//                   , (unsigned)pData_[ 1]
//                   , (unsigned)pData_[ 2]
//                   , (unsigned)pData_[ 3]
//                   , (unsigned)pData_[ 4]
//                   , (unsigned)pData_[ 5]
//                   , (unsigned)pData_[ 6]
//                   , (unsigned)pData_[ 7]
//                   , (unsigned)pData_[ 8]
//                   , (unsigned)pData_[ 9]
//                   , (unsigned)pData_[10]
//                   , (unsigned)pData_[11]
//                   , (unsigned)pData_[12]
//                   , (unsigned)pData_[13]
//                   , (unsigned)pData_[14]
//                   , (unsigned)pData_[15]
//                   );
//            wrAddr_ += 16u;
//            pData_ += 16u;
//            noBytesLeft_ -= 16;
//
//            if(noBytesLeft_ > 0u)
//                cntDelay_ = DELAY_PER_ROW;
//        }
//        else
//        {
//            dib_osReleaseBuffer(_pPgmBuf, /*submitForProgramming*/ false);
//            _pPgmBuf = NULL;
//        }
//        
//#else /* This path: Emulate programming in realistic speed. Measurements show a programming
//         time of 0.3ms per quad-page: We will be ready already in the next 1ms-clock-tick.
//         CCP communication limits the speed, not programming. */
//        dib_osReleaseBuffer(_pPgmBuf, /*submitForProgramming*/ false);
//        _pPgmBuf = NULL;
//#endif
//    }


///** Return true if state machine requires continued calling for completing the process. */
//bool eap_firstTest(bool start)
//{
//    static enum {idle, init, error, waitForErase, program, success, } state_ SECTION(.sdata.OS.var) = idle;
//
//    if(start)
//    {
//        assert(state_ == idle  ||  state_ == success);
//        state_ = init;
//    }
//    else
//        assert(state_ == waitForErase  ||  state_ == program);
//
//    if(state_ == init)
//    {
//        eap_cntFsm = 0u;
//        eap_prgDataBuf.address = 0xFC0100u;
//        static uint8_t SDATA_OS(startVal_) = 1u;
//        for(unsigned int u=0u; u<EAP_C55FMC_SIZE_OF_QUAD_PAGE; ++u)
//            eap_prgDataBuf.data_b[u] = (startVal_ + u) & 0xFFu;
//        ++ startVal_;
//
//        eap_resultStartErase = eap_osStartEraseFlashBlocks( eap_prgDataBuf.address
//                                                          , EAP_C55FMC_SIZE_OF_QUAD_PAGE
//                                                          );
//        if(eap_resultStartErase == rom_err_processPending)
//        {
//            eap_noWaitCyclesErase = 0u;
//            state_ = waitForErase;
//        }
//        else
//            state_ = error;
//    }
//    else if(state_ == waitForErase)
//    {
//        ++ eap_noWaitCyclesErase;
//        eap_resultGetStatusErase = eap_osGetStatusEraseFlashBlocks();
//
//        if(eap_resultGetStatusErase == rom_err_noError)
//            state_ = program;
//        else if(eap_resultGetStatusErase != rom_err_processPending)
//            state_ = error;
//    }
//    else if(state_ == program)
//    {
//        eap_tiPgmQuadPageIn12p5ns = stm_osGetSystemTime(/*idxTimer*/ 0u);
//        eap_resultStartPgm = eap_osStartProgramQuadPage(&eap_prgDataBuf);
//
//        if(eap_resultStartPgm == rom_err_processPending)
//        {
//            eap_noWaitCyclesPgm = 0u;
//            while(true)
//            {
//                eap_resultGetStatusPgm = eap_osGetStatusProgramQuadPage();
//                if(eap_resultGetStatusPgm == rom_err_processPending)
//                    ++ eap_noWaitCyclesPgm;
//                else
//                {
//                    if(eap_resultGetStatusPgm == rom_err_noError)
//                        state_ = success;
//                    else
//                        state_ = error;
//
//                    break;
//                }
//            }
//            eap_tiPgmQuadPageIn12p5ns = stm_osGetSystemTime(/*idxTimer*/ 0u)
//                                        - eap_tiPgmQuadPageIn12p5ns;
//        }
//        else
//        {
//            state_ = error;
//            eap_tiPgmQuadPageIn12p5ns = stm_osGetSystemTime(/*idxTimer*/ 0u)
//                                        - eap_tiPgmQuadPageIn12p5ns;
//        }
//    }
//
//    return state_ != error  &&  state_ != success;
//}
