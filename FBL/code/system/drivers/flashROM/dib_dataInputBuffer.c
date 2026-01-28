/**
 * @file dib_dataInputBuffer.c
 * Input buffer for programmed data. To decouple the data providing channel from the
 * programming sequence of the flash ROM array, the driver uses a double input buffer
 * system. Unless the data transmission is faster then the programming, the providing
 * channel will always be able to store the delivered data and, consequently, the channel
 * can operate with maximum throughput.\n
 *   This module provides the buffers, the buffer handling and a flush operation for
 * programming even partly filled buffers.
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
 *   dib_isAddressInBuffer
 *   dib_getNoFreeInputBuffers
 *   dib_acquireInputBuffer
 *   dib_acquireProgramBuffer
 *   dib_releaseBuffer
 * Local functions
 *   releaseBuffer
 */

/*
 * Include files
 */

#include "dib_dataInputBuffer.h"

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "rom_flashRom.h"
#include "eap_eraseAndProgram.h"

/*
 * Defines
 */

/* Blocking free buffer filling (if programming is faster than filling) requires at least
   four buffers. */
#define NO_DATA_BUFFERS 4u

/*
 * Local type definitions
 */

/** A data input buffer for programming. The base programming step, writing a single page
    of a flash block, can be done with the information found in the buffer. */
struct dib_pageProgramBuffer_t
{
    /* This element contains the contents opf the quad page and its address. */
    eap_quadPageProgramBuffer_t pageBuf;

    /** The state of the buffer, like filling, completed, being programmed. */
    enum {dib_bufSt_free, dib_bufSt_empty, dib_bufSt_filling, dib_bufSt_toBePrgd, } state;

};


/*
 * Local prototypes
 */


/*
 * Data definitions
 */

/** Four alternatingly used input buffers. */
static dib_pageProgramBuffer_t _inputBufAry[NO_DATA_BUFFERS] =
{
    [0 ... sizeOfAry(_inputBufAry)-1] =
    {
        .pageBuf =
        {
            .address = 0u,
            .data_b =
            {
                [0 ... EAP_C55FMC_SIZE_OF_QUAD_PAGE-1] = 0u,
            },
        },
        .state = dib_bufSt_free,
    },
};

/** The number of currently free data buffers. */
static unsigned int _noFreeInputBufs = NO_DATA_BUFFERS;

/*
 * Function implementation
 */

/**
 * Return a buffer after filling it.
 *   @param[in] pBuf
 * The buffer by reference.
 *   @param[in] submitForProgramming
 * If \a true then the buffer is handeld over to the flash ROM driver for programming the
 * contents into the flash array. If \a false then the buffer is just back in the pool of
 * available, free buffers and all its contents are dropped without taking any effect.
 */
static inline void releaseBuffer( dib_pageProgramBuffer_t * const pBuf
                                , bool submitForProgramming
                                )
{
    if(submitForProgramming)
    {
        assert(pBuf->state == dib_bufSt_filling);
        pBuf->state = dib_bufSt_toBePrgd;
    }
    else
    {
        assert(pBuf->state == dib_bufSt_empty  ||  pBuf->state == dib_bufSt_filling
               ||  pBuf->state == dib_bufSt_toBePrgd
              );
        pBuf->state = dib_bufSt_free;
        ++ _noFreeInputBufs;
    }
} /* releaseBuffer */


/**
 * Query the number of currently available input buffers.
 *   @return
 * Get the count of available buffers.
 */
unsigned int dib_getNoFreeInputBuffers(void)
{
    return _noFreeInputBufs;
}


/**
 * Get a buffer for writing input data.\n
 *   After filling the buffer with data, the buffer needs to be released again using
 * dib_releaseBuffer().
 *   @return
 * Get the buffer by reference or NULL if there is currently no free buffer available.
 */
dib_pageProgramBuffer_t *dib_acquireInputBuffer(void)
{
    dib_pageProgramBuffer_t *pBuf = NULL;

    for(unsigned int u=0u; u<NO_DATA_BUFFERS; ++u)
        if(_inputBufAry[u].state == dib_bufSt_free)
            pBuf = &_inputBufAry[u];

    if(pBuf != NULL)
    {
        memset(&pBuf->pageBuf.data_b[0], 0xFF, sizeof(pBuf->pageBuf.data_b));
        pBuf->state = dib_bufSt_empty;
#ifdef DEBUG
        pBuf->pageBuf.address = 0u;
#endif

        -- _noFreeInputBufs;
        assert(_noFreeInputBufs <= NO_DATA_BUFFERS);
    }

    return pBuf;

} /* dib_getInputBuffer */


/**
 * Get the data contents of a buffer.
 */
eap_quadPageProgramBuffer_t *dib_getBufferPayload(dib_pageProgramBuffer_t *pBuf)
{
    return &pBuf->pageBuf;
}


/**
 * Check for an address if it points into a given buffer.
 *   @return
 * Get \a true if \a address is in the buffer space.
 */
bool dib_isAddressInBuffer(dib_pageProgramBuffer_t * const pBuf, uint32_t address)
{
    assert(rom_isValidFlashAddressRange(address, /*noBytes*/ 1));
    assert(pBuf->state != dib_bufSt_free  &&  pBuf->state != dib_bufSt_toBePrgd);
    if(pBuf->state == dib_bufSt_empty)
    {
        /* Buffer is not yet decided for a particular address. It will always match. */
        assert(pBuf->pageBuf.address == 0u);
        pBuf->pageBuf.address = GET_ADDR_OF_QUAD_PAGE(address);
        return true;
    }
    else
    {
        /* Buffer is in (filling) use. We need to compare the address. */
        return pBuf->pageBuf.address == GET_ADDR_OF_QUAD_PAGE(address);
    }
} /* dib_isAddressInBuffer */


/* Write some bytes into a buffer (which are intended for later programming). */
uint32_t dib_writeDataIntoBuffer( dib_pageProgramBuffer_t * const pBuf
                                , uint32_t address
                                , uint32_t noBytes
                                , const uint8_t dataAry[]
                                )
{
    assert(rom_isValidFlashAddressRange(address, noBytes));

    /* Set the buffer address on first write to the buffer. The buffer address is the
       address of the flash page, not the data address. */
    if(pBuf->state == dib_bufSt_empty)
    {
        assert(pBuf->pageBuf.address == 0u);
        pBuf->pageBuf.address = GET_ADDR_OF_QUAD_PAGE(address);
        pBuf->state = dib_bufSt_filling;
    }

    const uint32_t endAddrPage = pBuf->pageBuf.address + EAP_C55FMC_SIZE_OF_QUAD_PAGE;

    /* Check, how many bytes from the input fit into the quad-page, which is represented
       by the buffer. */
    uint32_t noBytesToCopy;
    if(GET_ADDR_OF_QUAD_PAGE(address) == pBuf->pageBuf.address)
    {
        /* The address points into the buffer's quad-page. At least one byte will fit. */
        if(address + noBytes > endAddrPage)
            noBytesToCopy = endAddrPage - address;
        else
            noBytesToCopy = noBytes;
        assert(noBytesToCopy > 0  &&  noBytesToCopy <= noBytes);

        /* Address as offset to beginning of quad page. */
        const uint32_t addrOffset = EAP_GET_ADDR_OFFS_IN_QUAD_PAGE(address);
        assert(addrOffset < sizeof(pBuf->pageBuf.data_b));

        /* Copy those bytes, which fit into the page. */
        memcpy(&pBuf->pageBuf.data_b[addrOffset], dataAry, noBytesToCopy);
    }
    else
    {
        /* The target address is unrelated to the given buffer. Nothing to be done. */
        noBytesToCopy = 0u;
    }

    return noBytesToCopy;

} /* dib_writeDataIntoBuffer */


/**
 * Get a buffer for programming into the flash array.\n
 *   The buffer pool is searched for a buffer, which had been filled with data before.\n
 *   After programming the contained data, the buffer needs to be released again using
 * dib_releaseBuffer().
 *   @return
 * Get such a buffer or NULL if no filled buffer is ready for programming.
 */
dib_pageProgramBuffer_t *dib_acquireProgramBuffer(void)
{
    dib_pageProgramBuffer_t *pBuf = NULL;

    for(unsigned int u=0u; u<NO_DATA_BUFFERS; ++u)
        if(_inputBufAry[u].state == dib_bufSt_toBePrgd)
            pBuf = &_inputBufAry[u];

    return pBuf;

} /* dib_acquireProgramBuffer */


/**
 * Return a buffer after use.
 *   @param[in] pBuf
 * The buffer, which had before been acquired with either dib_acquireInputBuffer() or
 * dib_acquireProgramBuffer().
 *   @param[in] submitForProgramming
 * If the buffer had been acquired for filling then this flag is now set to \a true. This
 * hands the buffer over to the flash ROM driver core for programming.\n
 *   If it should be discarded or if it had been acquired for programming, then pass \a
 * false.
 */
void dib_releaseBuffer(dib_pageProgramBuffer_t * const pBuf, bool submitForProgramming)
{
    releaseBuffer(pBuf, submitForProgramming);

} /* dib_releaseBuffer */

