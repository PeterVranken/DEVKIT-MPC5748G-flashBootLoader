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
 *   discardCro
 *   finalizeDtoMsg
 *   onDisconnect
 *   onRxCroMsg
 *   ccpStackMain
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

/** The 16 Bit station address of the ECU. This value is addressed to in the CCP CONNECT
    command. */
#define CCP_STATION_ADDR        0x1234u

/** Development support: Set verbosity. If not zero then more or less information is
    written to the console. Needs to be zero for productive use. */
#define VERBOSE     2

/** A full memory barrier, which we use to ensure that the volatile data-valid flag is
    toggled only after all data processing has completed. */
#define MEMORY_BARRIER_FULL()   {atomic_thread_fence(memory_order_seq_cst);}

/*
 * Local type definitions
 */

/** The CCP commands, which are supported by the implementation. */
enum ccpCmd_t
{
    ccpCmd_connect = 0x01u,
    ccpCmd_disconnect = 0x07u,
};

/** The CCP command retun codes, which are in use. */
enum ccpCmdResponseCode_t
{
    ccpCmdRespCode_noError = 0x00u,
    ccpCmdRespCode_paramOutOfRange = 0x32u,
};

/*
 * Local prototypes
 */


/*
 * Data definitions
 */

/** The runtime data of the finite state machine that implements the CCP protocol. */
static struct ccpFsm_t
{
    /** The current state of the FSM. */
    enum
    {
        ccp_stateFsm_idle,      /**< Not in a session, waiting for CONNECT. */
        ccp_stateFsm_connected, /**< Connected, waiting for next command. */
        ccp_stateFsm_erasing,   /**< CLEAR_MEMORY can't be answerd immediately, we are
                                     waiting for the flash driver. */
    } state;

    /** The CRO command message in progress. */
    struct croMsg_t
    {
        /** A flag, which indicates, whether the buffer is free, i.e., whether the preceding
            CRO message has been fully processed before the next one arrives. */
        volatile bool isBufferFree;

        /** The up to eight received message payload bytes. */
        uint8_t payload[8];

    } croMsg;

    /** The DTO command message to return. */
    struct dtoMsg_t
    {
        /** A flag, which indicates, whether the processing reached the state that the DTO
            reponse message can be sent back to the CCP client. */
        bool isResponseReady;

        /** The up to eight message payload bytes by reference. */
        uint8_t payload[8];

    } dtoMsg;

    /** A input event of the FSM: Sending the DTO failed. A signal to close the session. */
    bool canTxErr;

} _ccpFsm;


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

    _ccpFsm.state = ccp_stateFsm_idle;
    _ccpFsm.croMsg.isBufferFree = true;
    _ccpFsm.dtoMsg.isResponseReady = false;

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
 *   @remark
 * This function is expected to be called from a CAN interrupt, thus asynchronously to the
 * rest of the code, i.e., the implementation of the CCP state machine in its own task.
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
        if(pRxCanMsg->sizeOfPayload == 8u  &&  _ccpFsm.croMsg.isBufferFree)
        {
            memcpy(_ccpFsm.croMsg.payload, pRxCanMsg->payload, 8u);
            MEMORY_BARRIER_FULL();
            _ccpFsm.croMsg.isBufferFree = false;
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
 * Discard a received CRO message, which is silently ignored because it either doesn't
 * address to us or we are not in a successfully connected CCP session.
 */
static inline void discardCro(void)
{
#if VERBOSE >= 2
    iprintf( "State %u: CRO msg 0x%02X is ignored.\r\n"
           , (unsigned)_ccpFsm.state
           , (unsigned)_ccpFsm.croMsg.payload[0]
           );
#endif
    MEMORY_BARRIER_FULL();
    _ccpFsm.croMsg.isBufferFree = true;

} /* discardCro */


/**
 * Helper: Complete a DTO by entering the sequence counter and setting the ready flag.
 *   @param[in] cmdResponseCode
 * The CCP response code, which written into the DTO message.
 */
static inline void finalizeDtoMsg(unsigned int cmdResponseCode)
{
    /* Packet ID is always 0xFF. */
    _ccpFsm.dtoMsg.payload[0] = 0xFFu;

    _ccpFsm.dtoMsg.payload[1] = cmdResponseCode;

    /* Echo the command counter. */
    _ccpFsm.dtoMsg.payload[2] = _ccpFsm.croMsg.payload[1];

    /* Set flag for next possible transmission of DTO. */
    _ccpFsm.dtoMsg.isResponseReady = true;

} /* finalizeDtoMsg */


/**
 * The reaction on a received CCP DISCONNECT command.
 */
static void onDisconnect(void)
{
    /* Since CCP can't have two session with two different ECU open at a time, the station
       address in the command needs to be ours - otherwise we see a protocol error. The
       reaction is the same in both cases; we close the session. Only the returned response
       code will differ.
         The implementation doesn't support temporary disconnection. If it is requested, we
       fully disconnect but return another response code. */
    const bool isEndOfSession = _ccpFsm.croMsg.payload[2] == 1u;
    const unsigned int stationAddr = ((unsigned)_ccpFsm.croMsg.payload[5] << 8)
                                     + _ccpFsm.croMsg.payload[4];
    unsigned int reponseCode;
    if(isEndOfSession && stationAddr == CCP_STATION_ADDR)
        reponseCode = ccpCmdRespCode_noError;
    else
        reponseCode = ccpCmdRespCode_paramOutOfRange;

    _ccpFsm.state = ccp_stateFsm_idle;
    
    #if VERBOSE >= 1
    iprintf("Disconnected from client.\r\n");
    #endif
    
    finalizeDtoMsg(reponseCode);

} /* onDisconnect */


/**
 * Most relevant input event of CCP state machine: A new CRO command message has been
 * received.
 */
static void onRxCroMsg(void)
{
    #if VERBOSE >= 2
    iprintf( "State %u: New CRO msg 0x%02X.\r\n"
           , (unsigned)_ccpFsm.state
           , (unsigned)_ccpFsm.croMsg.payload[0]
           );
    #endif
    
    switch(_ccpFsm.state)
    {
    case ccp_stateFsm_idle:
        /* The only accepted CRO is the CONNECT command. We compare the station address to
           see if it talks to us. It is a 16 Bit value, LSB fist*/
        if(_ccpFsm.croMsg.payload[0] == ccpCmd_connect)
        {
            const unsigned int wantedStationAddr = ((unsigned)_ccpFsm.croMsg.payload[3] << 8)
                                                   + _ccpFsm.croMsg.payload[2];
            if(wantedStationAddr == CCP_STATION_ADDR)
            {
                #if VERBOSE >= 1
                iprintf("Connected with CCP client.\r\n");
                #endif
                _ccpFsm.state = ccp_stateFsm_connected;
                finalizeDtoMsg(ccpCmdRespCode_noError);
            }
            else
            {
                /* CCP CONNEXT relates to another ECU. We must not respond or change our
                   state. The CRO message is silently discarded. */
                #if VERBOSE >= 1
                iprintf( "CCP CONNECT with station 0x%04X is ignored. We are station %u.\r\n"
                       , wantedStationAddr
                       , CCP_STATION_ADDR
                       );
                #endif
                discardCro();
            }
        }
        else
        {
            /* No CRO command is of any interest as long as we are not connected. */
            discardCro();
        }
        break;

    case ccp_stateFsm_connected:
        switch(_ccpFsm.croMsg.payload[0])
        {
        case ccpCmd_disconnect /* DISCONNECT */:
            onDisconnect();
            break;

        default:
            #if VERBOSE >= 2
            iprintf("Unsupported CCP CRO command %u received.\r\n", _ccpFsm.croMsg.payload[0]);
            #endif
            discardCro();
        }
        break;

    default:
        assert(false);
        discardCro();

    } /* switch(Which state are we in?) */
} /* onRxCroMsg */


/**
 * Main clock tick of the CCP state machine. It checks for asynchronous and timer events.
 */
static void ccpStackMain(void)
{
    /* A CAN transmission problem can't be healed. We close the session. (And remain silent
       towards the client.) */
    if(_ccpFsm.canTxErr)
    {
        _ccpFsm.dtoMsg.isResponseReady = false;
        _ccpFsm.canTxErr = false;
        _ccpFsm.state = ccp_stateFsm_idle;

        MEMORY_BARRIER_FULL();
        _ccpFsm.croMsg.isBufferFree = true;
    }
} /* ccpStackMain */


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

#if VERBOSE >= 1
    if((cnt1ms_ % 10000u) == 0u)
        iprintf("%lus\r\n", cnt1ms_/1000u);
#endif

    /* Serve the CCP protocol for download of data and flashing only when the task was
       triggered by CAN Rx event(s). */
    if(noQueuedCcpMsgs > 0u)
    {
        assert(!_ccpFsm.croMsg.isBufferFree);
        onRxCroMsg();
    }

    if(noTimerTicks1ms > 0u)
        ccpStackMain();

    if(_ccpFsm.dtoMsg.isResponseReady)
    {
        if(cdr_osSendMessage( CCP_IDX_CAN_BUS_FOR_CCP
                            , CCP_IDX_MAILBOX_FOR_CCP_DTO
                            , _ccpFsm.dtoMsg.payload
                            )
           == cdr_errApi_noError
          )
        {
            _ccpFsm.croMsg.isBufferFree = true;
            _ccpFsm.dtoMsg.isResponseReady = false;
        }
        else
        {
            _ccpFsm.canTxErr = true;
        }
    }
} /* ccp_taskOSRxCcp */
