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

typedef long long timeR_t;

/* read the current time */
static inline timeR_t tr_now(void) {
    struct timespec ts;
    timeR_t t;

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
