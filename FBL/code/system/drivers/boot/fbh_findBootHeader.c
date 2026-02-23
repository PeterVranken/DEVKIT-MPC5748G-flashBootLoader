/**
 * @file fbh_findBootHeader.c
 * Look for a boot header at the possible locations.
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
 *   fbh_findApplication
 * Local functions
 *   isAddrValid
 */

/*
 * Include files
 */

#include "fbh_findBootHeader.h"

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "rom_flashRomDriver.h"

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


/*
 * Function implementation
 */

/**
 * Check of a flash ROM address.
 *   @return
 * Get \a true is address looks valid and \a false if it is evidently worng.
 *   @param[in] addr
 * The address to validate.
 */
static inline bool isAddrValid(uint32_t addr)
{
    /* Alignment should be correct for uint32_t access and value should be in the range of
       the managed flash ROM. */
    return (addr & (sizeof(uint32_t)-1u)) == 0u
           &&  rom_isValidFlashAddressRange(addr, sizeof(uint32_t));

} /* isAddrValid */


/**
 * Search the flash ROM for a loaded, usable application.
 *   @return
 * Get the address of a found application or 0u, if no such application is flashed.
 */
uint32_t fbh_findApplication(void)
{
    /* The FBL can support only two of the original MPC5748G locations of the boot header,
       FD8000 and FE0000, which are in the managed part of the flash ROM. FC0000 needs to
       be hi-jacked by the FBL as it has a higher priority than the others and would knock
       out the FBL if ever used by an application. (Hi-jacking of FC0000 actually means
       loosing 32k of usable flash ROM. The FBL needs to place its own boot header there
       but cannot place any data or code in this block as it belongs the partition 0, which
       has other blocks in the managed part of the flash ROM.)
         There is no true advantage in offering more than one boot header location to
       application programming. It is not even required that the FBL expects boot headers
       at the same location as the BAF, the reset logic of the MPC5748G. We decide to look
       for a boot header at these locations:
         FC8000: The beginning of the managed flash ROM. If an application places its boot
       header here then it can use all remaining flash ROM as a single big contiguous chunl
       of memory.
         FD8000: One of the locations supported by the BAF. If an application places its
       boot header here then its compiled binary would work well with or without the FBL
       being flashed, too. The downside is, that the boot header now splits the total
       available flash ROM in two chunks.
         See RM 57.1.3, Table 57-3, p.2840, for the BAF supported boot header locations,
       and Table 57-4 for the binary built-up of the header.
         The last word of the original MPC5748G boot header definition, "reserved for
       futire use", is used by the FBL as pointer to the magic word, which is supposed to
       by loacted at the highest address in use by the application. Checking this word is
       the the most simple but still effective validation of the application code. Making
       the reasonable working assumption for a flash tool, to flash the application in
       order of rising addresses, this magic would be written as very last word and most
       typical errors during the flash process will make it not be found in the memories
       and the FBL will reject to start the fragments from a failing flash process. */
    const uint32_t addrBootHdrAry[] = {0x00FC8000u, 0x00FD8000u};

    #define MASK_KNOWN_HDR_CFG_BITS 0xFFFFFFF6u
    #define EXPECTED_HDR_CFG_BITS   0x005A0002u

    uint32_t startAddr = 0u;
    for(unsigned int u=0u; u<sizeOfAry(addrBootHdrAry); ++u)
    {
        const uint32_t addrBootHdr = addrBootHdrAry[u]
                     , bootHdrCfg = *(const uint32_t*)(addrBootHdr + 0x00u)
                     , addrMagic  = *(const uint32_t*)(addrBootHdr + 0x18u);
        if((bootHdrCfg & MASK_KNOWN_HDR_CFG_BITS) == EXPECTED_HDR_CFG_BITS
           &&  isAddrValid(addrMagic)
           &&  *(const uint32_t*)addrMagic == FBH_MAGIC_AT_APPLICATION_END
          )
        {
            const uint32_t addr = *(const uint32_t*)(addrBootHdr + 0x10u);
            if(isAddrValid(addr))
            {
                startAddr = addr;
                break;
            }
        }
    }

    #undef MASK_KNOWN_HDR_CFG_BITS
    #undef EXPECTED_HDR_CFG_BITS

    return startAddr;

} /* fbh_findApplication */