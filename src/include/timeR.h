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

#ifndef TIME_R_H
#define TIME_R_H

#define TIME_R_BIN_NAME_ATTR "timeR_bin_name"

#ifdef HAVE_TIME_R

#include <assert.h>
#include "timeR-config.h"

# ifdef TIME_R_CLOCK_POSIX
#  include "timeR-posix.h"
# endif
# ifdef TIME_R_CLOCK_RDTSC
#  include "timeR-rdtsc.h"
# endif
# ifdef TIME_R_CLOCK_RDTSCP
#  include "timeR-rdtscp.h"
# endif

#define TIME_R_ENABLED 1

#if defined(__GNUC__) || defined(__clang__)
  #define TMR_ALWAYS_INLINE __attribute__((always_inline))
#else
  #define TMR_ALWAYS_INLINE
#endif

#include "timeR-timerlist.h"

/* timing bin: accumulates measured times */
typedef struct {
    char              *prefix;        /* prefix for the name */
    char              *name;          /* user-visible name in dump */
    timeR_t            sum_self;      /* time accumulated in just this bin */
    timeR_t            sum_total;     /* time accumulated including "called" bins */
    unsigned long long starts;        /* number of times this bin started accumulating */
    unsigned long long aborts;        /* number of times this bin implicitly ended */
    unsigned int       bcode:1;       /* a user function was evaluated in byte-compiled form */
} tr_bin_t;

/* timer element: instanced for each time timer */
typedef struct {
    timeR_t      start;        /* start time of this timer */
    timeR_t      lower_sum;    /* time accumulated in "called" timers */
    unsigned int bin_id;       /* ID of the bin that receives the timer */
} tr_timer_t;

/* pointer to timer element */
typedef struct {
    tr_timer_t   *curblock;       /* timer block that holds this element */
    unsigned int  index;          /* index within that block */
} tr_measureptr_t;

void timeR_init_early(void);
void timeR_startup_done(void);
void timeR_finish(void);

void timeR_forked(long childpid);

/* exposed internal state for the fast path inlines */
extern tr_timer_t  *timeR_measureblocks[TIME_R_MAX_MBLOCKS];
extern tr_timer_t  *timeR_current_mblock;
extern unsigned int timeR_current_mblockidx;
extern unsigned int timeR_next_mindex;
extern tr_bin_t    *timeR_bins;
extern timeR_t      timeR_current_lower_sum;

/* slow path functions for the fast path inlines */
void timeR_measureblock_full(void);
void timeR_end_timers_slowpath(const tr_measureptr_t *mptr, timeR_t when);

/* debug aid */
void timeR_dump_timer_stack(void);

/* fast path implementation */
static inline tr_measureptr_t timeR_begin_timer(tr_bin_id_t timer) {
    timeR_t         start = tr_now();
    tr_timer_t      *m;
    tr_measureptr_t mptr;

    //assert(timer < next_bin);

    /* allocate the next free measurement */
    mptr.curblock = timeR_current_mblock;
    mptr.index    = timeR_next_mindex;

    timeR_current_mblock[timeR_next_mindex].lower_sum    = timeR_current_lower_sum;
    timeR_current_lower_sum                              = 0;

    /* check if the measurement block is full */
    if (++timeR_next_mindex >= TIME_R_MBLOCK_SIZE)
        timeR_measureblock_full();

    m         = &mptr.curblock[mptr.index];
    m->start  = start;
    m->bin_id = timer;
    timeR_bins[timer].starts++;
    return mptr;
}

/* end the top-of-stack timer, extracted for reuse in end_timers_slowpath */
// FIXME: Seems a bit long for a fast-path function...
static inline void TMR_ALWAYS_INLINE timeR_end_latest_timer(timeR_t endtime) {
    tr_timer_t *m;
    tr_bin_t   *bin;
    timeR_t     diff;

    if (timeR_next_mindex == 0) {
        /* go back one mblock */
        assert(timeR_current_mblockidx > 0);
        timeR_next_mindex = TIME_R_MBLOCK_SIZE - 1;
        timeR_current_mblockidx--;
        timeR_current_mblock = timeR_measureblocks[timeR_current_mblockidx];
    } else
        timeR_next_mindex--;

    /* calculate time difference */
    m    = &timeR_current_mblock[timeR_next_mindex];
    diff = endtime - m->start;

    //assert(m->bin_id < next_bin);
    bin = &timeR_bins[m->bin_id];
    bin->sum_total += diff;

    /* calculate final amount of time spent in lower-level timers */
    if (diff >= timeR_current_lower_sum) {
        /* don't add negative values to self time */
        bin->sum_self += diff - timeR_current_lower_sum;
    } else {
        fprintf(stderr, "*** WARNING: Negative self time!\n");
    }
    timeR_current_lower_sum = m->lower_sum + diff;
}

static inline void timeR_end_timer(const tr_measureptr_t *mptr) {
    /* capture current time in case multiple timers are ending */
    timeR_t endtime = tr_now();

    /* end the newest timer on the measurement stack */
    timeR_end_latest_timer(endtime);

    /* run slowpath if this wasn't enough */
    if (timeR_current_mblock != mptr->curblock ||
        timeR_next_mindex    != mptr->index) {
        timeR_bins[timeR_current_mblock[timeR_next_mindex].bin_id].aborts++;
        timeR_end_timers_slowpath(mptr, endtime);
    }
}

tr_measureptr_t timeR_begin_external(char *name, void *addr);

/* generate a marker for the current position of the measurement stack */
/* to avoid a branch this marker points to the next free measurement   */
static inline tr_measureptr_t timeR_mark(void) {
    tr_measureptr_t mptr;

    mptr.curblock = timeR_current_mblock;
    mptr.index    = timeR_next_mindex;

    return mptr;
}

/* set the bcode flag in a time bin */
static inline void timeR_mark_bcode(unsigned int bin_id) {
    timeR_bins[bin_id].bcode = 1;
}

unsigned int timeR_add_userfn_bin(void);
void         timeR_name_bin(unsigned int bin_id, const char *name);
void         timeR_name_bin_anonfunc(unsigned int bin_id, const char *file,
                                     unsigned int line, unsigned int pos);
void         timeR_release(tr_measureptr_t *marker);

void         timeR_idlemark(int state);

static inline const char *timeR_get_bin_name(unsigned int bin_id) {
  return timeR_bins[bin_id].name;
}

extern char *timeR_output_file;
extern int   timeR_output_raw;
extern int   timeR_reduced_output;
extern int   timeR_exclude_init;
extern long  timeR_scale;

/* convenience macros */
#  define TimeR_CONCAT(a,b) a ## b

#  ifdef TIME_R_STATICTIMERS
// note: slightly messy, but allows compile-time removal of disabled timers
//       and the unconditional version wasn't a C statement either
#    define BEGIN_TIMER(bin) \
    tr_measureptr_t rtm_mptr_##bin; \
    if (TimeR_CONCAT(bin, _State)) \
	rtm_mptr_##bin = timeR_begin_timer(bin)

#    define END_TIMER(bin) \
    if (TimeR_CONCAT(bin, _State)) \
	timeR_end_timer(&rtm_mptr_##bin)

#    define BEGIN_TIMER_ALTERNATIVES(cond, bin_true, bin_false)	\
    tr_measureptr_t rtm_mptr_##bin_true;			\
    if (cond) {							\
	if (TimeR_CONCAT(bin_true, _State))			\
	    rtm_mptr_##bin_true = timeR_begin_timer(bin_true);	\
    } else {							\
	if (TimeR_CONCAT(bin_false, _State))			\
	    rtm_mptr_##bin_true = timeR_begin_timer(bin_false); \
    }

#    define END_TIMER_ALTERNATIVES(cond, bin_true, bin_false)	\
    if (cond && TimeR_CONCAT(bin_true, _State)) {		\
	timeR_end_timer(&rtm_mptr_##bin_true);			\
    } else if (!cond && TimeR_CONCAT(bin_false, _State)) {	\
	timeR_end_timer(&rtm_mptr_##bin_true);			\
    }

#  else
#    define BEGIN_TIMER(bin)                     do {} while (0)
#    define END_TIMER(bin)                       do {} while (0)
#    define BEGIN_TIMER_ALTERNATIVES(cond,tr,fa) do {} while (0)
#    define END_TIMER_ALTERNATIVES(cond,tr,fa)   do {} while (0)
#  endif

#  define MARK_TIMER() \
    tr_measureptr_t rtm_mptr_marker = timeR_mark()

#  define RELEASE_TIMER() \
    timeR_release(&rtm_mptr_marker)


/* timers for external functions */
#  ifdef TIME_R_EXTFUNC

#    define BEGIN_EXTERNAL_TIMER(fname, faddr)			\
    tr_measureptr_t rtm_mptr_extfunc =				\
	timeR_begin_external(fname, faddr);

#    define END_EXTERNAL_TIMER()		\
    timeR_end_timer(&rtm_mptr_extfunc);

#  else
#    define BEGIN_EXTERNAL_TIMER(n,a) do {} while (0)
#    define END_EXTERNAL_TIMER()      do {} while (0)
#  endif


/* timers for functions called via R_FunTab */
#  ifdef TIME_R_FUNTAB

#    define BEGIN_PRIMFUN_TIMER(id) \
    tr_measureptr_t rtm_mptr_primfun = timeR_begin_timer((id) + TR_StaticBinCount)

#    define END_PRIMFUN_TIMER(id) \
    timeR_end_timer(&rtm_mptr_primfun)

#  else

#    define BEGIN_PRIMFUN_TIMER(id) do {} while (0)
#    define END_PRIMFUN_TIMER(id)   do {} while (0)

#  endif


/* timers for functions written in R */
#  ifdef TIME_R_USERFUNCTIONS

#    define BEGIN_RFUNC_TIMER(id) \
    tr_measureptr_t rtm_mptr_rfunction = timeR_begin_timer(id)

#    define END_RFUNC_TIMER(id) \
    timeR_end_timer(&rtm_mptr_rfunction)

#  else

#    define BEGIN_RFUNC_TIMER(id) do {} while (0)
#    define END_RFUNC_TIMER(id)   do {} while (0)

#  endif

#else

  // timeR not enabled in configure
#  define TIME_R_ENABLED 0

static inline void timeR_init_early(void)   {}
static inline void timeR_startup_done(void) {}
static inline void timeR_finish(void)       {}
static inline void MARK_TIMER(void)         {}
static inline void RELEASE_TIMER(void)      {}

static inline unsigned int timeR_add_userfn_bin(void) {
    return 0;
}

static inline void timeR_name_bin(unsigned int bin_id, const char *name) {}
static inline void timeR_name_bin_anonfunc(unsigned int bin_id,
					   const char *file,
					   unsigned int line,
					   unsigned int pos) {}

static inline const char * timeR_get_bin_name(unsigned int bin_id) {
  return "";
}

static inline void timeR_mark_bcode(unsigned int bin_id) {}

static inline void timeR_idlemark(int state) {}

  // avoids an #ifdef in eval.c
#  define TR_UserFuncFallback 0

  // defined as macros to ensure the parameter is not parsed
#  define BEGIN_TIMER(unused)     do {} while (0)
#  define END_TIMER(unused)       do {} while (0)
#  define BEGIN_PRIMFUN_TIMER(id) do {} while (0)
#  define END_PRIMFUN_TIMER(id)   do {} while (0)
#  define BEGIN_RFUNC_TIMER(id)   do {} while (0)
#  define END_RFUNC_TIMER(id)     do {} while (0)
#  define BEGIN_EXTERNAL_TIMER(n,a) do {} while (0)
#  define END_EXTERNAL_TIMER()    do {} while (0)

#endif // HAVE_TIME_R

#endif // TIME_R_H
