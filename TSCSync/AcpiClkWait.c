/*++

    TSCSync
    https://github.com/KilianKegel/Visual-TSCSync-for-UEFI-Shell

    Copyright (c) 2017-2023, Kilian Kegel. All rights reserved.
    SPDX-License-Identifier: GNU General Public License v3.0

Module Name:

    AcpiClkWait.c

Abstract:

    TSCSync - TimeStampCounter (TSC) synchronizer,  analysing System Timer characteristics
    ACPI Timer wait

Author:

    Kilian Kegel

--*/
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <conio.h>
#include <intrin.h>

uint16_t gPmTmrBlkAddr;
int32_t pseudotimer;
int32_t pseudotimer2;

uint32_t gCOUNTER_WIDTH = 24;

////#define COUNTER_INIT  ((1 << gCOUNTER_WIDTH) - 1) & (uint32_t) - 5                       /* manipulate to random start counts */
//#define COUNTER_DELAY1_REMAINDER(d) (d & (1 << (gCOUNTER_WIDTH - 2)) - 1)
//#define COUNTER_DELAY2_REITERATER        (1 << (gCOUNTER_WIDTH - 2))
//#define COUNTER             pseudotimer & COUNTER_MASK
//    //#define COUNTER_ADVANCE     ++pseudotimer, pseudotimer &= ((1 << gCOUNTER_WIDTH) - 1)
//#define COUNTER_ADVANCE     pseudotimer += (0x7 & rand()), pseudotimer &= ((1 << gCOUNTER_WIDTH) - 1)
//#define COUNTER_OVFL_BIT    (gCOUNTER_WIDTH - 1)
//#define COUNTER_OVFL_VAL    (1 << COUNTER_OVFL_BIT)

int32_t GetACPICount(short p)
{
    uint32_t COUNTER_MASK = ((1 << gCOUNTER_WIDTH) - 1);
    //int32_t inc =  3 & rand();
    //
    //pseudotimer += inc;
    //pseudotimer2+= inc;

    //pseudotimer = COUNTER;

    //return pseudotimer;

    return COUNTER_MASK & _inpd(p);
}

int64_t AcpiClkWait/*pseudo delay upcount*/(int32_t delay) 
{
    uint32_t COUNTER_MASK = ((1 << gCOUNTER_WIDTH) - 1);
    uint32_t COUNTER_MASK2 = (((1 << gCOUNTER_WIDTH) - 1) / 2);
    //uint16_t pseudotimer/* simulate the timer count*/ = COUNTER_INIT;
    int32_t times = delay / COUNTER_MASK2;
    int32_t remainder = delay % COUNTER_MASK2;

    int32_t current = GetACPICount(gPmTmrBlkAddr);
    int32_t ticks = (remainder + current) - 1;

    int32_t cntstart = pseudotimer2;
    int32_t diff;
    uint64_t qwTSCStart, qwTSCEnd;
    size_t eflags = __readeflags();                     // save flaags

    _disable();

    qwTSCStart = __rdtsc();                             // get TSC start
    
    do 
    {
        while (1) 
        {
            diff = ticks - current;

            current = GetACPICount(gPmTmrBlkAddr);

            if (0 != (diff & (1 << (gCOUNTER_WIDTH - 1))))
            {
                if (diff > 0) 
                {
                    diff -= COUNTER_MASK;
                    diff = ((uint32_t)diff - 1);
                }

                break;
            }
            
        }

        ticks = (COUNTER_MASK2 - 1 + diff + current);

    } while (times-- > 0);

    qwTSCEnd = __rdtsc();                               // get TSC end ~50ms

    if (0x200 & eflags)                                 // restore IF interrupt flag
        _enable();
    //return pseudotimer2 - cntstart;
    return (int64_t)(qwTSCEnd - qwTSCStart);
}

void PCIReset(void)
{
    outp(0xCF9, 6);
}

/**
  Stalls the CPU for at least the given number of ticks.

  Stalls the CPU for at least the given number of ticks. It's invoked by
  MicroSecondDelay() and NanoSecondDelay().

  @param  Delay     A period of time to delay in ticks.

**/
int64_t InternalAcpiDelay(uint32_t  Delay)
{
    uint32_t BIT22 = (1 << (gCOUNTER_WIDTH - 2));
    uint32_t BIT23 = (1 << (gCOUNTER_WIDTH - 1));
    uint32_t    Ticks;
    uint32_t    Times;
    uint64_t qwTSCStart, qwTSCEnd;
    size_t eflags = __readeflags();                     // save flaags

    Times = Delay >> 22;
    Delay &= BIT22 - 1;

    _disable();

    qwTSCStart = __rdtsc();                             // get TSC start

    do {
        //
        // The target timer count is calculated here
        //
        Ticks = GetACPICount(gPmTmrBlkAddr) + Delay;
        Delay = BIT22;
        //
        // Wait until time out
        // Delay >= 2^23 could not be handled by this function
        // Timer wrap-arounds are handled correctly by this function
        //
        while (((Ticks - GetACPICount(gPmTmrBlkAddr)) & BIT23) == 0)
        {
            ;
        }

    } while (Times-- > 0);

    qwTSCEnd = __rdtsc();                               // get TSC end ~50ms

    if (0x200 & eflags)                                 // restore IF interrupt flag
        _enable();

//    return 0;
    return (int64_t)(qwTSCEnd - qwTSCStart);

}
