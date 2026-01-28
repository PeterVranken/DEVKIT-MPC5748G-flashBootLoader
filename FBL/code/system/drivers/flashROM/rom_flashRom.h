#ifndef ROM_FLASHROM_INCLUDED
#define ROM_FLASHROM_INCLUDED
/**
 * @file rom_flashRom.h
 * Definition of public interface of flash ROM driver.
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

/** @todo Define either MCU_MPC5748G, MCU_MPC5775B or MCU_MPC5775E to select the MCU, which
    this module is compiled for. */
#define MCU_MPC5748G

/*
 * Defines
 */


/*
 * Global type definitions
 */

/** Error and status codes of the public API of the flash ROM driver. */
typedef enum 
{
    rom_err_noError,            /**< Operation succeeded without an error. */
    rom_err_badAddressRange,    /**< Operation rejected due to bad/invalid addresses. */
    rom_err_quadPageNotBlank,   /**< Attempt to program a non-erased quad-page. */
    rom_err_processPending,     /**< Operation successfully initiated, is still ongoing. */
    rom_err_c55FmcErrorInPeg,   /**< C55FMC reported an error during flash array programming.*/
    rom_err_unexpectedHwState,  /**< HW is in unexpected state, maybe due to bad API use. */
    rom_err_verifyFailed,       /**< Programming of a quad-page ended with a data error. */
    rom_err_invalidErrorCode,   /**< Unused error code, e.g., to initialize variables. */
    
} rom_errorCode_t;


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
    
/* Force finalization and programming of partly written flash page. */
void rom_flushProgramDataBuffer(void);

/* Regularly called main function of driver. */
void rom_flashRomMain(void);

/*
 * Global inline functions
 */


#endif  /* ROM_FLASHROM_INCLUDED */
