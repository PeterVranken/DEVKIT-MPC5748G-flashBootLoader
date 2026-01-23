#ifndef DIB_DATAINPUTBUFFER_INCLUDED
#define DIB_DATAINPUTBUFFER_INCLUDED
/**
 * @file dib_dataInputBuffer.h
 * Definition of global interface of module dib_dataInputBuffer.c
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

/*
 * Include files
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "typ_types.h"

/*
 * Defines
 */

/** The needed buffer size in Byte. It reflects the property of the flash array, how many
    bytes can be programmed at once, and must not be changed. */
#define DIB_C55FMC_SIZE_OF_QUAD_PAGE    128u


/*
 * Global type definitions
 */

/** A data input buffer for programming. The base programming step, writing a quad page of
    128, can be done with the information found in the buffer. */
typedef struct dib_pageProgramBuffer_t
{
    /** The address to program the buffer data at. It is the global address in the address
        space of the MCU and it considers the quad-page alignment constraint of the flash
        ROM array. */
    uint32_t address;

    /** The data in different word widths. */
    union
    {
        /** The data as 128 bytes. */
        uint8_t data_b[DIB_C55FMC_SIZE_OF_QUAD_PAGE];

        /** The data as 8*4 words. */
        uint32_t data_u32[DIB_C55FMC_SIZE_OF_QUAD_PAGE / 4u];
    };

    /** The state of the buffer, like filling, completed, being programmed. */
    enum {dib_bufSt_free, dib_bufSt_empty, dib_bufSt_filling, dib_bufSt_toBePrgd, } state;

} dib_pageProgramBuffer_t;

_Static_assert( offsetof(dib_pageProgramBuffer_t, data_b)
                == offsetof(dib_pageProgramBuffer_t, data_u32)
                &&  sizeoffield(dib_pageProgramBuffer_t, data_b)
                    == sizeoffield(dib_pageProgramBuffer_t, data_u32)
              , "Invalid data modelling"
              );

/*
 * Global data declarations
 */


/*
 * Global prototypes
 */

/* Query the number of currently available input buffers. */
unsigned int dib_getNoFreeInputBuffers(void);

/* Get a buffer for writing input data. */
dib_pageProgramBuffer_t *dib_acquireInputBuffer(void);

/* Check for an address if it points into a given buffer. */
bool dib_isAddressInBuffer(dib_pageProgramBuffer_t *pBuf, uint32_t address);

/* Write some bytes into a buffer (which are intended for later programming). */
uint32_t dib_writeDataIntoBuffer( dib_pageProgramBuffer_t *pBuf
                                , uint32_t address
                                , uint32_t noBytes
                                , const uint8_t dataAry[]
                                );

/* Get a buffer for programming data. */
dib_pageProgramBuffer_t *dib_acquireProgramBuffer(void);

/* Submit a buffer for programming. */
void dib_releaseBuffer(dib_pageProgramBuffer_t *pBuf, bool submitForProgramming);

/*
 * Global inline functions
 */

#endif  /* DIB_DATAINPUTBUFFER_INCLUDED */
