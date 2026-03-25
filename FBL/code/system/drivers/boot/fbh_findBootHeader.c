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
 *   @param[out] pTiWaitForCcpInMs
 * If the function finds a boot header and doesn't return 0, then * \a pTiWaitForCcpInMs
 * contains the application requested time span, the FBL should wait for a potential CCP
 * CONNECT command after reset and before launching the application.\n
 *   Letting the FBL snooping for a possible CCP CONNECT after every reset is the easiest
 * way to allow re-programming the target device. However, it means an artificial
 * prolongation of the device's reset time and this is mostly not wanted. Therefore, it is
 * considered an application setting, whether to do and how long to wait. The application
 * can set the value as it suits it. A value of zero would mean not at all to look for a
 * CCP CONNECT.
 */
uint32_t fbh_findApplication(uint32_t * const pTiWaitForCcpInMs)
{
#if defined(MCU_MPC5748G)
    /* The FBL can support only two of the original MPC5748G locations of the boot header,
       FD8000 and FE0000, which are in the managed part of the flash ROM. FC0000 needs to
       be hi-jacked by the FBL as it has a higher priority than the others and would knock
       out the FBL if ever used by an application. (Hi-jacking of FC0000 actually means
       loosing 32k of usable flash ROM. The FBL needs to place its own boot header there
       but cannot place any data or code in this block as it belongs to partition 0, which
       has other blocks in the managed part of the flash ROM.)
         There is no true advantage in offering more than one boot header location to
       application programming. It is not even required that the FBL expects boot headers
       at the same location as the BAF, the reset logic of the MPC5748G. We decide to look
       for a boot header at these locations:
         FC8000: The beginning of the managed flash ROM. If an application places its boot
       header here then it can use all remaining flash ROM as a single big contiguous chunk
       of memory.
         FD8000: One of the locations supported by the BAF. If an application places its
       boot header here then its compiled binary would work well with or without the FBL
       being flashed. The downside is, that the boot header now splits the total available
       flash ROM in two separate areas.
         See RM48 57.1.3, Table 57-3, p.2840, for the BAF supported boot header locations,
       and Table 57-4 for the binary built-up of the header.
         The two words of the original MPC5748G boot header definition, "Configuration
       Bits" at offsets 0x8 and 0xC, which are not used by the BAF, are used by the FBL: 
         0x8: A time designation in Milliseconds. The flashed application can configure,
       how long the FBL waits for a potential CCP CONECT command after reset and before it
       launches the application. (A value of zero means not at all to check for incoming CCP
       CAN traffic but to immediately launch the application.)
         0xC: A pointer to the magic word, which is supposed to be located at the highest
       address in use by the application. Checking this word is the most simple but still
       effective validation of the application code. Making the reasonable working
       assumption for a flash tool, to flash the application in order of rising addresses,
       this magic would be written as very last word and most typical errors during the
       flash process will make it not be found in the memories and the FBL will reject to
       start the fragments from a failing flash process. */
    const uint32_t addrBootHdrAry[] = {0x00FC8000u, 0x00FD8000u};
    
    #define OFFS_HDR_CFG                    0
    #define OFFS_ADDR_APPL                  4
    #define OFFS_TI_WAIT_FOR_CCP_CONNECT    2
    #define OFFS_PTR_TO_MAGIC               3
    #define MASK_KNOWN_HDR_CFG_BITS         0xFFFFFFF6u
    #define EXPECTED_HDR_CFG_BITS           0x005A0002u

#elif defined(MCU_MPC5775B) || defined(MCU_MPC5775E)

    /* For the MPC5775B/E the contiguous flash ROM begins at 0x00800000 and this is at the
       same time the only location of a BAM supported boot header in the managed flash ROM
       address space. Unfortunately, this boot header has the highest priority and the FBL
       must not permit to flash data at that address. Different to the MPC5748G, we can
       block only the few bytes of the boot header but offer the rest of the flash block
       for normal application code. By blocking only a few bytes it is ensured that the
       application can't have a BAM recognized boot header and that it won't overrule the
       FBL.
         Different to the MPC5748G, there is no use case for having more than one FBL
       recognized boot header. We place the only supported one at the beginning of the
       managed flash ROM (considering the few blocked bytes). */
    const uint32_t addrBootHdrAry[] = {0x00800010u};
    
    #define OFFS_HDR_CFG                    0
    #define OFFS_ADDR_APPL                  1
    #define OFFS_TI_WAIT_FOR_CCP_CONNECT    2
    #define OFFS_PTR_TO_MAGIC               3
    #define EXPECTED_HDR_CFG_BITS           0xADeafBee
    #define EXPECTED_HDR_CFG_BITS           0xFFFFFFFFu
    
#elif defined(MCU_MPC5777C)
# error Implement fbh_findApplication for MPC5777C
#endif

    /* The maximum supported time, the FBL will wait for potential CCP CONNECT after
       reset. */
    #define TI_MAX_WAIT_FOR_CCP_IN_MS   60000u

    uint32_t startAddr = 0u;
    for(unsigned int u=0u; u<sizeOfAry(addrBootHdrAry); ++u)
    {
        const uint32_t * const pBootHdr = (const uint32_t*)addrBootHdrAry[u]
                     , bootHdrCfg       = pBootHdr[OFFS_HDR_CFG]
                     , addrAppl         = pBootHdr[OFFS_ADDR_APPL]
                     , tiWaitForCcpInMs = pBootHdr[OFFS_TI_WAIT_FOR_CCP_CONNECT]
                     , addrMagic        = pBootHdr[OFFS_PTR_TO_MAGIC];
                     
        if((bootHdrCfg & MASK_KNOWN_HDR_CFG_BITS) == EXPECTED_HDR_CFG_BITS
           &&  isAddrValid(addrAppl)
           &&  tiWaitForCcpInMs <= TI_MAX_WAIT_FOR_CCP_IN_MS
           &&  isAddrValid(addrMagic)
           &&  *(const uint32_t*)addrMagic == FBH_MAGIC_AT_APPLICATION_END
          )
        {
            startAddr = addrAppl;
            *pTiWaitForCcpInMs = tiWaitForCcpInMs;
            break;
        }
    }

    #undef OFFS_HDR_CFG
    #undef OFFS_ADDR_APPL
    #undef OFFS_TI_WAIT_FOR_CCP_CONNECT
    #undef OFFS_PTR_TO_MAGIC
    #undef MASK_KNOWN_HDR_CFG_BITS
    #undef EXPECTED_HDR_CFG_BITS
    #undef TI_MAX_WAIT_FOR_CCP_IN_MS

    return startAddr;

} /* fbh_findApplication */