/**
 * @file rom_flashRom.c
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
 *   rom_isFlashDriverBusy
 *   rom_isValidFlashAddressRange
 *   rom_startEraseFlashMemory
 * Local functions
 */

/*
 * Include files
 */

#include "rom_flashRom.h"

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>


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
 
static unsigned int _tiBusy = 0u; 

/*
 * Function implementation
 */

/**
 * Check the state of the flash driver.\n
 *   @return
 * Get \a false if the driver is idle; a new erase or program command can be accepted. If
 * \a false is returned then a new erase or program command will probably fail.
 */
bool rom_isFlashDriverBusy(void)
{
    return _tiBusy > 0u;
}


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
    const uint32_t endAddr = address + size;

    /* We can handle the overflow at the end of the ROM very easily, because the very last
       address in the address space is in no way manageable flash ROM. Caution, this might
       be different on other devices. */
    if(endAddr < address)
        return false;

    return address >= 0x00FA0000u  &&  endAddr <= 0x01580000u;

} /* rom_isValidFlashAddressRange */


/**
 * Initiate erasure of a portion of the flash ROM.\n
 *   The demanded address range doesn't need to be identical with one or more contiguous
 * flash blocks. If this is not the case, then the command will erase more bytes then
 * specified. All flash blocks, which share at least one byte with the specified address
 * range, will be erased.\n
 *   The function just triggers the operation; on return the flash is not yet erased. Use
 * rom_isFlashDriverBusy() to see, when the operation is completed.
 *   @return
 * Get \a true if the command could be started. If the driver is busy or if the address
 * range spans more bytes, which aren't in any of the supported flash blocks, then \a false
 * is returned.
 *   @param address
 * The first address to be erased.
 *   @param noBytes
 * The number of bates to erase at \a address.
 */
bool rom_startEraseFlashMemory(uint32_t address, uint32_t noBytes)
{
    if(_tiBusy == 0u  && rom_isValidFlashAddressRange(address, noBytes))
    {
        /* Scaling such that erasing all 4MB takes 10s. */
        _tiBusy = (uint32_t)(5000ull * (uint64_t)noBytes / 2097152ull);
        
        return true;
    }
    else
        return false;
}


void rom_flashRomMain(void)
{
    if(_tiBusy > 0u)
        -- _tiBusy;
}