#ifndef ROM_FLASHROMDRIVER_INCLUDED
#define ROM_FLASHROMDRIVER_INCLUDED
/**
 * @file rom_flashRomDriver.h
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

#include "typ_types.h"

/** @todo Define either MCU_MPC5748G, MCU_MPC5775B or MCU_MPC5775E to select the MCU, which
    this module is compiled for. */
#define MCU_MPC5748G

/*
 * Defines
 */

/* Only for testing purpose, it is allowed to modify the global error variable from the
   user command line interface in the QM process. Never set this flag to 1 in a production
   build! */
#ifdef DEBUG
# define ROM_TEST_BUILD_WITH_ERROR_INJECTION 1
#else
# define ROM_TEST_BUILD_WITH_ERROR_INJECTION 0
#endif

/*
 * Global type definitions
 */

/** Error and status codes of the public API of the flash ROM driver. */
typedef enum 
{
    rom_err_noError,           /**< Operation succeeded without an error. */
    rom_err_badAddressRange,   /**< Operation rejected due to bad/invalid addresses. */
    rom_err_driverNotReady,    /**< A driver API is used in a state, when it is not allowed. */
    rom_err_quadPageNotBlank,  /**< Attempt to program a non-erased quad-page. */
    rom_err_processPending,    /**< Operation successfully initiated, is still ongoing. */
    rom_err_c55FmcErrorInPeg,  /**< C55FMC reported an error during flash array programming.*/
    rom_err_unexpectedHwState, /**< HW is in unexpected state, maybe due to bad API use. */
    rom_err_verifyFailed,      /**< Programming of a quad-page ended with a data error. */
    rom_err_invalidErrorCode,  /**< Unused error code, e.g., to initialize variables. */
    
} rom_errorCode_t;


/*
 * Global data declarations
 */

#if ROM_TEST_BUILD_WITH_ERROR_INJECTION == 1
/* Last error code. Is public only in DEBUG compilation and for testing purpose. */
extern rom_errorCode_t DATA_P1(rom_lastError);
#endif

/*
 * Global prototypes
 */

/* Initialize the complete flash driver, including the sub-ordinated modules eap and dib. */
void rom_osInitFlashRomDriver(void);

/* Check if a memory address range is completely in the the flash ROM. */
bool rom_isValidFlashAddressRange(uint32_t address, uint32_t size);

/* Check the state of the flash driver: Is it possible to erase? */
bool rom_osReadyToStartErase(void);

/* Initiate erasure of a portion of the flash ROM. */
bool rom_osStartEraseFlashMemory(uint32_t address, uint32_t noBytes);

/* Check the state of the flash driver: Is it possible to write programming data? */
bool rom_osReadyToStartProgram(void);

/* Initiate programming a number of bytes. */
bool rom_osStartProgram(uint32_t address, const uint8_t *pDataToProgram, uint32_t noBytes);
    
/* Force finalization and programming of partly written flash page. */
void rom_osFlushProgramDataBuffer(void);

/* Get the last recently seen error in the flash ROM driver. */
rom_errorCode_t rom_osFetchLastError(void);

/* Regularly called main function of driver. */
void rom_osFlashRomDriverMain(void);

/*
 * Global inline functions
 */


#endif  /* ROM_FLASHROMDRIVER_INCLUDED */
