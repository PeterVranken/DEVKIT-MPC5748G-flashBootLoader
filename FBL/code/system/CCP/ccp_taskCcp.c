/**
 * @file ccp_taskCcp.c
 * This file implements the CCP communication task. It is activated on a CCP CRO Rx event
 * and it handles the received command message.
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
 *   ccp_osInitCcpTask
 *   ccp_osFilterForCcpMsg
 *   ccp_taskOSRxCcp
 * Local functions
 */

/*
 * Include files
 */

#include "ccp_taskCcp.h"

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "typ_types.h"
#include "bsw_basicSoftware.h"
#include "rtos.h"
#include "bsw_canInterface.h"
#include "cdr_canDriverAPI.h"

/*
 * Defines
 */
 
/** A full memory barrier, which we use to ensure that the volatile data-valid flag is
    toggled only after all data processing has completed. */
#define MEMORY_BARRIER_FULL()  {atomic_thread_fence(memory_order_seq_cst);}

/*
 * Local type definitions
 */
 
 
/*
 * Local prototypes
 */
 
 
/*
 * Data definitions
 */
 
/** The CRO command message in progress. */
static struct croMsg_t
{
    /** A flag, which indicated, whether the buffer is free, i.e., whether the preceding
        CRO message has been fully processed before the next one arrives. */
    volatile bool isBufferFree;
        
    /** The up to eight received message payload bytes by reference. The exposed data is
        valid only during the runtime of the callback. */
    unsigned char payload[8];

} _croMsg;
 
 
/*
 * Function implementation
 */

/**
 * Initialize the module. Needs to be called prior to the very first activation of the CCP
 * task.
 *   @return
 * The function returns \ true on success. If \a false is returned then CCP won't be
 * operational and the application should better not start up.
 *   @remark
 * This function depends on the CAN driver CDR, which needs to be initialized before
 * calling this function.
 */
bool ccp_osInitCcpTask(void)
{
    bool success = true;
    
    _croMsg.isBufferFree = true;
    
    /* We register the CCP CRO message for reception. The APSW uses the registration API
       with mailboxes starting at index zero. We steal one mailbox by taking the very last
       one. This way, conflicts are widely avoided. */
    if(cdr_osMakeMailboxReservation( CCP_IDX_CAN_BUS_FOR_CCP
                                   , CCP_IDX_MAILBOX_FOR_CCP_CRO
                                   , CCP_IS_EXT_CAN_ID_CCP_CRO_MSG
                                   , CCP_CAN_ID_CCP_CRO_MSG
                                   , /*isReceived*/ true
                                   , /*TxDLC*/ 0u /* doesn't care for Rx */
                                   , /*doNotify*/ true
                                   )
       != cdr_errApi_noError
      )
    {
        success = false;
    }
    if(cdr_osMakeMailboxReservation( CCP_IDX_CAN_BUS_FOR_CCP
                                   , CCP_IDX_MAILBOX_FOR_CCP_DTO
                                   , CCP_IS_EXT_CAN_ID_CCP_DTO_MSG
                                   , CCP_CAN_ID_CCP_DTO_MSG
                                   , /*isReceived*/ false
                                   , /*TxDLC*/ 8u
                                   , /*doNotify*/ false
                                   )
       != cdr_errApi_noError
      )
    {
        success = false;
    } 

    return success;

} /* ccp_osInitCcpTask */

/**
 * Hand over the next CRO command message to the CCP task.
 *   @return
 * The function returns \a true if it recognizes the message as CCP related.
 *   @param[out] pMsgConsumed
 * The function returns by reference the Boolean information whether or not is could
 * consume the CCP message. Get \a true if the function returns \a true but it couldn't
 * process the message due to an internal buffer overrun event. For non CCP messages thie
 * value will always be returned as \a false.\n
 *   Most of the time, a value of \a false points to a protocol error and will terminate the
 * CCP session.
 *   @param[in] pRxCanMsg
 * The message to check by reference.
 */
bool ccp_osFilterForCcpMsg( bool * const pMsgConsumed
                          , const bsw_rxCanMessage_t * const pRxCanMsg
                          )
{
    if(pRxCanMsg->idxCanBus == CCP_IDX_CAN_BUS_FOR_CCP
       &&  pRxCanMsg->idxMailbox == CCP_IDX_MAILBOX_FOR_CCP_CRO
      )
    {
        /* We need to copy the message data. The pointer is valid only during the call of
           this function. */
        if(pRxCanMsg->sizeOfPayload == 8u  &&  _croMsg.isBufferFree)
        {
            memcpy(_croMsg.payload, pRxCanMsg->payload, 8u);
            MEMORY_BARRIER_FULL();
            _croMsg.isBufferFree = false;
            *pMsgConsumed = true;
            
            /* After queuing the CCP CRO message, send an event, which activates the CCP
               response task. Using an event ensures minimal latency between receiving the
               CRO and transmitting the DTO message. */
            rtos_osSendEventCountable( CCP_ID_EV_PROC_RX_CRO
                                     , CCP_TASK_CCP_RX_CRO__MASK_EV_RX_CRO
                                     );
// TODO We need some buffer overrun indication so that the CCP protocol state machine can close the session
        }
        else
            *pMsgConsumed = false;
        
        return true;
    }
    else
    {
        *pMsgConsumed = false;
        return false;
    }
} /* ccp_osFilterForCcpMsg */


/**
 * The OS task, which is activated either on reception of a CCP CAN message or once a
 * Millisecond.
 *   @param taskParam
 * The task receives a combined argument. The argument indicates, why the task is activated
 * this time. It contains the count of CCP Rx messages, which have been queued since the
 * previous task activation and the number of 1-ms-timer events since the previous task
 * activation.
 */
void ccp_taskOsRxCcp(uint32_t taskParam)
{
    const uint32_t noQueuedCcpMsgs = (taskParam & CCP_TASK_CCP_RX_CRO__MASK_EV_RX_CRO)
                                     >> CCP_TASK_CCP_RX_CRO__SHFT_EV_RX_CRO;
    uint32_t noTimerTicks1ms = (taskParam & CCP_TASK_CCP_RX_CRO__MASK_EV_1MS)
                               >> CCP_TASK_CCP_RX_CRO__SHFT_EV_1MS;

    /* Caution, noQueuedCcpMsgs is not identical to the number of messages, we now have in
       the queue: Some messages may have been queued during the previous active time of
       this task (so after its previous activation) and may have been consumed already by
       that previous activation. */
    assert(noQueuedCcpMsgs > 0u  ||  noTimerTicks1ms > 0u);

    static uint32_t SDATA_OS(cnt1ms_) = 0u;
    cnt1ms_ += noTimerTicks1ms;

    /* Serve the CCP protocol for download of data and flashing only when the task was
       triggered by CAN Rx event(s). */
    if(noQueuedCcpMsgs > 0u)
    {
        assert(!_croMsg.isBufferFree);
        iprintf( "%lu new CRO msgs. Command ID is 0x%02X.\r\n"
               , noQueuedCcpMsgs
               , (unsigned)_croMsg.payload[0]
               );
        //ccp_osCcpStackMain();
        const uint8_t payloadDto[8] =
        {
            [0] = 0xFFu,
            [1] = 0x33u, /* Access denied */
            [2] = _croMsg.payload[1], /* Echo command counter. */
        };
        const cdr_errorAPI_t canErr = cdr_osSendMessage( CCP_IDX_CAN_BUS_FOR_CCP
                                                       , CCP_IDX_MAILBOX_FOR_CCP_DTO
                                                       , payloadDto
                                                       );
        MEMORY_BARRIER_FULL();
        _croMsg.isBufferFree = true;
    }
    
    if((cnt1ms_ % 10000u) == 0u)
        iprintf("%lus\r\n", cnt1ms_/1000u);

} /* ccp_taskOSRxCcp */
