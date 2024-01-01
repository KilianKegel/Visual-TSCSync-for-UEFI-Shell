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

extern int gfErrorCorrection;

//int iCPD;
//typedef struct _CURPREVDIFF {
//    uint32_t curr;
//    uint32_t prev;
//    uint32_t diff;
//    int32_t delay;
//}CURPREVDIFF;
//CURPREVDIFF curprevdiff[65536];

#define COUNTER_WIDTH 16
__inline uint16_t GetPITCount(void)
{
    uint32_t COUNTER_MASK = ((1 << COUNTER_WIDTH) - 1);
    uint8_t counterLoHi[2];
    uint16_t* pwCount = (uint16_t*)&counterLoHi[0];

    outp(0x43, (2/*TIMER*/ << 6) + 0x0),                            // counter latch timer 2
        counterLoHi[0] = (unsigned char)inp(0x40 + 2/*TIMER*/),     // get low byte
        counterLoHi[1] = (unsigned char)inp(0x40 + 2/*TIMER*/);     // get high byte

    return *pwCount;
    //return COUNTER_MASK & ~*pwCount;
}

int64_t PITClkWait/*pseudo delay upcount*/(int32_t Delay)
{
    static int cnt;
    int64_t  delay3 = Delay / 3, count = delay3, maxdrift = 0;
    uint64_t qwTSCPerIntervall, qwTSCEnd=0, qwTSCStart=0;
    size_t eflags = __readeflags();                     // save flaags
    int syncprogress = 1;

    _disable();
    GetPITCount();

    if (1)
    {
        uint16_t previous,current,diff = 0;

        while (syncprogress)
        {
            for (int i = 0; i < 5 && syncprogress; i++)
            {
                count = delay3 = Delay / 3;
                previous = GetPITCount();
                qwTSCStart = __rdtsc();                             // get TSC start

                while (count > 0)
                {
                    current = GetPITCount();

                    if (previous >= current)
                        diff = previous - current;
                    else
                        diff = ~(current - previous) + 1;

                    //curprevdiff[iCPD].diff = diff;//kgtest
                    //curprevdiff[iCPD].curr = current;//kgtest
                    //curprevdiff[iCPD].prev = previous;//kgtest

                    previous = current;

                    count -= diff;

                    //curprevdiff[iCPD++].delay = delay;//kgtest

                }

                qwTSCEnd = __rdtsc();                           // get TSC end ~50ms

                //if ((count + maxdrift) >= 0)
                //{
                    syncprogress = 0;
                    break;
                //}
            }
            maxdrift++;
        }
        printf("%lld       ", -count);                          // Additional ticks gone through: 

        //
        // subtract the additional number of TSC gone through
        //
        if (1 == gfErrorCorrection)
            qwTSCPerIntervall = ((qwTSCEnd - qwTSCStart) * delay3) / (delay3 - count);    // get number of CPU TSC per 8254 ClkTick (1,19MHz)
        else
            qwTSCPerIntervall = qwTSCEnd - qwTSCStart;

        if (0x200 & eflags)                                     // restore IF interrupt flag
            _enable();

    }
    return (int64_t)qwTSCPerIntervall;
}

///////////////////////////////////////
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
unsigned long long __osifIbmAtGetTscPer62799(uint32_t delay) {

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