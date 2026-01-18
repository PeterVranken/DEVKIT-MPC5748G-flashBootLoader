#ifndef ROM_FLASHROM_INCLUDED
#define ROM_FLASHROM_INCLUDED
/**
 * @file rom_flashRom.h
 * Definition of global interface of module rom_flashRom.c
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


/*
 * Global type definitions
 */


/*
 * Global data declarations
 */


/*
 * Global prototypes
 */

/* Check the state of the flash driver. */
bool rom_isFlashDriverBusy(void);

/* Check if a memory address range is completely in the the flash ROM. */
bool rom_isValidFlashAddressRange(uint32_t address, uint32_t size);

/* Initiate erasure of a portion of the flash ROM. */
bool rom_startEraseFlashMemory(uint32_t address, uint32_t noBytes);

/* Initiate programming a number of bytes. */
bool rom_startProgram(uint32_t address, const uint8_t *pDataToProgram, uint32_t noBytes);
    
/* Regularly called main function of driver. */
void rom_flashRomMain(void);

/*
 * Global inline functions
 */


#endif  /* ROM_FLASHROM_INCLUDED */
