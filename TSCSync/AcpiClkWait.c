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

int gfErrorCorrection = 1;

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

unsigned GetACPICount(short p)
{
    return _inpd(p);
}

int64_t AcpiClkWait/*pseudo delay upcount*/(int32_t Delay)
{
    static int cnt;
    int64_t  count = Delay;
    int64_t  qwTSCPerIntervall, qwTSCEnd, qwTSCStart;
    size_t eflags = __readeflags();                     // save flaags

    _disable();
    GetACPICount(gPmTmrBlkAddr);

    if (1)
    {
        uint16_t previous, current, diff = 0;

        previous = (uint16_t)GetACPICount(gPmTmrBlkAddr);
        qwTSCStart = __rdtsc();                             // get TSC start

        while (count > 0)
        {
            current = (uint16_t)GetACPICount(gPmTmrBlkAddr);

            if (current >= previous)
                diff = current - previous;
            else
                diff = ~(previous - current) + 1;       //diff = ~(previous - current) + 1;

            previous = current;

            count -= diff;
        }

        qwTSCEnd = __rdtsc();                                                   // get TSC end ~50ms
        
        printf("%lld       ", -count);                          // Additional ticks gone through: 

        //
        // subtract the additional number of TSC gone through
        //
        //         TSCdiff          qwTSCPerIntervall
        //     ---------------- == -------------------      ->
        //      CLKWAIT - count        CLKWAIT
        //
        //
        //                           TSCdiff * CLKWAIT
        //      qwTSCPerIntervall = -------------------
        //                            CLKWAIT - count
        //
        //          NOTE: "count" is negative. " - count " ADDs additional ticks gone through
        //
        if (1 == gfErrorCorrection)
        {
            qwTSCPerIntervall = ((qwTSCEnd - qwTSCStart) * Delay) / (Delay - count);
        }
        else {
            qwTSCPerIntervall = qwTSCEnd - qwTSCStart;
        }

        if (0x200 & eflags)                                     // restore IF interrupt flag
            _enable();

    }
    return (int64_t)qwTSCPerIntervall;
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

    return (int64_t)(qwTSCEnd - qwTSCStart);

}
