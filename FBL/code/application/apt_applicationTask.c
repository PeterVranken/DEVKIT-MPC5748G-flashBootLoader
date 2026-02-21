/**
 * @file apt_applicationTask.c
 * This file links the APSW to the operating system. It contains the
 * entry points into all the application task functions. A set of (regular) tasks is
 * created by the operating system and may or may not be implemented by the APSW. (If
 * not, an empty default implementation is used.)\n
 *   The init task is for example used to initialize all data structures of the CAN
 * interface.\n
 *   The regularly executed run-time tasks run the CAN interface engine and call the APSW.
 * The CAN interface engine updates all signal values and status in the global CAN API and
 * sends due messages, filled with information read from this API.
 *
 * Copyright (C) 2015-2026 Peter Vranken (mailto:Peter_Vranken@Yahoo.de)
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
 *   bsw_taskUserInit
 *   bsw_taskUser1ms
 *   bsw_taskUser10ms
 *   bsw_taskUser100ms
 *   bsw_taskUser1000ms
 * Local functions
 *   tokenizeCmdLine
 *   version
 *   showW
 *   showC
 *   greeting
 *   help
 */


/*
 * Include files
 */

#include "apt_applicationTask.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "lbd_ledAndButtonDriver.h"
#include "bsw_basicSoftware.h"
#include "f2d_float2Double.h"
#include "sio_serialIO.h"
#include "rtos.h"
#include "stricmp.h"
#include "rom_flashRomDriver.h"

/*
 * Defines
 */

/** Floating point random number with more than 15 Bit resolution; taken fron
    http://www.azillionmonkeys.com/qed/random.html on Jan 23, 2017.
      @remark Caution, the definition make use of a GNU extension and is not portable. */
#define DRAND() ({                                                                  \
    double d;                                                                       \
    do {                                                                            \
       d = (((rand() * RS_SCALE) + rand()) * RS_SCALE + rand()) * RS_SCALE;         \
    } while(d >= 1); /* Round off */                                                \
    d;                                                                              \
})

/** Helper for #DRAND. */
#define RS_SCALE (1.0 / (1.0 + RAND_MAX))

/** A scaled floating point random number in the range [a, b). */
#define RAND(a,b) ((double)((b)-(a))*DRAND()+(double)(a))

/** A Boolean random number with given probability p of getting a true. */
#define BOOL_RAND(p) ((DRAND()<(p))? true: false)


/*
 * Local type definitions
 */


/*
 * Local prototypes
 */


/*
 * Data definitions
 */

/** Simple counter of 1ms application task invokations. Used for timing operations. */
static volatile unsigned int BSS_P1(_cntTask1ms) = 0;

/** Simple counter of 10ms application task invokations. Used for timing operations. */
static volatile unsigned int BSS_P1(_cntTask10ms) = 0;

/** Flag, whether reporting of general status is enabled. */
static volatile bool BSS_P1(_enableReportingStatus) = false;

/** World time: The sum of this variable (unit 1ms) and \a _cntTask1ms (unit 1ms) is the
    world time, represented as number of Milliseconds since beginning of the day. */
static typeof(_cntTask1ms) BSS_P1(_offsetInS) = 0;

/** Status information time output: Counter until next time printing. */
static uint16_t BSS_P1(_cntPrintTime) = 0u;

/** Status information time output: Period of time printing. */
static uint16_t BSS_P1(_tiCycleTimeInS) = 0u;

/** Flag, to let the OS code initiate a restart of the application. 0: No action, 1: SW
    reset. */
uint8_t SDATA_P1(apt_restartApp) = 0u;


/*
 * Function implementation
 */


/**
 * Simple command line parsing. Replace white space in the command line by string
 * termination characters and record the beginnings of the non white space regions.
 *   @param pArgC
 * Prior to call: * \a pArgC is set by the caller to the number of entries available in
 * argV.\n
 *   After return: The number of found arguments, i.e., the number of non white space
 * regions in the command line.
 *   @param argV
 * The vector of arguments, i.e., pointers to the non white space regions in the command
 * line.
 *   @param cmdLine
 * Prior to call: The original command line.\n
 *   After return: White space in the command line is replaced by zero bytes. Note, not
 * necessarily all white space due to the restriction superimposed by \a pArgC.
 */
static void tokenizeCmdLine( unsigned int * const pArgC
                           , const char *argV[]
                           , char * const cmdLine
                           )
{
    char *pC = cmdLine;
    unsigned int noArgsFound = 0;
    while(noArgsFound < *pArgC)
    {
        /* Look for beginning of next argument. */
        while(isspace((int)*pC))
            ++ pC;

        /* Decide if we found a new argument of if we reached the end of the command line. */
        if(*pC != '\0')
        {
            /* New argument found. Record the beginning. */
            argV[noArgsFound++] = pC;

            /* Look for its end. */
            do
            {
                ++ pC;
            }
            while(*pC != '\0'  && !isspace((int)*pC));

            if(*pC != '\0')
            {
                /* There are characters left in the command line. Terminate the found
                   argument and continue with the outer loop. */
                * pC++ = '\0';
            }
            else
            {
                /* Command line has been parsed completely, leave outer loop and return. */
                break;
            }
        }
        else
        {
            /* Command line has been parsed completely, leave outer loop and return. */
            break;

        } /* End if(Further non white space region found?) */

    } /* End while(Still room left in ArgV) */

    *pArgC = noArgsFound;

} /* End of tokenizeCmdLine */


/**
 * Print version designation.
 */
static void version()
{
    fputs(bsw_version, stdout);
    
} /* End of version */


/**
 * GPL proposes 'show w', see http://www.gnu.org/licenses/gpl-3.0.html (downloaded
 * Oct 27, 2017)
 */
static void showW()
{
    version();
    
    static const char RODATA(gplShowW)[] =
    "GNU LESSER GENERAL PUBLIC LICENSE\r\n"
    "\r\n"
    "Version 3, 29 June 2007\r\n"
    "\r\n"
    "(...)\r\n"
    "\r\n"
    "15. Disclaimer of Warranty.\r\n"
    "\r\n"
    "THERE IS NO WARRANTY FOR THE PROGRAM, TO THE EXTENT PERMITTED BY\r\n"
    "APPLICABLE LAW. EXCEPT WHEN OTHERWISE STATED IN WRITING THE COPYRIGHT\r\n"
    "HOLDERS AND/OR OTHER PARTIES PROVIDE THE PROGRAM \"AS IS\" WITHOUT\r\n"
    "WARRANTY OF ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING, BUT NOT\r\n"
    "LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A\r\n"
    "PARTICULAR PURPOSE. THE ENTIRE RISK AS TO THE QUALITY AND PERFORMANCE OF\r\n"
    "THE PROGRAM IS WITH YOU. SHOULD THE PROGRAM PROVE DEFECTIVE, YOU ASSUME\r\n"
    "THE COST OF ALL NECESSARY SERVICING, REPAIR OR CORRECTION.\r\n";

    fputs(gplShowW, stdout);

} /* End of showW */



/**
 * GPL proposes 'show c', see http://www.gnu.org/licenses/gpl-3.0.html (downloaded
 * Oct 27, 2017)
 */
static void showC()
{
    version();
    
    static const char RODATA(gplShowC)[] =
    "This program is free software: you can redistribute it and/or modify\r\n"
    "it under the terms of the GNU Lesser General Public License as published\r\n"
    "by the Free Software Foundation, either version 3 of the License, or\r\n"
    "(at your option) any later version.\r\n"
    "\r\n"
    "This program is distributed in the hope that it will be useful,\r\n"
    "but WITHOUT ANY WARRANTY; without even the implied warranty of\r\n"
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\r\n"
    "GNU Lesser General Public License for more details.\r\n"
    "\r\n"
    "You should have received a copy of the GNU Lesser General Public License\r\n"
    "along with this program.  If not, see <https://www.gnu.org/licenses/>.\r\n";

    fputs(gplShowC, stdout);

} /* End of showC */



/**
 * Print program greeting.
 */
static void greeting()
{
    version();

    static const char RODATA(greeting)[] =
    "This program comes with ABSOLUTELY NO WARRANTY; for details type `show w'.\r\n"
    "This is free software, and you are welcome to redistribute it under certain\r\n"
    "conditions; type `show c' for details.\r\n";

    fputs(greeting, stdout);
    
} /* End of greeting */


/**
 * Print usage text.
 */
static void help()
{
    version();

    static const char RODATA(help)[] =
    "Type:\r\n"
    "help: Get this help text\r\n"
    "show c, show w: Show details of software license\r\n"
    "show CPU [on|off]: Enable/disable reporting of average CPU load\r\n"
    "show stack: Print once the current stack reserve of all processes\r\n"
    "show errors: Print once the total number of process errors recorded in the kernel\r\n"
    "show time [tiCycleInS]: Enable/disable regular display of current time\r\n"
#if ROM_TEST_BUILD_WITH_ERROR_INJECTION == 1
    "error: Inject an error in the current flash ROM task. For testing only\r\n"
#endif
    "version: Print software version designation\r\n"
    "time: Print current time\r\n"
    "time hour min [sec]: Set current time\r\n"
    "reset: A SW reset is triggered by writing to MC_ME_MCTL[KEY]\r\n";

    fputs(help, stdout);

} /* End of help */



/**
 * The initialization task of process bsw_pidUser, the QM aaplication code process. The task is
 * called at a time when no parallel thread execution is done yet, so no race conditions
 * exist with other parts of the code and safe build-up of all required data structure can
 * be done prior to the start of the real task scheduling.\n
 *   The initialization function of QM process bsw_pidUser is invoked prior to the
 * according initialization function of the safety process.\n
 *   We use this function to initialize the CAN interface.
 *   @return
 * The function may return a negative value to indicate a severe problem. The system won't
 * start up in this case. Normally, we will return zero.
 *   @param PID
 * The process ID of the initialized process, always bsw_pidUser in our case.
 */
int32_t bsw_taskUserInit(uint32_t PID ATTRIB_DBG_ONLY)
{
    assert(PID == bsw_pidUser);

    bool success = true;
    
    /* Print initial hello. */
    greeting();
    
    return success? 0: -1;

} /* End of bsw_taskUserInit */




/**
 * Format the current time in printable format.
 *   @param msgTime
 * The time is written into this character string. It has room for \a sizeOfMsgTime
 * characters, which includes the terminating zero byte.
 *   @param sizeOfMsgTime
 * A value of 9 is suffcient to hold any possible time designation.
 */
void apt_printCurrTime(char msgTime[], unsigned int sizeOfMsgTime)
{
    /* Current time in seconds since beginning of day. */
    const unsigned int noMillis = _cntTask1ms;
    unsigned int noSec = noMillis / 1000u + _offsetInS;

    /* Avoid expensive modulo. */
    if(noSec >= 86400u)
    {
        noSec -= 86400u;
        _offsetInS -= 86400u;
    }
    assert(noSec < 86400u);

    /* Split current time in hour, minute and second of day. */
    unsigned int h, m, s;
    h = noSec / 3600u;
    m = s = noSec - h*3600u;
    m /= 60u;
    s -= m*60u;

    snprintf(msgTime, sizeOfMsgTime, "%02u:%02u:%02u", h, m, s);

} /* apt_printCurrTime */


/**
 * Print the current time.
 */
static void printCurrTime(void)
{
    char msgTime[9];
    apt_printCurrTime(msgTime, sizeof(msgTime));
    iprintf("Current time is %s\r\n", msgTime);

} /* printCurrTime */


/**
 * Demo code of the regularly executed 1ms task of the APSW.
 *   @return
 * Normally, the function will return zero. However, it may return a negative value to
 * indicate a severe problem. The system would count a process error and a safety
 * supervisor task could take an action.
 *   @param PID
 * The process ID of the process, the task belongs to; always bsw_pidUser in our case.
 *   @param taskParam
 * A task receives an argument. It is always zero in our case.
 */
int32_t bsw_taskUser1ms(uint32_t PID ATTRIB_DBG_ONLY, uint32_t taskParam ATTRIB_UNUSED)
{
    assert(PID == bsw_pidUser);

    ++ _cntTask1ms;
    
    return 0;

} /* End of bsw_taskUser1ms */




/**
 * Demo code of the regularly executed 10ms task of the APSW. The engine of the CAN
 * interface is clocked and the decoded and pre-processed CAN information can be consumed
 * by the APSW. Vice versa, the APSW provided functional results are encoded and timely
 * sent by the engine. All data access can be done easy and directly since no race
 * conditions occur.
 *   @return
 * Normally, the function will return zero. However, it may return a negative value to
 * indicate a severe problem. The system would count a process error and a safety
 * supervisor task could take an action.
 *   @param PID
 * The process ID of the process, the task belongs to; always bsw_pidUser in our case.
 *   @param taskParam
 * A task receives an argument. It is always zero in our case.
 */
int32_t bsw_taskUser10ms(uint32_t PID ATTRIB_DBG_ONLY, uint32_t taskParam ATTRIB_UNUSED)
{
    assert(PID == bsw_pidUser);

    ++ _cntTask10ms;
    
    if(_cntPrintTime > 0u)
    {
        if(--_cntPrintTime == 0u)
        {
            printCurrTime();
            _cntPrintTime = _tiCycleTimeInS;
        }
    }

    /* Look for possible user input through serial interface. */
    static unsigned int DATA_P1(cntIdleLoops_) = 2800;
    char inputMsg[80+1];
    if(sio_getLine(inputMsg, sizeOfAry(inputMsg)) != NULL)
    {
        const char *argV[11];
        unsigned int argC = sizeOfAry(argV);
        tokenizeCmdLine(&argC, argV, inputMsg);
        bool didNotUnderstand = false;
        if(argC >= 1)
        {
            if(stricmp(argV[0], "show") == 0  &&  argC >= 2)
            {
                if(stricmp(argV[1], "c") == 0)
                    showC();
                else if(stricmp(argV[1], "w") == 0)
                    showW();
                else if(stricmp(argV[1], "stack") == 0)
                {
                    /* Report the stack sizes now and once. */
                    iprintf( "Stack reserve in Byte:\r\n"
                             "  Operating system:  %hu\r\n"
                             "  APSW (QM process): %hu\r\n"
                             "  Safety process:    %hu\r\n"
                           , bsw_stackReserveProcessOS
                           , bsw_stackReserveProcessAPSW
                           , bsw_stackReserveProcessSafety
                           );
                }
                else if(stricmp(argV[1], "CPU") == 0)
                {
                    const bool isOn = argC == 2u  || stricmp(argV[2], "off") != 0u;
                    _enableReportingStatus = isOn;
                }
                else if(stricmp(argV[1], "errors") == 0)
                {
                    /* Report the event losses, which mostly means task overruns. */
                    // TODO We need proper export of the process and event IDs by the BSW.
                    iprintf( "Event losses:\r\n"
                             "  Tasks 1ms:   %u\r\n"
                             "  Tasks 10ms:  %u\r\n"
                             "  Tasks 100ms: %u\r\n"
                             "  Tasks 1s:    %u\r\n"
                             "  Task CCP:    %u\r\n"
                           , rtos_getNoActivationLoss(0u)
                           , rtos_getNoActivationLoss(1u)
                           , rtos_getNoActivationLoss(2u)
                           , rtos_getNoActivationLoss(3u)
                           , rtos_getNoActivationLoss(4u)
                           );

                    /* Report process errors. */
                    iprintf( "Exceptions:\r\n"
                             "  QM (APSW) process:   %u\r\n"
                             "  Safety process:      %u\r\n"
                           , rtos_getNoTotalTaskFailure(/*pidAPSW*/   1u)
                           , rtos_getNoTotalTaskFailure(/*pidSafety*/ 2u)
                           );
                }
                else if(strcmp(argV[1], "time") == 0)
                {
                    /* Regular output of the current time: Omitted argument means "on",
                       value 0 (including non-numbers) means "off". */
                    const signed int tiCycleInS = argC == 2u? 1: atoi(argV[2]);
                    if(tiCycleInS < 0)
                        _tiCycleTimeInS = 0u;
                    else if(100*tiCycleInS <= UINT16_MAX)
                        _tiCycleTimeInS = (uint16_t)(100*tiCycleInS);
                    else
                        _tiCycleTimeInS = 65500u;

                    _cntPrintTime = _tiCycleTimeInS > 0u? 1u: 0u;
                }
            }
            else if(stricmp(argV[0], "help") == 0)
                help();
            else if(stricmp(argV[0], "version") == 0)
                version();
#if ROM_TEST_BUILD_WITH_ERROR_INJECTION == 1
            else if(stricmp(argV[0], "error") == 0)
                rom_lastError = rom_err_invalidErrorCode;
#endif
            else if(stricmp(argV[0], "time") == 0)
            {
                if(argC >= 3)
                {
                    signed int i = atoi(argV[1]);
                    if(i < 0)
                        i = 0;
                    else if(i >= 24)
                        i = 23;
                    _offsetInS = (unsigned)i * 3600u;

                    i = atoi(argV[2]);
                    if(i < 0)
                        i = 0;
                    else if(i >= 60)
                        i = 59;
                    _offsetInS += (unsigned)i * 60u;

                    /* Designation of seconds is an option only. */
                    if(argC >= 4)
                    {
                        i = atoi(argV[3]);
                        if(i < 0)
                            i = 0;
                        else if(i >= 60)
                            i = 59;
                        _offsetInS += (unsigned)i;
                    }
                    assert(_offsetInS < 86400);

                    /* Consider current system, which we don't want to reset. */
                    _offsetInS -= _cntTask1ms / 1000;
                }

                printCurrTime();
                _cntPrintTime = _tiCycleTimeInS;
            }
            else if(strcmp(argV[0], "reset") == 0)
            {
                apt_restartApp = 1u;
            }
            else
            {
                didNotUnderstand = true;

            } /* End if/else if(Command) */

            /* Reset the timer for output of help if user has entered something. */
            cntIdleLoops_ = 0;
        }
        else
        {
            didNotUnderstand = true;
            
        } /* End if(User input contains possible command) */
        
        if(didNotUnderstand)
        {
            /* Echo bad user input, which could not be consumed. */
            fputs("I couldn't understand you. You typed:", stdout);
            fflush(stdout);

            unsigned int u;
            for(u=0; u<argC; ++u)
            {
                fputs(" ", stdout);
                fputs(argV[u], stdout);
            }
            fputs("\r\nTry `help'\r\n", stdout);
        }
    }
    else
    {
        /* Offer help after 30s of no input. */
        if(++cntIdleLoops_ >= 3000)
        {
            fputs("Type `help' to get software usage information\r\n", stdout);
            cntIdleLoops_ = 0;
        }

    } /* if(Got user input?) */

    return 0;

} /* End of bsw_taskUser10ms */




/**
 * Demo code of the regularly executed 100ms task of the APSW.
 *   @return
 * Normally, the function will return zero. However, it may return a negative value to
 * indicate a severe problem. The system would count a process error and a safety
 * supervisor task could take an action.
 *   @param PID
 * The process ID of the process, the task belongs to; always bsw_pidUser in our case.
 *   @param taskParam
 * A task receives an argument. It is always zero in our case.
 */
int32_t bsw_taskUser100ms(uint32_t PID ATTRIB_DBG_ONLY, uint32_t taskParam ATTRIB_UNUSED)
{
    assert(PID == bsw_pidUser);

    return 0;

} /* End of bsw_taskUser100ms */




/**
 * Demo code of the regularly executed 1s task of the APSW.
 *   @return
 * Normally, the function will return zero. However, it may return a negative value to
 * indicate a severe problem. The system would count a process error and a safety
 * supervisor task could take an action.
 *   @param PID
 * The process ID of the process, the task belongs to; always bsw_pidUser in our case.
 *   @param taskParam
 * A task receives an argument. It is always zero in our case.
 */
int32_t bsw_taskUser1000ms(uint32_t PID ATTRIB_DBG_ONLY, uint32_t taskParam ATTRIB_UNUSED)
{
    assert(PID == bsw_pidUser);

    static bool SDATA_P1(isOn_) = false;
    lbd_setLED(lbd_led_7_DS4, isOn_=!isOn_);

    /* Report CPU load. */
    static uint8_t SDATA_P1(cntCycles_) = 0u;
    static uint16_t SDATA_P1(maxCpuLoad_) = 0u;
    const uint16_t cpuLoad = bsw_cpuLoad;
    if(_enableReportingStatus && (cntCycles_ % 4u) == 3u)
    {
        printf("CPU load: %.1f%%\r\n", f2d((float)maxCpuLoad_ / 10.0f));
        maxCpuLoad_ = cpuLoad;
    }
    else if(cpuLoad > maxCpuLoad_)
        maxCpuLoad_ = cpuLoad;

    ++ cntCycles_;

    return 0;
    
} /* End of bsw_taskUser1000ms */
