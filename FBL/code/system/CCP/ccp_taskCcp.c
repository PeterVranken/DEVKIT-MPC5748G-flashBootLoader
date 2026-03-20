/**
 * @file ccp_taskCcp.c
 * This file implements the CCP communication task. It is activated on a CCP
 * CRO Rx event and it handles the received command message.
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
 *   resetAuthentication
 *   initFsm
 *   checkForDownloadKey
 *   isServiceResponseAddress
 *   setCanRxErr
 *   setCanRxErrInternally
 *   setCanTxErrInternally
 *   setTimeout
 *   checkTimeout
 *   discardCro
 *   finalizeDtoMsg
 *   readU32FromCro
 *   writeMta0IntoDto
 *   isValidFlashAddressRangeForProgram
 *   isFlashDriverReady
 *   finishDisconnect
 *   onDisconnect
 *   onSetMta
 *   finishUpload
 *   onUpload
 *   performDownload
 *   onDownload
 *   submitClearMemory
 *   onClearMemory
 *   submitProgram
 *   onProgram
 *   onDiagService
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
#include "cdr_canDriverAPI.h"
#include "rom_flashRomDriver.h"
#include "stm_systemTimer.h"
#include "swr_softwareReset.h"
#include "tds_taskDigSignature.h"

/*
 * Defines
 */

/** Development support: Set verbosity. If not zero then more or less information is
    written to the console. Needs to be zero for productive use. */
#define VERBOSE     1

/** The maximum wait time for completion of the authentication process. If it takes longer
    then the authentication fails. */
#define CCP_TI_MAX_WAIT_AUTHENTICATION_IN_MS        2000u

/** The maximum wait time between arrival of a CCP command and the flash ROM driver
    returning to state idle (from a previous command). If this time elapses then the CCP
    command reponds an error message and the flash procedure will normally fail and be
    aborted.\n
      Note, this time can be chosen rather short. Waiting for the flash driver occurs only
    after program commands, becasue they are executed asynchronously. The long lasting
    erase commands are execute synchronously with the CCP protocol flow and don't cause a
    wait-of-idle of a subsequent command. */
#define CCP_TI_MAX_WAIT_FLASH_DRV_BUSY_IN_MS        500u

/** The maximum wait time for completion of an erase command in the flash ROM driver. This
    timeout should be in the magnitude of 10s. */
#define CCP_TI_MAX_WAIT_FLASH_DRV_ERASE_IN_MS       25000u

/** The session timeout. The session is silently closed, if the client didn't send any CRO
    command this long. */
#define CCP_TI_MAX_SESSION_IDLE_IN_MS               5000u

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
    ccpCmd_download = 0x03u,
    ccpCmd_upload = 0x04u,
    ccpCmd_disconnect = 0x07u,
    ccpCmd_clearMemory = 0x10u,
    ccpCmd_program = 0x18u,
    ccpCmd_diagService = 0x20,
    ccpCmd_actionService = 0x21,
    ccpCmd_program6 = 0x22u,
    ccpCmd_download6 = 0x23u,
};

/** The CCP command retun codes, which are in use. */
enum ccpCmdResponseCode_t
{
    ccpCmdRespCode_noError = 0x00u,
    ccpCmdRespCode_unknownCommand = 0x30,
    ccpCmdRespCode_paramOutOfRange = 0x32u,
    ccpCmdRespCode_overload = 0x34u,
};

/** The diagnostic service numbers in use. */
enum diagServiceNum_t
{
    diagSN_uploadSeed = 0x00u,
    diagSN_uploadVersionFbl = 0x01u,
    diagSN_resetEcuToFbl = 0x02u,
    diagSN_resetEcuToApp = 0x08u, /**< Code chosen compatible with NXP's RAppId tool. */
};

/** The action service numbers in use. */
enum actionServiceNum_t
{
    actnSN_resetToApp = 0x00u,
    actnSN_resetToFbl = 0x01u,
    actnSN_checkKey = 0x02u,
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
        ccp_stateFsm_authenticationBusy,    /**< Waiting for result of authentication. */
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
    unsigned int tiWaitInMs;

    /** The current value of the data pointer MTA0. */
    uint32_t mta0;

    /** Temporary storage of the number of bytes to upload, erase or program. Used in case of
        delayed submission of the command due to the flash ROM driver being temporarily
        busy. */
    uint32_t noBytesToProcess;

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

} _ccpFsm SECTION(.data.OS._ccpFsm);

/** The authentication state. */
static enum
{
    stKey_noAuthenticationYet,  /**< Authentication not tried in current session. */
    stKey_authenticationFailed, /**< Authenitcation has failed and must not be repeated. */
    stKey_authentified,         /**< All CCP commands are permitted in this session. */

} _stateAuthentication SECTION(.data.OS._stateAuthentication) = stKey_noAuthenticationYet;

/** The time-to-reset counter states. */
static enum
{
    stReset_unlimitedOperation, /**< No reset is scheduled at all, FBL runs forever. */
    stReset_launchFbl,  /**< Reset is scheduled and will (re-)launch the FBL after reset. */
    stReset_launchApp,  /**< Reset is scheduled and will launch the flashed application. */

} _stateScheduledReset SECTION(.sdata.OS._stateScheduledReset) = stReset_unlimitedOperation;

/** The counter till reset. Unit is Milliseconds. Reset happens at counter value 0 and if
    _stateScheduledReset is not \a stReset_unlimitedOperation. */
static uint16_t _tiTillResetInMs = 0;

/*
 * Function implementation
 */

/**
 * Initialize the authentication state.
 */
static inline void resetAuthentication(void)
{
    _stateAuthentication = stKey_noAuthenticationYet;
    tds_osResetAuthenticationData();

} /* resetAuthentication */


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
    _ccpFsm.tiWaitInMs = 0u;
    _ccpFsm.mta0 = 0u;
    _ccpFsm.noBytesToProcess = 0u;
    _ccpFsm.canTxErr = false;
    _ccpFsm.canRxErrAck = _ccpFsm.canRxErrCnt;
    resetAuthentication();

    MEMORY_BARRIER_FULL();
    _ccpFsm.croMsg.isBufferFree = true;

} /* initFsm */


/**
 * Check, if CCP DOWNLOAD completed the download of an authentication key. If so, check the
 * key and update the global authentication state.
 *   @return
 * The function returns \a true if a key upload has been recognized and the validation of
 * the downloaded key has been initiated. The state machine must enter a wait state for the
 * completion of the validation. Otherwise \a false is returned and CCP communication can
 * continue with wait state.
 */
static bool checkForDownloadKey()
{
    /* We don't really get a clear signal or notification (e.g., by action service), when
       the key is entirely downloaded. We assume the normal doing of the CCP client,
       downloading with rising MTA. If the MTA reaches the end of the agreed buffer then
       the key download has completed. If the client uses another download pattern then bad
       luck, authentication will fail, which is not our problem.
         1st term in condition: We allow only one attempt per session. */
    if(_stateAuthentication == stKey_noAuthenticationYet
       && _ccpFsm.mta0
          == (uint32_t)&tds_authenticationData.keyAry[TDS_SIZE_OF_AUTHENTICATION_KEY]
      )
    {
        /* Initiate key check. The validation takes about 650ms and is delegated to a low
           priority background task. (Time triggered tasks won't be blocked.) This requires
           on the other hand that the state machine in this high priority task enters a
           wait state until the validation is done. */
        return tds_osStartVerificationOfSignature();
    }
    else
        return false;

} /* checkForDownloadKey */


/**
 * Check if an address requested by CCP command UPLOAD or DOWNLOAD points to the result of
 * a DIAG_SERVICE or ACTION_SERVICE request.
 *   @param[in] isUpload
 * The check depends on whether the CCP client wants to up- or download data.
 *   @param[in] address
 * The first address of the memory area requested for upload.
 *   @param[in] size
 * The length of the memory area in Byte.
 */
static inline bool isServiceResponseAddress(bool isUpload, uint32_t address, uint32_t size)
{
    const uint32_t endAddr = address + size;

    /* We can handle the overflow at the end of the 32 Bit address space very easily,
       because the very last address in the address space is surely not used for the
       requestable information. */
    if(endAddr < address)
        return false;

    /* We simply check for all the few services, we support. */
    return isUpload
           && (address >= (uint32_t)&bsw_version[0]
               && endAddr <= (uint32_t)&bsw_version[bsw_sizeOfVersion]
               && _stateAuthentication == stKey_authentified
               ||  address >= (uint32_t)&tds_authenticationData.seed
                   && endAddr <= (uint32_t)&tds_authenticationData.seed
                                  + sizeof(tds_authenticationData.seed)
                                  + sizeof(tds_authenticationData.addrOfKey)
                   && _stateAuthentication == stKey_noAuthenticationYet
              )
           || !isUpload
              && _stateAuthentication == stKey_noAuthenticationYet
              && (address >= (uint32_t)&tds_authenticationData.keyAry[0]
                  && endAddr <= (uint32_t)&tds_authenticationData.keyAry[0]
                                 + sizeof(tds_authenticationData.keyAry)
                 );
} /* isServiceResponseAddress */


/**
 * Set a CAN Rx error from another CPU context, e.g., the CAN ISR.
 */
static inline void setCanRxErr(void)
{
    ++ _ccpFsm.canRxErrCnt;
}


/**
 * Set a CAN Rx error from the CCP task. This method must not be used from another context, e.g., the CAN ISR.
 */
static inline void setCanRxErrInternally(void)
{
    -- _ccpFsm.canRxErrAck;
}


/**
 * Set a CAN Tx error from the CCP task. This method must not be used from another context, e.g., the CAN ISR.
 */
static inline void setCanTxErrInternally(void)
{
    _ccpFsm.canTxErr = true;
}


/**
 * Initialize the module. Needs to be called prior to the very first activation of the CCP
 * task.
 *   @return
 * The function returns \a true on success. If \a false is returned then CCP won't be
 * operational and the application should better not start up.
 *   @param[in] tiTillResetToAppInMs
 * The typical operation of the FBL is running only for a short while, to snoop for
 * potential CCP CONNECT requests from a CCP client but to start the flashed application if
 * there's no such request. This is the time in Milliseconds to wait for a connect
 * request. Range is 1..65535ms.\n
 *   If there is no application in flash ROM then this time can be set to 0. A value of
 * zero means the FBL will operated unlimited, no reset will be triggered.
 *   @remark
 * This function depends on the CAN driver CDR, which needs to be initialized before
 * calling this function.
 */
bool ccp_osInitCcpTask(uint16_t tiTillResetToAppInMs)
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

    /* Program the reset counter for launching a flashed application if there is no CCP
       CONNECT request. */
    if(tiTillResetToAppInMs > 0u)
    {
        _stateScheduledReset = stReset_launchApp;
        _tiTillResetInMs = tiTillResetToAppInMs;
    }
    else
    {
        _stateScheduledReset = stReset_unlimitedOperation;
        _tiTillResetInMs = 0u;
    }

    return success;

} /* ccp_osInitCcpTask */


/**
 * Hand over the next CRO command message to the CCP task.
 *   @return
 * The function returns \a true if it recognizes the message as CCP related.
 *   @param[out] pMsgConsumed
 * The function returns by reference the Boolean information whether or not it could
 * consume the CCP message.\n
 *   * \a pMsgConsumed is set to \a false, if the function returns \a true but it couldn't
 * process the message due to an internal buffer overrun event. Most of the time, this
 * points to a protocol error and will terminate the CCP session.\n
 *   For non CCP messages, when the function returns \a false, * \a pMsgConsumed will
 * always be returned as \a false.
 *   @param[in] pRxCanMsg
 * The message to check by reference.
 *   @remark
 * This function is expected to be called from a CAN interrupt, thus asynchronously to the
 * rest of the code, i.e., to the implementation of the CCP state machine in its own task.
 */
bool ccp_osFilterForCcpMsg( bool * const pMsgConsumed
                          , const bsw_rxCanMessage_t * const pRxCanMsg
                          )
{
    if(pRxCanMsg->idxCanBus == CCP_IDX_CAN_BUS_FOR_CCP
       && pRxCanMsg->idxMailbox == CCP_IDX_MAILBOX_FOR_CCP_CRO
      )
    {
        /* We need to copy the message data. The pointer is valid only during the call of
           this function. */
        if(pRxCanMsg->sizeOfPayload == 8u && _ccpFsm.croMsg.isBufferFree)
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
            setCanRxErr();
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
 * Set a timeout value.\n
 *   Use counterpart function checkTimeout() to see it has already elapsed.
 *   @param[in] tiWaitInMs
 * The timeout value in Milliseconds.
 *   @warning The general purpose timeout counter tiWait is applied. Consequently, we can
 * only have one timeout supervision at a time. Conflicting, overlapping use of the global
 * timeout counter can't be diagnosed or recognized but we simply lead to wrong application
 * behavior.
 */
static inline void setTimeout(unsigned int tiWaitInMs)
{
    _ccpFsm.tiWaitInMs = tiWaitInMs;
}


/**
 * Check if the timeout has elapsed, which had been programmed using setTimeout().
 *   @return
 *     @retval to_busy
 * The timeout has not elapsed yet and we still wait for the condition to become true.
 *     @retval to_timeout
 * The timeout has elapsed without the condition becoming true. If you get this return
 * value then the global timeout counter is again available for another purpose.
 *     @retval to_done
 * The condition, we wait for became true before the timeout has elapsed. If you get this
 * return value then the global timeout counter is again available for another purpose.
 *   @param[in] tiWaitInMs
 * The timeout value in Milliseconds.
 */
static enum {to_busy, to_timeout, to_done} checkTimeout(bool conditionToWaitFor)
{
    if(conditionToWaitFor)
    {
        _ccpFsm.tiWaitInMs = 0u;
        return to_done;
    }
    else if(_ccpFsm.tiWaitInMs > 0)
        return to_busy;
    else
        return to_timeout;

} /* checkTimeout */


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

    /* The pending CRO command has completed. Start the session idle timeout to limit the
       time till reception of next CRO. */
    setTimeout(CCP_TI_MAX_SESSION_IDLE_IN_MS);

} /* finalizeDtoMsg */


/**
 * Helper: Read a 32 Bit word from the payload of a CRO message.\n
 *   The word is assumed to have big endian, MSB first.
 *   @param[in] idxFirstByte
 * The index of the MSB in the CRO's payload. Range is 2..4.
 */
static inline uint32_t readU32FromCro(unsigned int idxFirstByte)
{
    assert(idxFirstByte >= 2u && idxFirstByte <= 4u);
    return ((((uint32_t)_ccpFsm.croMsg.payload[idxFirstByte+0] << 8)
             | _ccpFsm.croMsg.payload[idxFirstByte+1]
            ) << 8
            | _ccpFsm.croMsg.payload[idxFirstByte+2]
           ) << 8
           | _ccpFsm.croMsg.payload[idxFirstByte+3];

} /* readU32FromCro */


/**
 * Helper: Write the MTA0 into the DTO message.\n
 *   The address in field \a mta0 is encoded in the response as expected for the DTO of CCP
 * commands PROGRAM, PROGRAM_6, DOWNLOAD and DOWNLOAD_6.
 */
static inline void writeMta0IntoDto(void)
{
    /* Address extension isn't used at all. */
    _ccpFsm.dtoMsg.payload[3] = 0u;
    const uint32_t addr = _ccpFsm.mta0;
    _ccpFsm.dtoMsg.payload[4] = (addr & 0xFF000000u) >> 24;
    _ccpFsm.dtoMsg.payload[5] = (addr & 0x00FF0000u) >> 16;
    _ccpFsm.dtoMsg.payload[6] = (addr & 0x0000FF00u) >> 8;
    _ccpFsm.dtoMsg.payload[7] = (addr & 0x000000FFu) >> 0;
}



/**
 * Check if a memory address range is permitted for programming.\n
 *   Basically, we use method rom_isValidFlashAddressRange from the flash ROM driver to
 * check addresses but there may be additional conditions to consider.\n
 *   The FBL for the MPC5775B/E disallows programming at the first addresses at 0x00800000;
 * here, the MCU's reset logic supports a boot header. If the FBL would allow programming
 * these bytes then an application could be programmed that overruled the FBL so that
 * further erasure and re-progamming became impossible.
 *   @param[in] address
 * The first address of the memory area.
 *   @param[in] size
 * The length of the memory area in Byte.
 */
static bool isValidFlashAddressRangeForProgram(uint32_t address, uint32_t size)
{
#if defined(MCU_MPC5775B) || defined(MCU_MPC5775E)
    /* We don't need a full check, all the rest is done by rom_isValidFlashAddressRange. */
    if(address < 0x00800010u)
        return false;
#endif

    return rom_isValidFlashAddressRange(address, size);

#if defined(MCU_MPC5777C)
# error Implement isValidFlashAddressRangeForProgram for MPC5777C
#endif
} /* isValidFlashAddressRangeForProgram */



/**
 * Check if flash ROM driver is ready to accept next command.\n
 *   This method combines the drivers APIs for readiness for either erasure or programming.
 * It it returns \a true then any new command can be initiates.
 *   @remark
 * Combining the readiness APIs of the driver allows combining some wait states here in the
 * CCP protocol state machine, but it degrades the capabilities of the flash ROM driver in
 * doing certain things in parallel. If we selectively use the original APIs of the driver,
 * then we would have to add some more specific wait states here in this state machine and
 * would become a bit more efficient. The gain is however little. The main effect is that
 * some program data could already be received and written to the driver, while erasure is
 * ongoing. (Which would be beneficial only if we massively increase the number of quad-page
 * buffers in the flash ROM driver.) Complexity of the state machine rises as erasure is no
 * longer a synchronous command; a failure result could be reported only with delay (as it
 * is anyway for CCP PROGRAM commands).
 */
static bool isFlashDriverReady(void)
{
    return rom_osReadyToStartErase() && rom_osReadyToStartProgram();
}


/**
 * When the availability the DISCONNECT comand has been checked, then this function
 * completes the operation.\n
 *   This function is either called immediately on reception of a CCP DISCONNECT command
 * or a bit later, if we first had to wait for the flash ROM driver becoming idle.
 */
static void finishDisconnect(void)
{
    /* As the flash ROM driver is free after completion of all pending activities we may
       return the DTO. */

    /* We still check for a pending fault in the flash ROM driver. (Resulting from an
       earlier command.) We still need to return this information to the CCP client. We do
       this by negative response code. */
    const bool hasDrvFault = rom_osFetchLastError() != rom_err_noError;

    /* Since CCP can't have two sessions with two different ECUs open at a time, the station
       address in the command needs to be ours - otherwise we see a protocol error. The
       reaction is the same in both cases; we close the session. Only the returned response
       code will differ.
         The implementation doesn't support temporary disconnection. If it is requested, we
       fully disconnect but return another response code. */
    const bool isEndOfSession = _ccpFsm.croMsg.payload[2] == 1u;
    const unsigned int stationAddr = ((unsigned)_ccpFsm.croMsg.payload[5] << 8)
                                     + _ccpFsm.croMsg.payload[4];
    unsigned int reponseCode;
    if(isEndOfSession && stationAddr == CCP_STATION_ADDR && !hasDrvFault)
        reponseCode = ccpCmdRespCode_noError;
    else
        reponseCode = ccpCmdRespCode_paramOutOfRange;

    _ccpFsm.mta0 = 0u;
    _ccpFsm.state = ccp_stateFsm_idle;

    #if VERBOSE >= 1
    iprintf("Disconnected from client.\r\n");
    #endif

    finalizeDtoMsg(reponseCode);
    resetAuthentication();

} /* finishDisconnect */


/**
 * The reaction on a received CCP DISCONNECT command.
 */
static void onDisconnect(void)
{
    /* Force last recently written data (if any) be still programmed before we disconnect. */
    rom_osFlushProgramDataBuffer();

    if(isFlashDriverReady())
        finishDisconnect();
    else
    {
        /* If the flash driver is busy then we enter the state to wait for its
           availability. We don't store additional information, all we need to know
           later remains in _ccpFsm and the CRO message buffer. */
        setTimeout(CCP_TI_MAX_WAIT_FLASH_DRV_BUSY_IN_MS);
        _ccpFsm.state = ccp_stateFsm_flashDrvBusy;
    }
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
    const bool isValidAddr = addrExt == 0u
                             && (isServiceResponseAddress(/*isUpload*/ false, addr, 1u)
                                 ||  rom_isValidFlashAddressRange(addr, 1u)
                                     && _stateAuthentication == stKey_authentified
                                );

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
 * When the availability and the correctness of the UPLOAD command have been checked, then
 * this function completes the operation.\n
 *   This function is either called immediately on reception of a CCP UPLOAD command
 * or a bit later, when we first had to wait for the flash ROM driver becoming idle.
 */
static void finishUpload(void)
{
    /* As the flash driver is free we may execute the upload command immediately and return
       the DTO. */
    #if VERBOSE >= 3
    iprintf( "Now uploading %lu Bytes at 0x%08lX.\r\n"
           , _ccpFsm.noBytesToProcess
           , _ccpFsm.mta0
           );
    #endif
    assert(_ccpFsm.noBytesToProcess <= 5);
    memcpy(&_ccpFsm.dtoMsg.payload[3], (const void*)_ccpFsm.mta0, _ccpFsm.noBytesToProcess);
    _ccpFsm.mta0 += _ccpFsm.noBytesToProcess;

    /* We still check for a pending fault in the flash ROM driver. (Resulting from an
       earlier command.) We still need to return this information to the CCP client. We do
       this by negative response code. */
    const rom_errorCode_t errCode = rom_osFetchLastError();
    if(errCode != rom_err_noError)
    {
        #if VERBOSE >= 1
        iprintf( "Upload fails because of err code %u.\r\n", errCode);
        #endif
        finalizeDtoMsg(ccpCmdRespCode_paramOutOfRange);
    }
    else
        finalizeDtoMsg(ccpCmdRespCode_noError);

    _ccpFsm.state = ccp_stateFsm_connected;

} /* finishUpload */


/**
 * The reaction on a received CCP UPLOAD command.
 */
static void onUpload(void)
{
    _ccpFsm.noBytesToProcess = (unsigned)_ccpFsm.croMsg.payload[2];
    const bool isValidAddrRange = isValidFlashAddressRangeForProgram( _ccpFsm.mta0
                                                                    , _ccpFsm.noBytesToProcess
                                                                    )
                                  &&  _stateAuthentication == stKey_authentified
                                  || isServiceResponseAddress( /*isUpload*/ true
                                                             , _ccpFsm.mta0
                                                             , _ccpFsm.noBytesToProcess
                                                             );
    if(_ccpFsm.noBytesToProcess <= 5u && isValidAddrRange)
    {
        /* Potentially, we switch from programming to uploading. This requires flushing a
           possibly pending input buffer. The operation is very cheap. It doesn't matter
           doing it before every upload command. Maintaining and checking a status variable
           would likely cost more. */
        rom_osFlushProgramDataBuffer();

        if(isFlashDriverReady())
            finishUpload();
        else
        {
            /* If the flash driver is busy then we enter the state to wait for its
               availability. We don't store additional information, all we need to know
               later remains in _ccpFsm and the CRO message buffer. */
            setTimeout(CCP_TI_MAX_WAIT_FLASH_DRV_BUSY_IN_MS);
            _ccpFsm.state = ccp_stateFsm_flashDrvBusy;
        }
    }
    else
    {
        #if VERBOSE >= 1
        iprintf( "Uploading %lu Bytes from 0x%08lX is rejected.\r\n"
               , _ccpFsm.noBytesToProcess
               , _ccpFsm.mta0
               );
        #endif
        finalizeDtoMsg(ccpCmdRespCode_paramOutOfRange);
    }
} /* onUpload */


/**
 * When the correctness of and the readiness for the DOWNLOAD comand have been checked,
 * then this function performs the action.\n
 *   This function is either called immediately on reception of a CCP DOWNLOAD command
 * or a bit later, when we first had to wait for the flash ROM driver becoming idle.\n
 *   Note, in contrast to the other functions finishXxx(), this function can't guarantee
 * that the CCP command can be finalized; it may initiate the long lasting authentication
 * and would then enter a wait state for completion of the latter.
 */
static void performDownload(void)
{
    /* As the flash driver is free we may execute the download command immediately. */
    #if VERBOSE >= 3
    iprintf( "Now downloading %lu Bytes to 0x%08lX.\r\n"
           , _ccpFsm.noBytesToProcess
           , _ccpFsm.mta0
           );
    #endif
    assert(_ccpFsm.noBytesToProcess <= 6);
    memcpy((void*)_ccpFsm.mta0, _ccpFsm.pDataToProgram, _ccpFsm.noBytesToProcess);
    _ccpFsm.mta0 += _ccpFsm.noBytesToProcess;
    writeMta0IntoDto();

    /* Currently, our only allowed download is for providing the key in the authentication
       dialog. Therefore, we can directly check here, if the download of the key has been
       completed. Not a nice concept but appropriate for the use case. */
    if(checkForDownloadKey())
    {
        setTimeout(CCP_TI_MAX_WAIT_AUTHENTICATION_IN_MS);
        _ccpFsm.state = ccp_stateFsm_authenticationBusy;
    }
    else
    {
        /* We still check for a pending fault in the flash ROM driver. (Resulting from an
           earlier command.) We still need to return this information to the CCP client. We
           do this by negative response code. */
        finalizeDtoMsg(rom_osFetchLastError() == rom_err_noError
                       ? ccpCmdRespCode_noError
                       : ccpCmdRespCode_paramOutOfRange
                      );
        _ccpFsm.state = ccp_stateFsm_connected;
    }
} /* performDownload */


/**
 * The reaction on a received CCP DOWNLOAD command.
 *   @program[in] isDownload6
 * The same function is used for CCP command DOWNLOAD and DOWNLOAD_6. The argument tells,
 * which actual command to serve.
 */
static void onDownload(bool isDownload6)
{
    if(isDownload6)
    {
        _ccpFsm.noBytesToProcess = 6u;
        _ccpFsm.pDataToProgram = &_ccpFsm.croMsg.payload[2];
    }
    else
    {
        _ccpFsm.noBytesToProcess = (unsigned)_ccpFsm.croMsg.payload[2];
        _ccpFsm.pDataToProgram = &_ccpFsm.croMsg.payload[3];
    }

    /* Our only supported use-case for the download is providing data to some diagnostic or
       action service. */
    const bool isValidAddrRange = isServiceResponseAddress( /*isUpload*/ false
                                                          , _ccpFsm.mta0
                                                          , _ccpFsm.noBytesToProcess
                                                          );
    if(isValidAddrRange  &&  (isDownload6 || _ccpFsm.noBytesToProcess <= 5u))
    {
        if(isFlashDriverReady())
            performDownload();
        else
        {
            /* If the flash driver is busy then we enter the state to wait for its
               availability. We don't store additional information, all we need to know
               later remains in _ccpFsm and the CRO message buffer. */
            setTimeout(CCP_TI_MAX_WAIT_FLASH_DRV_BUSY_IN_MS);
            _ccpFsm.state = ccp_stateFsm_flashDrvBusy;
        }
    }
    else
    {
        #if VERBOSE >= 1
        iprintf( "Downloading %lu Bytes to 0x%08lX is rejected.\r\n"
               , _ccpFsm.noBytesToProcess
               , _ccpFsm.mta0
               );
        #endif
        finalizeDtoMsg(ccpCmdRespCode_paramOutOfRange);
    }
} /* onDownload */


/**
 * When the availability and the correctness of the erase command have been checked, then
 * this function initiates the operation at the flash driver.\n
 *   This function is either called immediately on reception of a CCP CLEAR_MEMORY command
 * or a bit later, when we first had to wait for the flash ROM driver becoming idle.
 */
static void submitClearMemory(void)
{
    /* As the flash driver is free we submit the erase command immediately and wait in
       another state for its completion.
         However, before we submit the erase command, we check for a pending fault in the
       flash ROM driver. (Resulting from an earlier command.) We still need to return this
       information to the CCP client. We do this by rejecting the new command. */
    if(rom_osFetchLastError() == rom_err_noError
       && rom_osStartEraseFlashMemory(_ccpFsm.mta0, _ccpFsm.noBytesToProcess)
      )
    {
        #if VERBOSE >= 1
        iprintf( "Now clearing %lu Bytes at 0x%08lX.\r\n"
               , _ccpFsm.noBytesToProcess
               , _ccpFsm.mta0
               );
        #endif
        setTimeout(CCP_TI_MAX_WAIT_FLASH_DRV_ERASE_IN_MS);
        _ccpFsm.state = ccp_stateFsm_erasing;
    }
    else
    {
        /* Either the flash driver doesn't accept the command or an error had happened in
           the (asynchronusly executed) preceding command. */
        _ccpFsm.state = ccp_stateFsm_connected;
        finalizeDtoMsg(ccpCmdRespCode_paramOutOfRange);
    }
} /* submitClearMemory */


/**
 * The reaction on a received CCP CLEAR_MEMORY command.\n
 *   The command is executed synchronously with respect to the CCP protocol flow. The CCP
 * client will receive the DTO only after completion. There are two blocking states
 * involved. When the command comes in then the flash driver could be still busy (in an
 * asynchronously executed, earlier program command). If it is free, the erase command
 * can be submitted but then we need to wait again for its completion. The CCP state
 * machine uses two pending states to synchronize with these blocking states of the flash
 * driver.
 */
static void onClearMemory(void)
{
    _ccpFsm.noBytesToProcess = readU32FromCro(/*idxFirstByte*/ 2u);
    const bool isValidAddrRange = rom_isValidFlashAddressRange( _ccpFsm.mta0
                                                              , _ccpFsm.noBytesToProcess
                                                              )
                                  &&  _stateAuthentication == stKey_authentified;
    if(isValidAddrRange)
    {
        /* If we had already written some data to program to the API of the flash ROM
           driver, then we force it be completely programmed before we can begin with
           erasing. */
        rom_osFlushProgramDataBuffer();

        if(rom_osReadyToStartErase())
            submitClearMemory();
        else
        {
            /* If the flash driver is busy then we enter the state to wait for its
               availability. We don't store additional information, all we need to know
               later remains in the CRO message buffer. */
            setTimeout(CCP_TI_MAX_WAIT_FLASH_DRV_BUSY_IN_MS);
            _ccpFsm.state = ccp_stateFsm_flashDrvBusy;
        }
    }
    else
    {
        #if VERBOSE >= 1
        iprintf( "Clearing %lu Bytes at 0x%08lX is rejected.\r\n"
               , _ccpFsm.noBytesToProcess
               , _ccpFsm.mta0
               );
        #endif
        finalizeDtoMsg(ccpCmdRespCode_paramOutOfRange);
    }
} /* onClearMemory */


/**
 * When the availability and the correctness of the PROGRAM or PROGRAM_6 command have been
 * checked, then this function initiates the operation at the flash driver.\n
 *   This function is either called immediately on reception of a CCP PROGRAM(_6) command
 * or a bit later, when we first had to wait for the flash ROM driver becoming idle.
 */
static void submitProgram(void)
{
    /* As the flash driver is free we submit the program command immediately and return the
       DTO. The actual programming is running asynchronously from now on in the flash
       driver.
         However, before we submit the next program command, we check for a pending fault
       in the flash ROM driver. (Resulting from an earlier command.) We still need to
       return this information to the CCP client. We do this by rejecting the new command. */
    enum ccpCmdResponseCode_t reponseCode;
    if(rom_osFetchLastError() == rom_err_noError
       && rom_osStartProgram(_ccpFsm.mta0, _ccpFsm.pDataToProgram, _ccpFsm.noBytesToProcess)
      )
    {
        #if VERBOSE >= 3
        iprintf( "Now programming %lu Bytes at 0x%08lX.\r\n"
               , _ccpFsm.noBytesToProcess
               , _ccpFsm.mta0
               );
        #endif

        /* The MTA is updated and the new value echoed in the response message. */
        _ccpFsm.mta0 += _ccpFsm.noBytesToProcess;
        writeMta0IntoDto();

        reponseCode = ccpCmdRespCode_noError;
    }
    else
    {
        /* Either the flash driver doesn't accept the command or an error had happened in
           the (asynchronusly executed) preceding command. */
        reponseCode = ccpCmdRespCode_paramOutOfRange;
    }

    finalizeDtoMsg(reponseCode);
    _ccpFsm.state = ccp_stateFsm_connected;

} /* submitProgram */


/**
 * The reaction on a received CCP PROGRAM or PROGRAM_6 command.\n
 *   The command is executed asynchronously with respect to the CCP protocol flow. The CCP
 * client will receive the DTO after successful submission of the command at the flash
 * driver, which only means that command and data have been buffered in the driver. This
 * leads to a potential blocking state. If the driver's input buffer is full then we wait
 * till the driver is available again. After this wait phase, the DTO is returned.\n
 *   It depends on the speed of programming and CCP data transmission, what will typically
 * happen. If programming is in average faster than CCP data transfer, then we will rarely see
 * the blocking happen and CCP data transfer is running at full speed. If vice versa, then
 * programming will slow down the CCP data transfer to the average rate of programming.
 *   @program[in] isProgram6
 * The same function is used for CCP command PROGRAM and PROGRAM_6. The argument tells,
 * which actual command to serve.
 */
static void onProgram(bool isProgram6)
{
    if(isProgram6)
    {
        _ccpFsm.noBytesToProcess = 6u;
        _ccpFsm.pDataToProgram = &_ccpFsm.croMsg.payload[2];
    }
    else
    {
        _ccpFsm.noBytesToProcess = (unsigned)_ccpFsm.croMsg.payload[2];
        _ccpFsm.pDataToProgram = &_ccpFsm.croMsg.payload[3];
    }
    const bool isValidAddrRange = isValidFlashAddressRangeForProgram( _ccpFsm.mta0
                                                                    , _ccpFsm.noBytesToProcess
                                                                    )
                                  &&  _stateAuthentication == stKey_authentified;
    if(isValidAddrRange  &&  (isProgram6 || _ccpFsm.noBytesToProcess <= 5u))
    {
        if(isFlashDriverReady())
            submitProgram();
        else
        {
            /* If the flash driver is busy then we enter the state to wait for its
               availability. We don't store additional information, all we need to know
               later remains in _ccpFsm and the CRO message buffer. */
            setTimeout(CCP_TI_MAX_WAIT_FLASH_DRV_BUSY_IN_MS);
            _ccpFsm.state = ccp_stateFsm_flashDrvBusy;
        }
    }
    else
    {
        #if VERBOSE >= 1
        iprintf( "Prgramming %lu Bytes at 0x%08lX is rejected.\r\n"
               , _ccpFsm.noBytesToProcess
               , _ccpFsm.mta0
               );
        #endif
        finalizeDtoMsg(ccpCmdRespCode_paramOutOfRange);
    }
} /* onProgram */


/**
 * The reaction on a received CCP DIAG_SERVICE command.
 */
static void onDiagService(void)
{
    /* For both, DIAG_SERVICE and ACTION_SERVICE, the CCP spec (2.1 as of Feb 18, 1999)
       says in the command description that the service number would be 16 Bit but the
       illustrated example on the same page shows it as an 8 Bit value. An I-net query
       clearly stated that the 8 Bit example is correct. */
    const uint8_t serviceNo = _ccpFsm.croMsg.payload[2];

    enum ccpCmdResponseCode_t reponseCode = ccpCmdRespCode_noError;
    uint8_t noResponseBytes;
    if(serviceNo == diagSN_uploadSeed  &&  _stateAuthentication == stKey_noAuthenticationYet)
    {
        /* Choose a new, random seed value. */
        uint32_t seed = stm_osGetSystemTime(/*idxTimer*/ 0u);
        seed ^= (seed << 7);
        seed ^= (seed >> 13);
        seed ^= (seed << 11);
        seed = (seed * 0xA5A5A5A5u) ^ 0x3C6EF372u;
        tds_authenticationData.seed = seed;

        /* Provide the CCP UPLOAD address to the CCP client together with the seed. (The
           upload size is implicit to the chosen crypto algorithm.) */
        tds_authenticationData.addrOfKey = (uint32_t)&tds_authenticationData.keyAry[0];
        _ccpFsm.mta0 = (uint32_t)&tds_authenticationData.seed;
        noResponseBytes = sizeof(tds_authenticationData.seed)
                          + sizeof(tds_authenticationData.addrOfKey);
    }
    else if(serviceNo == diagSN_uploadVersionFbl
            &&  _stateAuthentication == stKey_authentified
           )
    {
        /* The MTA is set to where the response has to be uploaded from. */
        _ccpFsm.mta0 = (uint32_t)&bsw_version[0];
        noResponseBytes = bsw_sizeOfVersion;
    }
    else if((serviceNo == diagSN_resetEcuToFbl  ||  serviceNo == diagSN_resetEcuToApp)
            &&  _stateAuthentication == stKey_authentified
           )
    {
        /* The MTA is invalidated as the service has not response. */
        _ccpFsm.mta0 = 0u;
        noResponseBytes = 0u;

        #if VERBOSE >= 1
        iprintf( "ECU is soon reset by DIAG_SERVICE. %s will be launched after reset.\r\n"
               , serviceNo == diagSN_resetEcuToFbl? "FBL": "Flashed application"
               );
        #endif

        /* Signal the reset and configure a short delay. Switching after flashing to the
           flashed application is in no way time critical, so a delay doesn't harm but
           allows the CCP client to still disconnect. */
        _stateScheduledReset = serviceNo == diagSN_resetEcuToApp
                               ? stReset_launchApp
                               : stReset_launchFbl;

        /* Provide the time to send the DTO, to allow clean DISCONNECT and to transmit the
           console feedback to a potentially connected terminal. */
        _tiTillResetInMs = 200u;
    }
    else
    {
        reponseCode = ccpCmdRespCode_paramOutOfRange;
        noResponseBytes = 0;
    }

    _ccpFsm.dtoMsg.payload[3] = noResponseBytes;

    /* We don't need or support the data type information. */
    _ccpFsm.dtoMsg.payload[4] = 0;

    finalizeDtoMsg(reponseCode);

} /* onDiagService */


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
                /* Stop timeout till reset, if any. */
                _stateScheduledReset = stReset_unlimitedOperation;
                _tiTillResetInMs = 0u;

                #if VERBOSE >= 1
                iprintf("Connected with CCP client.\r\n");
                #endif
                _ccpFsm.mta0 = 0u;
                resetAuthentication();
                _ccpFsm.state = ccp_stateFsm_connected;
                finalizeDtoMsg(ccpCmdRespCode_noError);

                /* If there still is a pending fault of the flash ROM driver (from an
                   earlier session) then we ignore it silently. This situation may happen,
                   if a fault situation let to abortion of the last session. (If it were
                   properly closed then the fault had already been reported latest in the
                   DISCONNECT command.) */
                rom_osFetchLastError();
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
        /* Stop the timeout, which limits the idle time of a session. */
        setTimeout(0u);

        switch(_ccpFsm.croMsg.payload[0])
        {
        case ccpCmd_disconnect:
            onDisconnect();
            break;

        case ccpCmd_setMta:
            onSetMta();
            break;

        case ccpCmd_upload:
            onUpload();
            break;

        case ccpCmd_download:
            onDownload(/*isDownload6*/ false);
            break;

        case ccpCmd_download6:
            onDownload(/*isDownload6*/ true);
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

        case ccpCmd_diagService:
            onDiagService();
            break;

        default:
            #if VERBOSE >= 1
            iprintf("Unsupported CCP CRO command %u received.\r\n", _ccpFsm.croMsg.payload[0]);
            #endif
            finalizeDtoMsg(ccpCmdRespCode_unknownCommand);
            setTimeout(CCP_TI_MAX_SESSION_IDLE_IN_MS);
        }
        break;

    default:
        /* The other states are pending and can't accept a new CCP CRO message. */
        setCanRxErrInternally();

        #if VERBOSE >= 1
        iprintf( "Unexpected CCP CRO command %u received in state %u.\r\n"
               , _ccpFsm.croMsg.payload[0]
               , (unsigned)_ccpFsm.state
               );
        #endif
        /* No break here. */

    case ccp_stateFsm_aborting:
        /* Since we are anyway about to abort the communication because of previous errors,
           we can silently drop the unexpected CRO message. */
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
        iprintf("CCP session is aborted due to CAN Rx/Tx errors.\r\n");
#endif
        setTimeout(CCP_TI_MAX_WAIT_ABORT_FLASH_DRV_BUSY_IN_MS);
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
    case ccpCmd_disconnect:
        finishDisconnect();
        break;

    case ccpCmd_upload:
        finishUpload();
        break;

    case ccpCmd_download:
        performDownload();
        break;

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
    if(_ccpFsm.tiWaitInMs > 0u)
        -- _ccpFsm.tiWaitInMs;

    /* Reset state machine: Trigger a reset if commanded and delay time is elapsed. */
    if(_stateScheduledReset != stReset_unlimitedOperation)
    {
        /* The function doesn't reset immediately; it has a count-down so that we
           give some feedback and be able to properly close the CCP session. */
        if(_tiTillResetInMs > 0u)
            -- _tiTillResetInMs;
        else
        {
            if(_stateScheduledReset == stReset_launchApp)
                swr_osSoftwareReset(SWR_BOOT_FLAG_START_APP);
            else
                swr_osSoftwareReset(SWR_BOOT_FLAG_START_FBL);
        }
    }

    switch(_ccpFsm.state)
    {
    case ccp_stateFsm_aborting:
        /* Try waiting for flash ROM driver being idle again, then abort everything and
           return to state idle. No DTO is sent any more. */
        if(checkTimeout(isFlashDriverReady()) != to_busy)
        {
#if VERBOSE >= 1
            iprintf("CCP session ends now.\r\n");
#endif
            initFsm();
        }
        break;

    case ccp_stateFsm_authenticationBusy:
        /* The validation of the digital signature received as key for authentication is
           ongoing in a low priority background task. */
        {
            const enum tds_taskState_t stAuth = tds_getStateOfVerificationTask();
            if(checkTimeout(stAuth != tds_ts_busy) != to_busy)
            {
                #if VERBOSE >= 2
                if(stAuth == tds_ts_busy)
                    iprintf("Authentication stuck, DOWNLOAD command fails.\r\n");
                #endif
                if(stAuth == tds_ts_verificationOk)
                    _stateAuthentication = stKey_authentified;
                else
                {
                    resetAuthentication();
                    _stateAuthentication = stKey_authenticationFailed;
                }
                _ccpFsm.state = ccp_stateFsm_connected;
                finalizeDtoMsg(_stateAuthentication == stKey_authentified
                               ? ccpCmdRespCode_noError
                               : ccpCmdRespCode_paramOutOfRange
                              );
            }
        }
        break;

    case ccp_stateFsm_flashDrvBusy:
        /* A CCP command is pending. The demanded action (erase, program) could not be
           initiated yet, since the flash ROM driver was still busy. Now check its state
           again and go ahead when eventually free again. However, don't block forever,
           consider a timeout. */
        switch(checkTimeout(isFlashDriverReady()))
        {
        case to_done:
            /* The CRO message buffer still contains the CRO message, which we could
               not process yet due to the flash driver being busy. Now we try the
               evaluation of the message again. */
            reSubmitCroCmd();
            break;
        case to_timeout:
            #if VERBOSE >= 2
            iprintf("Flash driver stuck, CCP command fails.\r\n");
            #endif
            _ccpFsm.state = ccp_stateFsm_connected;
            finalizeDtoMsg(ccpCmdRespCode_overload);
            break;
        default:
            assert(false);
        case to_busy:
            break;
        }

        break;

    case ccp_stateFsm_erasing:
        /* Erasure of flash has been initiated at the flash driver. Now wait for completion.
           However, don't block forever, consider a timeout. */
        switch(checkTimeout(isFlashDriverReady()))
        {
        case to_done:
            /* Erasure completed. Eventually, we can send out the DTO. */
            #if VERBOSE >= 1
            iprintf("Flash erasure completed.\r\n");
            #endif
            /* Note, CLEAR_MEMORY must not increment the MTA. */
            _ccpFsm.state = ccp_stateFsm_connected;
            finalizeDtoMsg(ccpCmdRespCode_noError);
            break;
        case to_timeout:
            #if VERBOSE >= 2
            iprintf("Flash driver stuck in erase, CCP command fails.\r\n");
            #endif
            _ccpFsm.state = ccp_stateFsm_connected;
            finalizeDtoMsg(ccpCmdRespCode_overload);
            break;
        default:
            assert(false);
        case to_busy:
            break;
        }
        break;

    case ccp_stateFsm_connected:
        /* If we are connected but no CRO command is currently in progress, then the
           timeout counter is applied to limit the time, we wait for the next CRO. If it
           elapses then we close the session silently. */
        if(checkTimeout(false) == to_timeout)
        {
            #if VERBOSE >= 1
            iprintf( "No CRO received after %u ms. Session is closed.\r\n"
                   , CCP_TI_MAX_SESSION_IDLE_IN_MS
                   );
            #endif
            setTimeout(CCP_TI_MAX_WAIT_ABORT_FLASH_DRV_BUSY_IN_MS);
            _ccpFsm.state = ccp_stateFsm_aborting;
        }
        break;

    default:
        assert(false);
    case ccp_stateFsm_idle:
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
        iprintf("State %u: CAN communication error.\r\n", (unsigned)_ccpFsm.state);
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

    /* Serve the CCP protocol for download of data and flashing only when the task was
       triggered by CAN Rx event(s). */
    if(noQueuedCcpMsgs > 0u)
    {
        assert(!_ccpFsm.croMsg.isBufferFree);
        onRxCroMsg();
    }

    checkForAsyncEvents();

    if(noTimerTicks1ms > 0u)
    {
        onClockTick();

        /* Run the flash ROM driver from the same task as the CCP protocol. This eliminates
           all race conditions between the two and all commanding and error/result checking
           becomes most easy. */
        rom_osFlashRomDriverMain();
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
            setCanTxErrInternally();
    }
} /* ccp_taskOsRxCcp */
