#ifndef TIME_R_H
#define TIME_R_H

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

typedef enum {
    // internal
    TR_Startup,
    TR_Overhead,
    TR_UserFuncFallback,

    // memory.c
    TR_cons,
    TR_allocVector,
    TR_allocList,
    TR_allocS4,
    TR_GCInternal,
    TR_Protect,
    TR_UnprotectPtr,

    // arith.c
    TR_doArith,

    // connections.c
    TR_gzFile,
    TR_bzFile,
    TR_xzFile,

    // context.c
    TR_onExits,

    // dotcode.c
    TR_dotExternal,
    TR_dotCallFull,
    TR_dotCall,
    TR_dotCodeFull,
    TR_dotCode,

    // dounzip.c
    TR_doUnzip,
    TR_zipRead,

    // duplicate.c
    TR_Duplicate,

    // envir.c
    TR_SymLookup,
    TR_FunLookup,
    TR_FunLookupEval,

    // errors.c
    TR_CheckStack,

    // eval.c
    TR_Match,
    TR_dotBuiltIn,
    TR_Eval,

    // internet.c
    TR_Download,

    // logic.c
    TR_doLogic,

    // main.c
    TR_Repl,
    TR_Mainloop,

    // names.c
    TR_Install,
    TR_dotSpecial2,

    // subset.c
    TR_doSubset,
    TR_doSubset2,

    // Rsock.c
    TR_inSockRead,
    TR_inSockWrite,
    TR_inSockOpen,
    TR_inSockConnect,

    // sys-std.c
    TR_Sleep,

    // sys-unix.c
    TR_System,

    /* must be the last entry */
    TR_FirstUserBin
} tr_bin_id_t;

/* timing bin: accumulates measured times */
typedef struct {
    char              *name;          /* user-visible name in dump */
    timeR_t            sum_exclusive; /* time accumulated in just this bin */
    timeR_t            sum_complete;  /* time accumulated including "called" bins */
    unsigned long long starts;        /* number of times this bin started accumulating */
    unsigned long long aborts;        /* number of times this bin implicitly ended */
    unsigned int       drop:1;        /* do not include this bin in caller bins */
} tr_bin_t;

/* timer element: instanced for each time timer */
typedef struct {
    timeR_t      start;        /* start time of this timer */
    timeR_t      lower_sum;    /* time accumulated in "called" timers */
    timeR_t      dropped_time; /* time accumulated in lower bins marked as "drop" (e.g. overhead) */
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

/* exposed internal state for the fast path inlines */
extern tr_timer_t  *timeR_measureblocks[TIME_R_MAX_MBLOCKS];
extern tr_timer_t  *timeR_current_mblock;
extern unsigned int timeR_current_mblockidx;
extern unsigned int timeR_next_mindex;
extern tr_bin_t    *timeR_bins;

/* slow path functions for the fast path inlines */
void timeR_measureblock_full(void);
void timeR_end_timers_slowpath(const tr_measureptr_t *mptr, timeR_t when,
                               timeR_t prev_diff, timeR_t prev_drop);

/* fast path implementation */
static inline tr_measureptr_t timeR_begin_timer(tr_bin_id_t timer) {
    /* capture start time early to simplify overhead subtraction */
    timeR_t         start = tr_now();
    tr_timer_t      *m;
    tr_measureptr_t mptr;

    //assert(timer < next_bin);

    /* allocate the next free measurement */
    mptr.curblock = timeR_current_mblock;
    mptr.index    = timeR_next_mindex;

    timeR_current_mblock[timeR_next_mindex].lower_sum    = 0;
    timeR_current_mblock[timeR_next_mindex].dropped_time = 0;

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
static inline void TMR_ALWAYS_INLINE timeR_end_latest_timer(timeR_t endtime, timeR_t *prev_diff, timeR_t *prev_drop) {
    tr_timer_t *m;
    tr_bin_t   *bin;
    timeR_t     diff, tmp;

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

    /* reduce difference if a previous bin was marked as drop */
    if (diff >= *prev_drop) {
        diff -= *prev_drop;
    } else {
        fprintf(stderr, "*** WARNING: Negative difference remains after drop!\n");
        diff = 0;
    }

    //assert(m->bin_id < next_bin);
    bin = &timeR_bins[m->bin_id];
    bin->sum_complete += diff;

    /* calculate final amount of time spent in lower-level timers */
    tmp = m->lower_sum + *prev_diff;
    if (diff >= tmp) {
        /* don't add negative values to exclusive time */
        bin->sum_exclusive += diff - tmp;
    } else {
        fprintf(stderr, "*** WARNING: Negative exclusive time!\n");
    }

    /* add dropped time */
    m->dropped_time += *prev_drop;

    if (bin->drop) {
        *prev_drop += diff;
        *prev_diff = 0;
    } else {
        *prev_diff = diff;
    }
}

static inline void timeR_end_timer(const tr_measureptr_t *mptr) {
    timeR_t prev_diff = 0;
    timeR_t prev_drop = 0;

    /* capture current time in case multiple timers are ending */
    timeR_t endtime = tr_now();

    /* end the newest timer on the measurement stack */
    timeR_end_latest_timer(endtime, &prev_diff, &prev_drop);

    /* run slowpath if this wasn't enough */
    if (timeR_current_mblock != mptr->curblock ||
        timeR_next_mindex    != mptr->index) {
        timeR_bins[timeR_current_mblock[timeR_next_mindex].bin_id].aborts++;
        timeR_end_timers_slowpath(mptr, endtime, prev_diff, prev_drop);
    }
}

/* generate a marker for the current position of the measurement stack */
/* to avoid a branch this marker points to the next free measurement   */
static inline tr_measureptr_t timeR_mark(void) {
    tr_measureptr_t mptr;

    mptr.curblock = timeR_current_mblock;
    mptr.index    = timeR_next_mindex;

    return mptr;
}

unsigned int timeR_add_userfn_bin(void);
void         timeR_name_bin(unsigned int bin_id, char *name);
void         timeR_name_bin_anonfunc(unsigned int bin_id, const char *file,
                                     unsigned int line, unsigned int pos);
void         timeR_release(tr_measureptr_t *marker);

char *timeR_output_file;

/* convenience macros */
#  define BEGIN_TIMER(bin) \
    tr_measureptr_t rtm_mptr_##bin = timeR_begin_timer(bin)

#  define END_TIMER(bin) \
    timeR_end_timer(&rtm_mptr_##bin);

#  define MARK_TIMER() \
    tr_measureptr_t rtm_mptr_marker = timeR_mark()

#  define RELEASE_TIMER() \
    timeR_release(&rtm_mptr_marker)

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

static inline void timeR_name_bin(unsigned int bin_id, char *name) {}
static inline void timeR_name_bin_anonfunc(unsigned int bin_id,
					   const char *file,
					   unsigned int line,
					   unsigned int pos) {}

  // avoids an #ifdef in eval.c
#  define TR_UserFuncFallback 0

  // defined as macros to ensure the parameter is not parsed
#  define BEGIN_TIMER(unused) do {} while (0)
#  define END_TIMER(unused)   do {} while (0)

#endif // HAVE_TIME_R

#endif // TIME_R_H
