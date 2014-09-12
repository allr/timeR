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
 * POSIX clock_gettime-based time functions
 *
 * This measurement method uses CLOCK_MONOTONIC to
 * measure elapsed time. Its output is always in
 * nanoseconds.
 */

#ifndef TIME_R_POSIX
#define TIME_R_POSIX

#include <stdint.h>
#include <time.h>

#define TIME_R_UNIT "ns"

typedef long long timeR_t;

/* read the current time */
static inline timeR_t tr_now(void) {
    struct timespec ts;

    /* add an optimization barrier so the compiler won't move the call around */
    asm volatile ("" ::: "memory");

    /* ignore result:
     * - EFAULT can't happen (or the compiler is broken)
     * - EINVAL/EPERM were already checked during init
     */
    clock_gettime(CLOCK_MONOTONIC, &ts);

    /* second barrier to avoid moves in the other direction */
    asm volatile ("" ::: "memory");

    return (timeR_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

/* check if this timing method is working, returns 1 if ok */
static inline int rtime_check_working(void) {
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
	return 0;
    else
	return 1;
}

#endif
