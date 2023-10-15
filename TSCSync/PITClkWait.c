/*++

    TSCSync
    https://github.com/KilianKegel/Visual-TSCSync-for-UEFI-Shell

    Copyright (c) 2017-2023, Kilian Kegel. All rights reserved.
    SPDX-License-Identifier: GNU General Public License v3.0

Module Name:

    PITClkWait.c

Abstract:

    TSCSync - TimeStampCounter (TSC) synchronizer,  analysing System Timer characteristics
    PIT i8254 Timer wait

Author:

    Kilian Kegel

--*/
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <conio.h>
#include <intrin.h>

int64_t PITClkWait/*pseudo delay upcount*/(uint32_t delay) {
#define COUNTER_WIDTH 16
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
    uint8_t counterLoHi[2];
    uint16_t* pwCount = (uint16_t*)&counterLoHi[0];
    uint64_t qwTSCStart, qwTSCEnd;
    size_t eflags = __readeflags();                     // save flaags

    qwTSCStart = __rdtsc();                             // get TSC start

    do 
    {
        outp(0x43, (2/*TIMER*/ << 6) + 0x0);                            // counter latch timer 2
        counterLoHi[0] = (unsigned char)inp(0x40 + 2/*TIMER*/);         // get low byte
        counterLoHi[1] = (unsigned char)inp(0x40 + 2/*TIMER*/);         // get high byte

        ticks = delay12 + (((1 << COUNTER_WIDTH) - 1) & ~*pwCount);
        while (1) 
        {
            outp(0x43, (2/*TIMER*/ << 6) + 0x0);                        // counter latch timer 2
            counterLoHi[0] = (unsigned char)inp(0x40 + 2/*TIMER*/);     // get low byte
            counterLoHi[1] = (unsigned char)inp(0x40 + 2/*TIMER*/);     // get high byte
            uint32_t diff = ticks - (((1 << COUNTER_WIDTH) - 1) & ~*pwCount);

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

extern void _disable(void);
extern void _enable(void);

#pragma intrinsic (_disable, _enable)

#define TIMER 2

/** _osifIbmAtGetTscPerSec - calulates TSC clocks per second

    GetTscPerSec() returns the TimeStampCounter counts per second

    NTSC Color Subcarrier:  f = 3.579545MHz * 4 ->
                            f = 14.31818MHz / 12 -> 1.193181666...MHz
    PIT 8254 input clk:     f = 1.193181666MHz
                            f = 11931816666Hz / 62799 ->
                            f = 19Hz -> t = 1/f
                            t = 52.631ms
                            ========
                            52.631ms * 19 ->
                                  1s
                            ===============

    @param[in] VOID

    @retval number of CPU clock per second

**/
unsigned long long _osifIbmAtGetTscPer62799(uint32_t delay) {

    size_t eflags = __readeflags();         // save flaags
    unsigned long long qwTSCPerTick, qwTSCEnd, qwTSCStart, qwTSCDrift;
    unsigned char counterLoHi[2];
    unsigned short* pwCount = (unsigned short*)&counterLoHi[0];
    unsigned short wCountDrift;

    _disable();

    outp(0x61, 0);                          // stop counter
    outp(0x43, (TIMER << 6) + 0x34);        // program timer 2 for MODE 2
    outp(0x42, 0xFF);                       // write counter value low 65535
    outp(0x42, 0xFF);                       // write counter value high 65535
    outp(0x61, 1);                          // start counter

    qwTSCStart = __rdtsc();                 // get TSC start

    //
    // repeat counter latch command until 50ms
    //
    do                                                              //
    {                                                               //
        outp(0x43, (TIMER << 6) + 0x0);                             // counter latch timer 2
        counterLoHi[0] = (unsigned char)inp(0x40 + TIMER);          // get low byte
        counterLoHi[1] = (unsigned char)inp(0x40 + TIMER);          // get high byte
        //
    } while (*pwCount > (65535 - 62799));                           // until 62799 ticks gone

    qwTSCEnd = __rdtsc();                               // get TSC end ~50ms

    *pwCount = 65535 - *pwCount;                        // get true, not inverted, number of clock ticks...
    // ... that really happened
    wCountDrift = *pwCount - 62799;                     // get the number of additional ticks gone through
    if (wCountDrift > 22)
        wCountDrift -= 22;

    //
    // approximate the additional number of TSC
    //
    qwTSCPerTick = (qwTSCEnd - qwTSCStart) / *pwCount;  // get number of CPU TSC per 8254 ClkTick (1,19MHz)
    qwTSCDrift = wCountDrift * qwTSCPerTick;            // get TSC drift

    //printf("PITticks gone: %04d, PITticksDrift: %04d, TSCPerTick: %lld, TSCDrift: %5lld, TSC end/start diff: %lld, end - start - TSCDrift: %lld, TSC/Sec: %lld\n",
    //    *pwCount,                                       /* PITticks gone            */
    //    wCountDrift,                                    /* PITticksDrift            */
    //    qwTSCPerTick,                                   /* TSCPerTick               */
    //    qwTSCDrift,                                     /* TSCDrift                 */
    //    qwTSCEnd - qwTSCStart,                          /* TSC end/start diff       */
    //    qwTSCEnd - qwTSCDrift - qwTSCStart,             /* end - start - TSCDrift   */
    //    20 * (qwTSCEnd - qwTSCStart - qwTSCDrift)       /* TSC/Sec                  */
    //);

    if (0x200 & eflags)                                     // restore IF interrupt flag
        _enable();

    return 1 * (qwTSCEnd - qwTSCStart - qwTSCDrift);   // subtract the drift from TSC difference, scale to 1 second
}