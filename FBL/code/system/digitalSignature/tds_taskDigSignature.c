/**
 * @file tds_taskDigSignature.c
 * A task at low priority, which executes the really long calculating TweedNaCl
 * code in the background.
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
 *   tds_osStartVerificationOfSignature
 *   tds_getStateOfVerificationTask
 *   tds_taskDigitalSignature
 * Local functions
 */

/*
 * Include files
 */

#include "tds_taskDigSignature.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "bsw_basicSoftware.h"
#include "rtos.h"
#include "tweetnacl.h"
#ifdef DEBUG
# include "stm_systemTimer.h"
#endif

/*
 * Defines
 */
 
/** Development support: Set verbosity. If not zero then more or less information is
    written to the console. Needs to be zero for productive use. */
#define VERBOSE     1

/** A full memory barrier, which we use to ensure that the volatile data-valid flag is
    toggled only after all data processing has completed. */
#define MEMORY_BARRIER_FULL()   {atomic_thread_fence(memory_order_seq_cst);}

/*
 * Local type definitions
 */
 
 
/*
 * Local prototypes
 */
 
 
/*
 * Data definitions
 */

/** The data for the authentication requires careful binary buildup. */
struct tds_authenticationData_t DATA_OS(tds_authenticationData);

_Static_assert( TDS_SIZE_OF_AUTHENTICATION_KEY % 4u == 0u
                &&  offsetof(struct tds_authenticationData_t, keyAry)
                    == offsetof(struct tds_authenticationData_t, keyAry_u32)
                &&  offsetof(struct tds_authenticationData_t, seed)
                    == TDS_SIZE_OF_AUTHENTICATION_KEY
                &&  offsetof(struct tds_authenticationData_t, addrOfKey)
                    == TDS_SIZE_OF_AUTHENTICATION_KEY + 4u
                &&  (offsetof(struct tds_authenticationData_t, seed) & 03u) == 0u
              , "Bad modelling of authentication data buffer"
              );

/** The public key of the authentication procedure. */
static const uint8_t RODATA(_publicKey)[32] =
{ 0x03u, 0xA1u, 0x07u, 0xBFu, 0xF3u, 0xCEu, 0x10u, 0xBEu,
  0x1Du, 0x70u, 0xDDu, 0x18u, 0xE7u, 0x4Bu, 0xC0u, 0x99u,
  0x67u, 0xE4u, 0xD6u, 0x30u, 0x9Bu, 0xA5u, 0x0Du, 0x5Fu,
  0x1Du, 0xDCu, 0x86u, 0x64u, 0x12u, 0x55u, 0x31u, 0xB8u,
};

/** The event, which signals completion of the verification of the signature. */
static volatile enum tds_taskState_t DATA_P1(_stateVerificationTask) = tds_ts_idle;

#ifdef DEBUG
/** For debugging only: The time it takes to calculate the validity of the signature. This
    is the elapsed real-world time, not the CPU time. Unit is 200ns. */
uint32_t tds_tiSignVerify = 0;
#endif

/*
 * Function implementation
 */

/**
 * Start the verification of the signature.\n
 *   This call activates the background task to do the calculation. Pre-requistes of
 * calling this function is:\n
 * - Previous calls of terminated. tds_getStateOfVerificationTask() had been called until
 *   this function had signalled either \a tds_ts_verificationOk or \a
 *   tds_ts_verificationFailed. Overlapping calls of the activation task are not supported,
 *   undefined behavior results.\n
 * - There is only one caller of this method, or different callers are strictly synchronized
     outside of this implementation. Re-entrancy is no granted.
 * - All required data for the verification, seed and key (aka digital signature), has been
 *   written to the global data struture \a tds_authenticationData.
 * - The caller needs to have the priviledges to activate a task using rtos_osSendEvent().
 *   @return
 * Get \a true if the verification task could be started. This will always be the case if
 * the pre-requisites have been considered. \a false will mainly happen, if
 * tds_getStateOfVerificationTask() had not been polled till termination, e.g., due to a
 * timeut condition.
 */
bool tds_osStartVerificationOfSignature(void)
{   
    bool success = _stateVerificationTask == tds_ts_idle;
    if(success)
    {
        _stateVerificationTask = tds_ts_busy;
        MEMORY_BARRIER_FULL();
        success = rtos_osSendEvent(CCP_ID_EV_PROC_DIG_SIGNATURE, /*taskParam*/ 0u);
    }
    return success;
    
} /* tds_osStartVerificationOfSignature */


/**
 * Check the event, which signals the completion of the verification of the digital
 * signature.
 *   @return
 * Get \a tds_ts_verificationOk or \a tds_ts_verificationFailed if the verification task
 * has completed. Other return value are meaningless, no result is available yet.
 */
enum tds_taskState_t tds_getStateOfVerificationTask(void)
{
    const enum tds_taskState_t result = _stateVerificationTask;
    
    /* The event is automatically reset, when it signals task completion. Note, although
       the event flag is written from two different task, we never have a race condition
       (unless the caller violates the rules of using
       tds_osStartVerificationOfSignature()). The verification task is a single-shot task
       and once it has set the flag to completion, it'll never touch it again. The caller
       can safely do a read-modify-write in this case. */
    if(result == tds_ts_verificationOk  ||  result == tds_ts_verificationFailed)
    {
        /* The verification task has terminated and it is allowed to start the next one on
           demand. */
        _stateVerificationTask = tds_ts_idle;
    }
    
    return result;
    
} /* tds_getStateOfVerificationTask */


/**
 * The OS task, which is activated, when the CCP authentication process has received the
 * digital signature. The task will then take the time to verify the signature. By
 * experience, this takes more than 600ms!
 *   @return
 * Normally, the function will return zero. However, it may return a negative value to
 * indicate a severe problem. The system would count a process error and a safety
 * supervisor task could take an action.
 *   @param PID
 * The ID of the process, this task executes in.
 *   @param taskParam
 * The task doesn't make use of the task parameter.
 */
int32_t tds_taskDigitalSignature( uint32_t PID ATTRIB_DBG_ONLY
                                , uint32_t taskParam ATTRIB_UNUSED
                                )
{
    assert(PID == bsw_pidUser);
    #if VERBOSE >= 1
    iprintf("Task digitalSignature has been activated.\r\n");
    #endif
    
    if(_stateVerificationTask != tds_ts_busy)
        return -1;

    MEMORY_BARRIER_FULL();
    #ifdef DEBUG
    tds_tiSignVerify = stm_getSystemTime(1 /*200ns*/);
    #endif
    const bool keyOk = crypto_sign_verify( &tds_authenticationData.keyAry[0]
                                         , TDS_SIZE_OF_AUTHENTICATION_KEY
                                           + sizeof(tds_authenticationData.seed)
                                         , _publicKey
                                         );
    #ifdef DEBUG
    tds_tiSignVerify = stm_getSystemTime(1 /*200ns*/) - tds_tiSignVerify;
    #endif
    if(keyOk)
    {
        #if VERBOSE >= 1
        iprintf("Authentication succeeded.\r\n");
        #endif
        _stateVerificationTask = tds_ts_verificationOk;
    }
    else
    {
        #if VERBOSE >= 1
        iprintf("Authentication failed.\r\n");
        #endif
        _stateVerificationTask = tds_ts_verificationFailed;
    }
    #if VERBOSE >= 1
    iprintf("Task digitalSignature terminates.\r\n");
    #endif
    
    return 0;

} /* tds_taskDigitalSignature */
