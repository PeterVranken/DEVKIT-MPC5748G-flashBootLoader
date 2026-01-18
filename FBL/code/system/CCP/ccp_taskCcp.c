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
 *   ccp_taskOsRxCcp
 * Local functions
 *   discardCro
 *   finalizeDtoMsg
 *   readU32FromCro
 *   onDisconnect
 *   onSetMta
 *   submitClearMemory
 *   onClearMemory
 *   submitProgram
 *   onProgram
 *   onRxCroMsg
 *   onCanError
 *   reSubmitCroCmd
 *   onClockTick
 *   checkForAsyncEvents
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
#include "rom_flashRom.h"

/*
 * Defines
 */

/** The 16 Bit station address of the ECU. This value is addressed to in the CCP CONNECT
    command. */
#define CCP_STATION_ADDR        0x1234u

/** Development support: Set verbosity. If not zero then more or less information is
    written to the console. Needs to be zero for productive use. */
#define VERBOSE     2

/** The maximum wait time between arrival of a CCP command and the flash ROM driver
    returning to state idle (from a previous command). If this time elapses then the CCP
    command reponds an error message and the flash procedure will normally fail and be
    aborted.\n
      Note, this time can be chosen rather short. Waiting for the flash driver occurs only
    after program commands, becasue they are executed asynchronously. The long lasting
    erase commands are execute synchronously with the CCP protocol flow and don't cause a
    wait-of-idle of a subsequent command. */
#define CCP_TI_MAX_WAIT_FLASH_DRV_BUSY_IN_MS        100u

/** The maximum wait time for completion of an erase command in the flash ROM driver. This
    timeout should be in the magnitude of 10s. */
#define CCP_TI_MAX_WAIT_FLASH_DRV_ERASE_IN_MS       20000u

/** The maximum wait time between an unexpected protocol error and the flash ROM driver
    returning to state idle. Normally, we wait up to this this time before we finally abort
    the current session. We want to see the flash ROM driver in idle when aborting, so that
    we are again in the proper state of potentially starting a new session.\n
      A CAN protocol error can occur at any time, also in the middle of  along lasting
    flash erasure operation. Therefore this timeout should be in the magnitude of 10s. */
#define CCP_TI_MAX_WAIT_ABORT_FLASH_DRV_BUSY_IN_MS  20000u

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
    ccpCmd_setMta = 0x02u,
    ccpCmd_disconnect = 0x07u,
    ccpCmd_clearMemory = 0x10u,
    ccpCmd_program = 0x18u,
    ccpCmd_program6 = 0x22u,
};

/** The CCP command retun codes, which are in use. */
enum ccpCmdResponseCode_t
{
    ccpCmdRespCode_noError = 0x00u,
    ccpCmdRespCode_paramOutOfRange = 0x32u,
    ccpCmdRespCode_overload = 0x34u,
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
        ccp_stateFsm_flashDrvBusy,  /**< Waiting for flash driver becoming available. */
        ccp_stateFsm_erasing,   /**< CLEAR_MEMORY can't be answerd immediately, we are
                                     waiting for the flash driver. */
        ccp_stateFsm_aborting,  /**< Closing session as sson as possible after errors. */

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

    /** A down-counter for limiting time spans. Used as timeout in the states, where we
        wait for the flash ROM driver to complete. */
    uint32_t tiWait;
    
    /** The current value of the data pointer MTA0. */
    uint32_t mta0;

    /** Temporary storage of the number of bytes to erase or program. Used in case of
        delayed submission of the command due to the flash ROM driver being temporarily
        busy. */
    uint32_t noBytesToEraseOrProgram;
    
    /** Temporary storage of the number of bytes to program. Used in case of delayed
        submission of the PROGRAM or PROGRAM_6 command due to the flash ROM driver being
        temporarily busy. */
    const uint8_t *pDataToProgram;
    
    /** An input event of the FSM: Error receivig a CRO message. A signal to close the
        session. The asynchronity between event sender (a CAN ISR context) and receiver
        (the CCP task) is handled by the counter pattern: The ISR is the owner and only
        writer of this event counter and the CCP is owner and only writer of the
        acknowledge counter. The difference of the two counters is the number of reported
        events. */
    volatile uint8_t canRxErrCnt;

    /** See \a canRxErrCnt. */
    uint8_t canRxErrAck;

    /** An input event of the FSM: Sending the DTO failed. A signal to close the session. */
    bool canTxErr;

} _ccpFsm;


/*
 * Function implementation
 */

/**
 * Initialize or rset the finite state machine, which implements the CCP protocol.\n
 *   This function can be used to abort the communication with the client in case of
 * recognized protocol errors. The session is terminated (without feedback to the client)
 * and a new session connect becomes possible.
 */
static void initFsm(void)
{
    _ccpFsm.state = ccp_stateFsm_idle;
    _ccpFsm.dtoMsg.isResponseReady = false;
    _ccpFsm.tiWait = 0u;
    _ccpFsm.mta0 = 0u;
    _ccpFsm.noBytesToEraseOrProgram = 0u;
    _ccpFsm.canTxErr = false;
    _ccpFsm.canRxErrAck = _ccpFsm.canRxErrCnt;

    MEMORY_BARRIER_FULL();
    _ccpFsm.croMsg.isBufferFree = true;

} /* initFsm */


/**
 * Initialize the module. Needs to be called prior to the very first activation of the CCP
 * task.
 *   @return
 * The function returns \a true on success. If \a false is returned then CCP won't be
 * operational and the application should better not start up.
 *   @remark
 * This function depends on the CAN driver CDR, which needs to be initialized before
 * calling this function.
 */
bool ccp_osInitCcpTask(void)
{
    bool success = true;

    _ccpFsm.canRxErrCnt = 0u;
    initFsm();

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
        }
        else
        {
            /* Report a CAN communication error to the CCP task. This is a fatal situation
               and a potentially open session will be closed. */
            ++ _ccpFsm.canRxErrCnt;
            *pMsgConsumed = false;
        }

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
static inline void finalizeDtoMsg(enum ccpCmdResponseCode_t cmdResponseCode)
{
    /* Packet ID is always 0xFF. */
    _ccpFsm.dtoMsg.payload[0] = 0xFFu;

    _ccpFsm.dtoMsg.payload[1] = (unsigned)cmdResponseCode;

    /* Echo the command counter. */
    _ccpFsm.dtoMsg.payload[2] = _ccpFsm.croMsg.payload[1];

    /* Set flag for next possible transmission of DTO. */
    _ccpFsm.dtoMsg.isResponseReady = true;

} /* finalizeDtoMsg */


/** 
 * Helper: Read a 32 Bit word from the payload of a CRO message.\n
 *   The word is assumed to have big endian, MSB first.
 *   @param[in] idxFirstByte
 * The index of the MSB in the CRO's payload. Range is 2..4.
 */
static inline uint32_t readU32FromCro(unsigned int idxFirstByte)
{
    assert(idxFirstByte >= 2u  &&  idxFirstByte <= 4u);
    return ((((uint32_t)_ccpFsm.croMsg.payload[idxFirstByte+0] << 8)
             | _ccpFsm.croMsg.payload[idxFirstByte+1]
            ) << 8
            | _ccpFsm.croMsg.payload[idxFirstByte+2]
           ) << 8
           | _ccpFsm.croMsg.payload[idxFirstByte+3];

} /* readU32FromCro */


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
 * The reaction on a received CCP SET_MTA command.
 */
static void onSetMta(void)
{
    /* Address extension and MTA1 are not required or supported by the FBL. We reject such a
       command with parameter out of range response. The same happens, if the address is
       outside available flash ROM - this check is delegated to the flash ROM driver. */
    const uint8_t addrExt = _ccpFsm.croMsg.payload[3];
    const bool isMta0 = _ccpFsm.croMsg.payload[2] == 0u;
    const uint32_t addr = readU32FromCro(/*idxFirstByte*/ 4u);
    const bool isValidAddr = addrExt == 0u  && rom_isValidFlashAddressRange(addr, 1);

    unsigned int reponseCode;
    if(isMta0 && isValidAddr)
    {
        _ccpFsm.mta0 = addr;
        #if VERBOSE >= 1
        iprintf("Setting MTA0 to 0x%lX.\r\n", addr);
        #endif
        reponseCode = ccpCmdRespCode_noError;
    }
    else
    {
        #if VERBOSE >= 1
        iprintf( "Setting MTA%u to 0x%lX.%u is rejected.\r\n"
               , _ccpFsm.croMsg.payload[2]
               , addr
               , (unsigned)addrExt
               );
        #endif
        reponseCode = ccpCmdRespCode_paramOutOfRange;
    }

    finalizeDtoMsg(reponseCode);

} /* onSetMta */


/**
 * When the availability and the correctness of the erase comand have been checked, then
 * this function initiates the operation at the flash driver.\n
 *   This function is either called immediately on reception of a CCP CLEAR_MEMORY command
 * or a bit later, when we first had to wait for the flash ROM driver becoming idle. 
 */
static void submitClearMemory(void)
{
    /* As the flash driver is free we submit the erase command immediately and wait in
       another state for its completion. */
    if(rom_startEraseFlashMemory(_ccpFsm.mta0, _ccpFsm.noBytesToEraseOrProgram))
    {
        #if VERBOSE >= 1
        iprintf( "Now clearing %lu Bytes at 0x%08lX.\r\n"
               , _ccpFsm.noBytesToEraseOrProgram
               , _ccpFsm.mta0
               );
        #endif
        _ccpFsm.tiWait = CCP_TI_MAX_WAIT_FLASH_DRV_ERASE_IN_MS;
        _ccpFsm.state = ccp_stateFsm_erasing;
    }
    else
    {
        /* The flash driver doesn't accept the command. */
        // TODO Check if this is an assertion; we had beforehand checked busy and the address range
        _ccpFsm.state = ccp_stateFsm_connected;
        finalizeDtoMsg(ccpCmdRespCode_paramOutOfRange);
    }        
} /* submitClearMemory */


/**
 * The reaction on a received CCP CLEAR_MEMORY command.\n
 *   The command is executed synchronously with respect to the CCP protocol flow. The CCP
 * will receive the DTO only after completion. There are two blocking states involved. When
 * the command comes in then the flash driver could be still busy (in an asynchronously
 * executetd, earlier programm command). If it is free, the erase command can be submitted
 * but then we need to wait aiagn for its completion. The CCP state machine uses two
 * pending states to synchronize with these blocking states of the flash driver.
 */
static void onClearMemory(void)
{
    _ccpFsm.noBytesToEraseOrProgram = readU32FromCro(/*idxFirstByte*/ 2u);
    const bool isValidAddrRange = rom_isValidFlashAddressRange( _ccpFsm.mta0
                                                              , _ccpFsm.noBytesToEraseOrProgram
                                                              );
    if(isValidAddrRange)
    {
        if(!rom_isFlashDriverBusy())
            submitClearMemory();
        else
        {
            /* If the flash driver is busy then we enter the state to wait for its
               availability. We don't store additional information, all we need to know
               later remains in the CRO message buffer. */
            _ccpFsm.tiWait = CCP_TI_MAX_WAIT_FLASH_DRV_BUSY_IN_MS;
            _ccpFsm.state = ccp_stateFsm_flashDrvBusy;
        }
    }
    else
    {
        #if VERBOSE >= 1
        iprintf( "Clearing %lu Bytes at 0x%08lX is rejected.\r\n"
               , _ccpFsm.noBytesToEraseOrProgram
               , _ccpFsm.mta0
               );
        #endif
        finalizeDtoMsg(ccpCmdRespCode_paramOutOfRange);
    }
} /* onClearMemory */


/**
 * When the availability and the correctness of the PROGRAM or PROGRAM_6 comand have been
 * checked, then this function initiates the operation at the flash driver.\n
 *   This function is either called immediately on reception of a CCP PROGRAM(_6) command
 * or a bit later, when we first had to wait for the flash ROM driver becoming idle. 
 */
static void submitProgram(void)
{
    /* As the flash driver is free we submit the program command immediately and return the
       DTO. The actual programming in running asynchronously from now on in the flash
       driver. */
    enum ccpCmdResponseCode_t reponseCode;
    if(rom_startProgram(_ccpFsm.mta0, _ccpFsm.pDataToProgram, _ccpFsm.noBytesToEraseOrProgram))
    {
        #if VERBOSE >= 3
        iprintf( "Now programming %lu Bytes at 0x%08lX.\r\n"
               , _ccpFsm.noBytesToEraseOrProgram
               , _ccpFsm.mta0
               );
        #endif
        reponseCode = ccpCmdRespCode_noError;
    }
    else
    {
        /* The flash driver doesn't accept the command. */
        // TODO Check if this is an assertion; we had beforehand checked busy and the address range
        reponseCode = ccpCmdRespCode_paramOutOfRange;
    }
    
    finalizeDtoMsg(reponseCode);
    _ccpFsm.state = ccp_stateFsm_connected;
        
} /* submitProgram */


/**
 * The reaction on a received CCP PROGRAM command.\n
 *   The command is executed asynchronously with respect to the CCP protocol flow. The CCP
 * will receive the DTO after successful submission of the command at the flash driver,
 * which only means that command and data have been buffered in the driver. This leads to a
 * potential blocking state. If the driver's input buffer is full then we wait till the
 * driver is available again. After this wait phase, the DTO is returned.\n
 *   It depends on the speed of programming and CCP data transmission, what will typically
 * happen. If programming is in average faster than CCP data transfer, then we will rarely see
 * the blocking happen and CCP data tansfer is running at full speed. If vice versa, then
 * programming will slow down the CCP data transfer to the average rate of programming.
 */
static void onProgram(bool isProgram6)
{
    if(isProgram6)
    {
        _ccpFsm.noBytesToEraseOrProgram = 6u;
        _ccpFsm.pDataToProgram = &_ccpFsm.croMsg.payload[2];
    }
    else
    {
        _ccpFsm.noBytesToEraseOrProgram = (unsigned)_ccpFsm.croMsg.payload[2];
        _ccpFsm.pDataToProgram = &_ccpFsm.croMsg.payload[3];
    }
    const bool isValidAddrRange = rom_isValidFlashAddressRange( _ccpFsm.mta0
                                                              , _ccpFsm.noBytesToEraseOrProgram
                                                              );
    if(isValidAddrRange)
    {
        if(!rom_isFlashDriverBusy())
            submitProgram();
        else
        {
            /* If the flash driver is busy then we enter the state to wait for its
               availability. We don't store additional information, all we need to know
               later remains in_ccpFsm and the CRO message buffer. */
            _ccpFsm.tiWait = CCP_TI_MAX_WAIT_FLASH_DRV_BUSY_IN_MS;
            _ccpFsm.state = ccp_stateFsm_flashDrvBusy;
        }
    }
    else
    {
        #if VERBOSE >= 1
        iprintf( "Prgramming %lu Bytes at 0x%08lX is rejected.\r\n"
               , _ccpFsm.noBytesToEraseOrProgram
               , _ccpFsm.mta0
               );
        #endif
        finalizeDtoMsg(ccpCmdRespCode_paramOutOfRange);
    }
} /* onProgram */


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
           see if it talks to us. It is a 16 Bit value, LSB first. */
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
                iprintf( "CCP CONNECT with station 0x%04X is ignored. We are station"
                         " 0x%04X.\r\n"
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
        case ccpCmd_disconnect:
            onDisconnect();
            break;

        case ccpCmd_setMta:
            onSetMta();
            break;

        case ccpCmd_clearMemory:
            onClearMemory();
            break;

        case ccpCmd_program:
            onProgram(/*isProgram6*/ false);
            break;

        case ccpCmd_program6:
            onProgram(/*isProgram6*/ true);
            break;

        default:
            // TODO Shall we abort the session? Likely yes, protocol error
            #if VERBOSE >= 2
            iprintf("Unsupported CCP CRO command %u received.\r\n", _ccpFsm.croMsg.payload[0]);
            #endif
            discardCro();
        }
        break;

    default:
        assert(false);
    case ccp_stateFsm_aborting:
        /* These other states ignore any incoming CRO message. */
        discardCro();

    } /* switch(Which state are we in?) */
} /* onRxCroMsg */


/**
 * Process a CAN error event in the protocol state machine.
 */
static void onCanError(void)
{
    /* CAN errors in state idle, when no CCP connection is active, are silently ignored. In
       all other states, we terminate the session as soon as possible. We only wait for a
       possibly running flash job to terminate. */
    if(_ccpFsm.state != ccp_stateFsm_idle)
    {
#if VERBOSE >= 1
        iprintf("CCP session is aborted due to CAN Rx/Tx errors.\n");
#endif
        _ccpFsm.tiWait = CCP_TI_MAX_WAIT_ABORT_FLASH_DRV_BUSY_IN_MS;
        _ccpFsm.state = ccp_stateFsm_aborting;
        
    }
} /* onCanError */


/**
 * After waiting for the flash driver becoming idle, we can submit the pending CCP command.
 * The information about the command (erase or program) is still found in the CRO message
 * buffer.
 */
static void reSubmitCroCmd(void)
{
    assert(_ccpFsm.state == ccp_stateFsm_flashDrvBusy);
    switch(_ccpFsm.croMsg.payload[0])
    {
// TODO DISCONNECT should use this mechanism, too, to wait for the flash driver to complete its last (program) action
//    case ccpCmd_disconnect:
//        submitDisconnect();
//        break;

    case ccpCmd_clearMemory:
        submitClearMemory();
        break;

    case ccpCmd_program:
    case ccpCmd_program6:
        submitProgram();
        break;

    default:
        assert(false);
        discardCro();
    }
    assert(_ccpFsm.state != ccp_stateFsm_flashDrvBusy);
    
} /* reSubmitCroCmd */


/**
 * Timer event for the protocol state machine.
 */
static void onClockTick(void)
{
    switch(_ccpFsm.state)
    {
    case ccp_stateFsm_aborting:
        /* Try waiting for flash ROM driver being idle again, the abort everything and
           return to state idle. No DTO is sent any more. */
        if(_ccpFsm.tiWait > 0u)
        {   
            if(rom_isFlashDriverBusy())
                -- _ccpFsm.tiWait;
            else
                initFsm();
        }
        else
            initFsm();

        break;

    case ccp_stateFsm_flashDrvBusy:
        /* A CCP command is pending. The demanded action (erase, program) could not be
           initiate yet, since the flash ROM driver was still busy. Now check its state
           again and go ahead when eventually free again. However, don't block forever,
           consider a timeout. */
        if(_ccpFsm.tiWait > 0u)
        {   
            if(rom_isFlashDriverBusy())
                -- _ccpFsm.tiWait;
            else
            {
                /* The CRO message buffer still contains the CRO message, which we could
                   not process yet due to the flash driver being busy. Now we try the
                   evaluation of the message again. */
                reSubmitCroCmd();
            }
        }
        else
        {
            #if VERBOSE >= 2
            iprintf("Flash driver stuck, CCP command fails.\n");
            #endif
            _ccpFsm.state = ccp_stateFsm_connected;
            finalizeDtoMsg(ccpCmdRespCode_overload);
        }
        break;

    case ccp_stateFsm_erasing:
        /* Erasure of flash has been initated at the flash driver. Now wait for completion.
           However, don't block forever, consider a timeout. */
        if(_ccpFsm.tiWait > 0u)
        {   
            if(rom_isFlashDriverBusy())
                -- _ccpFsm.tiWait;
            else
            {
                /* Erasure completed. Eventually, we can send out the DTO. */
                #if VERBOSE >= 1
                iprintf("Flash erasure completed.\r\n");
                #endif
                _ccpFsm.mta0 += _ccpFsm.noBytesToEraseOrProgram;
                _ccpFsm.state = ccp_stateFsm_connected;
                finalizeDtoMsg(ccpCmdRespCode_noError);
            }
        }
        else
        {
            #if VERBOSE >= 2
            iprintf("Flash driver stuck in erase, CCP command fails.\n");
            #endif
            _ccpFsm.mta0 += _ccpFsm.noBytesToEraseOrProgram;
            _ccpFsm.state = ccp_stateFsm_connected;
            finalizeDtoMsg(ccpCmdRespCode_overload);
        }
        break;

    default:
        assert(false);
    case ccp_stateFsm_idle:
    case ccp_stateFsm_connected:
        /* Nothing to do on regular clock tick in these states. */
        break;

    } /* switch(Which state are we in?) */
} /* onClockTick */


/**
 * Only CRO Rx events and regular timer clock tick events directly and immediately trigger
 * an update of the CCP protocol state machine. Other events need to be checked on a
 * regular base.\n
 *   This function polls the flags, which indicate such asynchronous events; in the first
 * place it are CAN transmission errors. If an event is detected, then the related handler
 * is invoked. The concept is to call this function as often as reasonably possible.
 */
static void checkForAsyncEvents(void)
{
    /* Check for CAN Rx and Tx errors. Rx errors are set asynchronously from the CAN ISR,
       Tx errors are set from the CCP task itself. (When trying to submit the DTO response
       messages.)*/
    const uint8_t canRxErrCnt = _ccpFsm.canRxErrCnt;
    const bool canErr = canRxErrCnt != _ccpFsm.canRxErrAck  ||  _ccpFsm.canTxErr;
    _ccpFsm.canRxErrAck = canRxErrCnt;
    _ccpFsm.canTxErr = false;
    
    /* If we see a CAN error, then we call the related handler, which takes the state
       dependent decision. */
    if(canErr)
    {
#if VERBOSE >= 3
        iprintf("State %u: CAN communication error.\n", (unsigned)_ccpFsm.state);
#endif
        onCanError();
    }
} /* checkForAsyncEvents */


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
        iprintf("CCP task %lus up and running\r\n", cnt1ms_/1000u);
#endif

    /* Serve the CCP protocol for download of data and flashing only when the task was
       triggered by CAN Rx event(s). */
    if(noQueuedCcpMsgs > 0u)
    {
        assert(!_ccpFsm.croMsg.isBufferFree);
        onRxCroMsg();
    }

    if(noTimerTicks1ms > 0u)
    {
        // TODO Could be done in any task activation
        checkForAsyncEvents();
        
        onClockTick();
        
        /* Run the flash ROM driver from the same task as the CCP protocol. This eliminates
           all race conditions between the two and all commanding and error/result checking
           becomes most easy. */
        rom_flashRomMain();
    }

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
            _ccpFsm.canTxErr = true;
    }
} /* ccp_taskOSRxCcp */
