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
 * rdtscp-based time functions
 *
 * This measurement method uses RDTSCP to measure
 * elapsed time. Its output is a CPU-specific
 * tick count whose relation to real time may vary
 * depending on the power saving mechanisms on some
 * CPUs.
 *
 * WARNING: If you use this, make sure you understand the
 *          limitations of RDTSCP!
 */

#ifndef TIME_R_RDTSCP
#define TIME_R_RDTSCP

#include <stdint.h>

#define TIME_R_UNIT "cpu tick(s)"

#if !defined(__i386__) && !defined(__x86_64__)
#  error "RDTSCP time measurement requested, but no x86(_64) compatible compiler used?"
#endif

typedef long long timeR_t;

/* read the current time */
static inline timeR_t tr_now(void) {
    unsigned int ax, cx, dx;

    /* memory clobber should stop the compiler from moving this call around */
    asm volatile("rdtscp" : "=a" (ax), "=c" (cx), "=d" (dx) : : "memory");

    return ax | ((uint64_t)dx << 32);
}

/* use CPUID to check for RDTSCP */
static inline int rtime_check_working(void) {
    unsigned int ax, bx, cx, dx;

    void cpuid(unsigned int arg) {
	asm volatile("cpuid" : "=a" (ax), "=b" (bx), "=c" (cx), "=d" (dx) :
		     "a" (arg));
    }

    /* check for highest supported extended CPUID value */
    cpuid(0x80000000);
    if (ax < 0x80000001)
	return 0;

    /* check for RDTSCP */
    cpuid(0x80000001);
    if (dx & (1 << 27))
	return 1;
    else
	return 0;
}

#endif
