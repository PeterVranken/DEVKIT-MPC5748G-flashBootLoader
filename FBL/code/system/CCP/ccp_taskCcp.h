#ifndef CCP_TASKCCP_INCLUDED
#define CCP_TASKCCP_INCLUDED
/**
 * @file ccp_taskCcp.h
 * Definition of global interface of module ccp_taskCcp.c
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
#include "bsw_basicSoftware.h"

/*
 * Defines
 */

/** The CAN bus by zero based index, which should be used for CCP communication. */
#define CCP_IDX_CAN_BUS_FOR_CCP         0u
    
/** The CAN ID of the CCP Rx command message, aka CRO message. */
#define CCP_CAN_ID_CCP_CRO_MSG          0x600u

/** Boolean: Is the CAN ID of the CCP CRO message a 29 Bit extended ID? */
#define CCP_IS_EXT_CAN_ID_CCP_CRO_MSG   false
 
/** The CAN ID of the CCP Tx response message, aka DTO message. */
#define CCP_CAN_ID_CCP_DTO_MSG          0x650u

/** Boolean: Is the CAN ID of the CCP DTO message a 29 Bit extended ID? */
#define CCP_IS_EXT_CAN_ID_CCP_DTO_MSG   false
 
/** The 16 Bit station address of the ECU. This value is addressed to in the CCP CONNECT
    command. */
#define CCP_STATION_ADDR                0x1234u


/** The CCP task uses countable events as trigger. All events, which can trigger the task
    share the 32 bits of the task parameter. We define masks for the different events. Here
    for event "number of meanwhile received CCP CRO messages". */
#define CCP_TASK_CCP_RX_CRO__MASK_EV_RX_CRO     0xFFFF0000u

/** Since we define only masks with solid bits, the only thing we need to decode the
    multiplicity of an event is the number of bits to right shift after masking. Here for
    event "number of meanwhile received CCP CRO messages". */
#define CCP_TASK_CCP_RX_CRO__SHFT_EV_RX_CRO     16
    
/** The CCP task uses countable events as trigger. All events, which can trigger the task
    share the 32 bits of the task parameter. We define masks for the different events. Here
    for event "number of 1ms clock ticks". */
#define CCP_TASK_CCP_RX_CRO__MASK_EV_1MS        0x0000FFFFu

/** Since we define only masks with solid bits, the only thing we need to decode the
    multiplicity of an event is the number of bits to right shift after masking. Here for
    event "number of 1ms clock ticks". */
#define CCP_TASK_CCP_RX_CRO__SHFT_EV_1MS        0

/** The ID of the RTOS event processor, which we notify on reception of a CCP CRO mesasges. */
#define CCP_ID_EV_PROC_RX_CRO                   4u

/** We need a mailbox in the CAN driver for the CCP CRO Rx message. The APSW uses the
    registration API with mailboxes starting at index zero. We steal one mailbox by taking
    the very last one. This way, conflicts are widely avoided. */
#define CCP_IDX_MAILBOX_FOR_CCP_CRO     (BSW_IDX_LAST_RX_MAILBOX)

/** We need a mailbox in the CAN driver for the CCP DTO Tx message. The APSW uses the
    registration API with mailboxes starting at index zero. We steal one mailbox by taking
    the very last one. This way, conflicts are widely avoided. */
#define CCP_IDX_MAILBOX_FOR_CCP_DTO     (BSW_IDX_LAST_TX_MAILBOX)

/*
 * Global type definitions
 */


/*
 * Global data declarations
 */


/*
 * Global prototypes
 */

/** Initialize the module. */
bool ccp_osInitCcpTask(uint8_t tiTillResetToAppInMs);

/** The OS task, which is activated either on reception of a CCP CAN message. */
void ccp_taskOsRxCcp(uint32_t taskParam);

/** Hand over the next CRO command message. */
bool ccp_osFilterForCcpMsg(bool *pMsgConsumed, const bsw_rxCanMessage_t *pRxCanMsg);

/*
 * Global inline functions
 */


#endif  /* CCP_TASKCCP_INCLUDED */
