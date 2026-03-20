#ifndef SWR_SOFTWARERESET_INCLUDED
#define SWR_SOFTWARERESET_INCLUDED
/**
 * @file swr_softwareReset.h
 * Definition of global interface of module swr_softwareReset.c
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

/** @remark Note, this file is shared with the assembly code. You must not use any
    constructs in the definition of the macros, which were not compatible with the GNU
    assembly language, e.g. number literals like 10u instead of 10. Everything else needs
    to protected against being read by the assembler by a preprocessor guard expression. */

/*
 * Include files
 */

#ifdef __STDC_VERSION__
# include <stdint.h>
# include <stdbool.h>
#endif

/*
 * Defines
 */

/** A magic value, which is used at the first 4 Byte RAM address to indicate that the
    application or FBL has left some information in RAM prior to triggering a reset. */
#define SWR_MAGIC_IS_BOOT_FLAG_VALID    0xDEADBEEF

/** A magic value, which is used at the second 4 Byte RAM address to indicate that the
    application or FBL requests launching the FBL after reset. */
#define SWR_BOOT_FLAG_START_FBL         0xADEAFBEE

/** A magic value, which is used at the second 4 Byte RAM address to indicate that the
    application or FBL requests launching a flashed application after reset. */
#define SWR_BOOT_FLAG_START_APP         0x31415927

#ifdef __STDC_VERSION__
/*
 * Global type definitions
 */


/*
 * Global data declarations
 */


/*
 * Global prototypes
 */

/* Start the countdown till a functional reset. */
void swr_osSoftwareReset(uint32_t bootFlag);

/*
 * Global inline functions
 */

#endif  /* For C code only */
#endif  /* SWR_SOFTWARERESET_INCLUDED */
