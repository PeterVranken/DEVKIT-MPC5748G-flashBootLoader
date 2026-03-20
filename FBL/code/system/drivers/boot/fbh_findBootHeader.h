#ifndef FBH_FINDBOOTHEADER_INCLUDED
#define FBH_FINDBOOTHEADER_INCLUDED
/**
 * @file fbh_findBootHeader.h
 * Definition of global interface of module fbh_findBootHeader.c
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

/*
 * Defines
 */

/** An arbitrary magic value, which is expected at the end of the flashed application as
    kind of minimal prove for seeing a valid application in flash ROM. */
#define FBH_MAGIC_AT_APPLICATION_END    0x1F54DEB9u

/*
 * Global type definitions
 */


/*
 * Global data declarations
 */


/*
 * Global prototypes
 */

/* Search the flash ROM for a loaded, usable application. */
uint32_t fbh_findApplication(uint32_t * const pTiWaitForCcpInMs);

/*
 * Global inline functions
 */


#endif  /* FBH_FINDBOOTHEADER_INCLUDED */
