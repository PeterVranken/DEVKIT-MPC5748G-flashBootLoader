#ifndef EAP_ERASEANDPROGRAM_INCLUDED
#define EAP_ERASEANDPROGRAM_INCLUDED
/**
 * @file eap_eraseAndProgram.h
 * Definition of global interface of module eap_eraseAndProgram.c
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

#include "dib_dataInputBuffer.h"
#include "rom_flashRom.h"

/*
 * Defines
 */


/*
 * Global type definitions
 */

/*
 * Global data declarations
 */


/*
 * Global prototypes
 */

/* Initialize the flash ROM driver. */
void eap_osInitFlashRomDriver(void);

/* Start the erasure of one or more flash blocks in the C55 controller. */
rom_errorCode_t eap_osStartEraseFlashBlocks(uint32_t addressFrom, uint32_t addressTo);

/* Check the status of an erase operation. */
rom_errorCode_t eap_osGetStatusEraseFlashBlocks(void);

/* Start the programming of a single quad-page in the C55 controller. */
rom_errorCode_t eap_osStartProgramQuadPage(dib_pageProgramBuffer_t * const pPrgDataBuf);

/* Check the status of a programming operation. */
rom_errorCode_t eap_osGetStatusProgramQuadPage(void);

bool eap_firstTest(bool start);

/*
 * Global inline functions
 */


#endif  /* EAP_ERASEANDPROGRAM_INCLUDED */
