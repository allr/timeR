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
