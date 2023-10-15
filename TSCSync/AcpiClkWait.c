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

int64_t AcpiClkWait/*pseudo delay upcount*/(uint32_t delay) 
{
#define COUNTER_WIDTH 24
//#define COUNTER_INIT  ((1 << COUNTER_WIDTH) - 1) & (uint32_t) - 5                       /* manipulate to random start counts */
#define COUNTER_DELAY1_REMAINDER(d) (d & (1 << (COUNTER_WIDTH - 2)) - 1)
#define COUNTER_DELAY2_REITERATER        (1 << (COUNTER_WIDTH - 2))
#define COUNTER             pseudotimer & ((1 << COUNTER_WIDTH) - 1)
    //#define COUNTER_ADVANCE     ++pseudotimer, pseudotimer &= ((1 << COUNTER_WIDTH) - 1)
#define COUNTER_ADVANCE     pseudotimer += (0x7 & rand()), pseudotimer &= ((1 << COUNTER_WIDTH) - 1)
#define COUNTER_OVFL_BIT    (COUNTER_WIDTH - 1)
#define COUNTER_OVFL_VAL    (1 << COUNTER_OVFL_BIT)
    ;
    //uint16_t pseudotimer/* simulate the timer count*/ = COUNTER_INIT;
    uint32_t turns = delay >> (COUNTER_WIDTH - 2);
    uint32_t delay1 = COUNTER_DELAY1_REMAINDER(delay);  /* initial / remainder delay */
    uint32_t delay2 = COUNTER_DELAY2_REITERATER;        /* repeated (WIDTH-2 ) delay */
    uint32_t delay12 = delay1;                          /* first turn: delay1, second turn delay2 */
    uint32_t ticks = 0;
    uint32_t cnt = 0;
    uint64_t qwTSCStart, qwTSCEnd;
    size_t eflags = __readeflags();                     // save flaags

    _disable();

    qwTSCStart = __rdtsc();                             // get TSC start
    do 
    {
        ticks = delay12 + (((1 << COUNTER_WIDTH) - 1) & _inpd(gPmTmrBlkAddr));
        while (1) 
        {
            uint32_t diff = ticks - (((1 << COUNTER_WIDTH) - 1) & _inpd(gPmTmrBlkAddr));

            if (diff & COUNTER_OVFL_VAL)
                break;
            cnt++;
        }
        delay12 = delay2;
    } while (turns-- > 0);

    qwTSCEnd = __rdtsc();                               // get TSC end ~50ms

    if (0x200 & eflags)                                 // restore IF interrupt flag
        _enable();

    return (int64_t)(qwTSCEnd - qwTSCStart);
}

void PCIReset(void)
{
    outp(0xCF9, 6);
}