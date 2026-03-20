#ifndef TDS_TASKDIGSIGNATURE_INCLUDED
#define TDS_TASKDIGSIGNATURE_INCLUDED
/**
 * @file tds_taskDigSignature.h
 * Definition of global interface of module tds_taskDigSignature.c
 *
 * Copyright (C) 2026 FEV.io GmbH, Germany (mailto:vranken@fev.io)
 *
 * All rights reserved. Reproduction in whole or in part is prohibited without the written
 * consent of the copyright owner.
 */

/*
 * Include files
 */

#include "typ_types.h"


/*
 * Defines
 */

/** The number of bytes of a correct authentication key. */
#define TDS_SIZE_OF_AUTHENTICATION_KEY  64u

/** The ID of the RTOS event processor, which we notify to demand the verification of a
    digital signature. */
#define CCP_ID_EV_PROC_DIG_SIGNATURE    5u

/*
 * Global type definitions
 */

/** The data for the authentication requires careful binary buildup. We use a mixture of
    union and static assertions to ensure correct sizes and alignments. This is required:
      - The signature (aka "key" of the seed-and-key procedure) of the message needs to be
    gapfree followed by the signed message (aka "seed" of the seed-and-key procedure).
      - The message needs to be 4-Byte aligned uint32_t words. This is useful for easy
    generation of the seed.
      - The message needs to be gapfree followed by the uint32_t word holding the address
    of the signature. This is required to make seed and CCP download address for the key
    accessible to a single CCP download of 8 Byte. */
extern struct tds_authenticationData_t
{
    /** An anonymous union allows to ensure the 4 Byte alignment even for the uin8_t buffer
        for the signature (aka key). */
    union
    {
        /** The 64 Byte of the signature, which is received from from CCP client. */
        uint8_t keyAry[TDS_SIZE_OF_AUTHENTICATION_KEY];

        /** This is not really used but needed to ensure correct alignment. */
        uint32_t keyAry_u32[TDS_SIZE_OF_AUTHENTICATION_KEY/4];
    };

    /** The message (aka seed), which is sent to the CCP client. */
    uint32_t seed;

    /** The address of authenticationKeyAry, here provided for UPLOAD towards the CCP
        client. */
    uint32_t addrOfKey;

} tds_authenticationData;


/*
 * Global data declarations
 */


/*
 * Global prototypes
 */

/* Check for completion of the verification of the digital signature. */
enum tds_taskState_t { tds_ts_idle
                     , tds_ts_busy
                     , tds_ts_verificationOk
                     , tds_ts_verificationFailed } tds_getStateOfVerificationTask(void);

/* Start the verification of the signature. */
bool tds_osStartVerificationOfSignature(void);

/* Clear the buffer with the authentification data, i.e., seed and key. */
void tds_osResetAuthenticationData(void);

/* Check the progress of the verification of the digital signature. */
enum tds_taskState_t tds_getStateOfVerificationTask(void);

/* The OS task, which verifies the signature in the authentication process. */
int32_t tds_taskDigitalSignature( uint32_t PID ATTRIB_DBG_ONLY
                                , uint32_t taskParam ATTRIB_UNUSED
                                );

/*
 * Global inline functions
 */


#endif  /* TDS_TASKDIGSIGNATURE_INCLUDED */
