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
#include <stddef.h>

#include "typ_types.h"
#include "rom_flashRomDriver.h"

/*
 * Defines
 */

/** The needed buffer size in Byte. It reflects the property of the flash array, how many
    bytes can be programmed at once, and must not be changed. */
#define EAP_C55FMC_SIZE_OF_QUAD_PAGE    128u

/** The bit mask, which to AND with an address in the middle of the flash ROM array in
    order to yield the address offset in the quad page, which that address is in. */
#define EAP_MASK_C55FMC_MASK_ADDR_IN_QUAD_PAGE ((EAP_C55FMC_SIZE_OF_QUAD_PAGE)-1u)

/** The bit mask, which to AND with an address in the middle of the flash ROM array in
    order to yield the address of the quad page, which that address is in. */
#define EAP_MASK_C55FMC_MASK_ADDR_OF_QUAD_PAGE (~EAP_MASK_C55FMC_MASK_ADDR_IN_QUAD_PAGE)

/** Get the address of the quad page, which a given address \a addr point into. */
#define GET_ADDR_OF_QUAD_PAGE(/*uint32_t*/ addr)    \
                                (((uint32_t)(addr)) & EAP_MASK_C55FMC_MASK_ADDR_OF_QUAD_PAGE)

/** Get the relative address (offset) in the quad page, which a given address \a addr point
    into. */
#define EAP_GET_ADDR_OFFS_IN_QUAD_PAGE(/*uint32_t*/ addr)   \
                                (((uint32_t)(addr)) & EAP_MASK_C55FMC_MASK_ADDR_IN_QUAD_PAGE)

/** Testing the integration of the flash ROM driver is supported by a simple mockup, which
    has the same API but which just emulates the true operation. Testing becomes possible
    without touching or modifying the flash ROM array. */
#define EAP_TEST_WITH_MOCKUP    0


/*
 * Global type definitions
 */

/** A data buffer for programming a quad-page. The base programming step, writing a quad
    page of 128 Byte, can be done with the information found in the buffer. */
typedef struct
{
    /** The address to program the buffer data at. It is the global address in the address
        space of the MCU and it considers the quad-page alignment constraint of the flash
        ROM array. */
    uint32_t address;

    /** The data in different word widths. */
    union
    {
        /** The data as 128 bytes. */
        uint8_t data_b[EAP_C55FMC_SIZE_OF_QUAD_PAGE];

        /** The data as 32 words. */
        uint32_t data_u32[EAP_C55FMC_SIZE_OF_QUAD_PAGE / 4u];

        /** The data as 16 double words. */
        uint64_t data_u64[EAP_C55FMC_SIZE_OF_QUAD_PAGE / 8u];
    };
} eap_quadPageProgramBuffer_t;

_Static_assert( offsetof(eap_quadPageProgramBuffer_t, data_b)
                == offsetof(eap_quadPageProgramBuffer_t, data_u32)
                &&  offsetof(eap_quadPageProgramBuffer_t, data_b)
                    == offsetof(eap_quadPageProgramBuffer_t, data_u64)
                &&  sizeoffield(eap_quadPageProgramBuffer_t, data_b)
                    == sizeoffield(eap_quadPageProgramBuffer_t, data_u32)
                &&  sizeoffield(eap_quadPageProgramBuffer_t, data_b)
                    == sizeoffield(eap_quadPageProgramBuffer_t, data_u64)
              , "Invalid data modelling"
              );

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
rom_errorCode_t eap_osStartProgramQuadPage(eap_quadPageProgramBuffer_t * const pPrgDataBuf);

/* Check the status of a programming operation. */
rom_errorCode_t eap_osGetStatusProgramQuadPage(void);

/*
 * Global inline functions
 */

#endif  /* EAP_ERASEANDPROGRAM_INCLUDED */
