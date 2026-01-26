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

/*
 * Defines
 */


/*
 * Global type definitions
 */

// TODO Naming wrong and values bad
typedef enum 
{
    STATUS_SUCCESS,
    STATUS_INVALID,
    STATUS_FLASH_ERR_UNEXPECTED_STATE,
    STATUS_FLASH_IN_PROGRESS,
    STATUS_ERROR_IN_PGM,
    STATUS_BUSY,
} status_t;

/*
 * Global data declarations
 */


/*
 * Global prototypes
 */

/* Start the programming of a single quad-page in the C55 controller. */
status_t eap_startProgramQuadPage(dib_pageProgramBuffer_t * const pPrgDataBuf);

/* Check the status of a programming operation. */
status_t eap_getStatusProgramQuadPage(void);

bool eap_firstTest(void);

/*
 * Global inline functions
 */


#endif  /* EAP_ERASEANDPROGRAM_INCLUDED */
