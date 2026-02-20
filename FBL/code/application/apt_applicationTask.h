#ifndef APT_APPLICATIONTASK_INCLUDED
#define APT_APPLICATIONTASK_INCLUDED
/**
 * @file apt_applicationTask.h
 * Definition of global interface of module apt_applicationTask.c
 *
 * Copyright (C) 2015-2022 Peter Vranken (mailto:Peter_Vranken@Yahoo.de)
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
#include <typ_types.h>

/*
 * Defines
 */


/*
 * Global type definitions
 */


/*
 * Global data declarations
 */

/** Flag, to let the OS code initiate a restart of the application. 0: No action, 1: SW
    reset. */
extern uint8_t SDATA_P1(apt_restartApp);

/*
 * Global prototypes
 */

/* Format the current time in printable format. */
void apt_printCurrTime(char msgTime[], unsigned int sizeOfMsgTime);

#endif  /* APT_APPLICATIONTASK_INCLUDED */
