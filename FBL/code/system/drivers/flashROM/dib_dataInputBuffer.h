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
#include "eap_eraseAndProgram.h"

/*
 * Defines
 */


/*
 * Global type definitions
 */

//struct dib_pageProgramBuffer_t;
typedef struct dib_pageProgramBuffer_t dib_pageProgramBuffer_t;

/*
 * Global data declarations
 */


/*
 * Global prototypes
 */

/* Initailize the module prior to use of any of its APIs. */
void dib_osInitBufferManagement(void);

/* Query the number of currently available input buffers. */
unsigned int dib_getNoFreeInputBuffers(void);

/* Get a buffer for writing input data. */
dib_pageProgramBuffer_t *dib_osAcquireInputBuffer(void);

/* Check for an address if it points into a given buffer. */
bool dib_osIsAddressInBuffer(dib_pageProgramBuffer_t *pBuf, uint32_t address);

/* Write some bytes into a buffer (which are intended for later programming). */
uint32_t dib_osWriteDataIntoBuffer( dib_pageProgramBuffer_t *pBuf
                                  , uint32_t address
                                  , uint32_t noBytes
                                  , const uint8_t dataAry[]
                                  );

/* Get a buffer for programming data. */
dib_pageProgramBuffer_t *dib_osAcquireProgramBuffer(void);

/* Get the data contents of a buffer. */
eap_quadPageProgramBuffer_t *dib_getBufferPayload(dib_pageProgramBuffer_t *pBuf);

/* Submit a buffer for programming. */
void dib_osReleaseBuffer(dib_pageProgramBuffer_t *pBuf, bool submitForProgramming);

/*
 * Global inline functions
 */

#endif  /* DIB_DATAINPUTBUFFER_INCLUDED */
