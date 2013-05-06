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

#define TIME_R_UNIT "1 cpu tick"

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
