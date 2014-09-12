/*
 *  timeR : Deterministic profiling for R
 *  Copyright (C) 2013  TU Dortmund Informatik LS XII
 *  Inspired by r-timed from the Reactor group at Purdue,
 *    http://r.cs.purdue.edu/
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  http://www.r-project.org/Licenses/
 */

/*
 * rdtsc-based time functions
 *
 * This measurement method uses RDTSC to measure
 * elapsed time. Its output is a CPU-specific
 * tick count whose relation to real time may vary
 * depending on the power saving mechanisms on some
 * CPUs.
 *
 * WARNING: If you use this, make sure you understand the
 *          limitations of RDTSC!
 */

#ifndef TIME_R_RDTSC
#define TIME_R_RDTSC

#include <stdint.h>

#define TIME_R_UNIT "cpu tick(s)"

#if !defined(__i386__) && !defined(__x86_64__)
#  error "RDTSC time measurement requested, but no x86(_64) compatible compiler used?"
#endif

typedef long long timeR_t;

/* read the current time */
static inline timeR_t tr_now(void) {
    unsigned int ax, bx, cx, dx;

#ifdef __i386__
    /* cpuid forces serialization in the CPU */
    /* memory clobber should stop the compiler from moving this call around */
    // FIXME: Saving/restoring ebx is only strictly neccessary if compiled with PIC
    asm volatile("movl %%ebx, %%esi \n"
                 "movl $0,    %%eax \n"
                 "cpuid             \n"
                 "rdtsc             \n"
                 "movl %%esi, %%ebx"
                 : "=a" (ax), "=c" (cx), "=d" (dx) : : "%esi", "memory");
#elif defined(__x86_64__)
    /* cpuid forces serialization in the CPU */
    /* memory clobber should stop the compiler from moving this call around */
    asm volatile("cpuid\nrdtsc" : "=a" (ax), "=b" (bx), "=c" (cx), "=d" (dx) : "a" (0) : "memory");
#else
#  error "No rdtsc implementation found?"
#endif

    return ax | ((uint64_t)dx << 32);
}

/* assume that RDTSC always works */
static inline int rtime_check_working(void) {
    return 1;
}

#endif
