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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

// FIXME: s/mbindex/blockidx/ for clarity

#define TIMER_INCLUDE_STATE_ARRAY

#include <sys/time.h>
#include <sys/resource.h>
#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <Defn.h>
#include "timeR.h"

/* names of the static bins
 * must be in sync with tr_bin_id_t in timeR.h!
 */
static char *bin_names[] = {
    /* The following list is also parsed by timeR-genconfig.pl, so please   */
    /* keep it in one entry per line format and do not move the markers     */
    /* MARKER:START */
    // internal
    "OverheadTest1",    /* MARKER:ALWAYS */
    "OverheadTest2",    /* MARKER:ALWAYS */
    "HashOverhead",
    // first timer in output is Startup
    "Startup",
    "UserFuncFallback",

    // memory.c
    "cons",
    "allocVector",
    "allocList",
    "allocS4",
    "GCInternal",

    // arith.c
    "doArith",

    // array.c,
    "doMatprod",

    // connections.c
    "gzFile",
    "bzFile",
    "xzFile",

    // context.c
    "onExits",

    // dotcode.c
    "dotExternalFull",
    "dotExternal",
    "dotCallFull",
    "dotCall",
    "dotCFull",
    "dotC",
    "dotFortranFull",
    "dotFortran",

    // dounzip.c
    "doUnzip",
    "zipRead",

    // duplicate.c
    "Duplicate",

    // envir.c
    "findVarInFrame3other",
    "SymLookup",
    "FunLookup",

    // eval.c
    "Match",
    "evalList",
    "bcEval",
    "bcEvalGetvar",
    "bcEvalArith1",
    "bcEvalArith2",
    "bcEvalMath1",
    "bcEvalRelop",
    "bcEvalLogic",

    // internet.c
    "Download",

    // logic.c
    "doLogic",
    "doLogic2",
    "doLogic3",

    // main.c
    "Repl",
    "SetupMainLoop",
    "endMainloop",

    // names.c
    "Install",
    "do_internal",

    // relop.c
    "doRelop",

    // subassign.c
    "doSubassign",
    "doSubassign2",
    "doSubassign3",

    // subset.c
    "doSubset",
    "doSubset2",
    "doSubset3",

    // Rsock.c
    "Rsock",

    // sys-std.c
    "Sleep",

    // sys-unix.c
    "System",

    // add your own timers here

    /* MARKER:END */
};

typedef char static_assert___names_and_enums_do_not_match
   [(sizeof(bin_names)/sizeof(bin_names[0]) == TR_StaticBinCount) ? 1 : -1];

/* needs C-1x
_Static_Assert(sizeof(bin_names)/sizeof(bin_names[0]) == TR_StaticBinCount,
               "Number of tr_bin_id_t entries does not match bin_names array!");
*/


/*** internal-only data structures ***/

/* fallback name if string duplication fails */
static char *userfunc_unknown_str = "unknown_user_function";

/* timer list */
static unsigned int max_mbindex;
tr_timer_t  *timeR_measureblocks[TIME_R_MAX_MBLOCKS];
unsigned int timeR_current_mblockidx;
tr_timer_t  *timeR_current_mblock;
unsigned int timeR_next_mindex; // always points to a free timer entry
timeR_t      timeR_current_lower_sum;

/* bins */
static unsigned int next_bin = TR_StaticBinCount;
static unsigned int bin_count;
static unsigned int first_userfn_idx;
tr_bin_t *timeR_bins;  // is realloc()'d, no pointers to elements please!


/* external function timing */
typedef struct {
    void *addr; // FIXME: Technically incorrect, should use a function ptr
    unsigned int bin_id;
} tr_extfunc_entry_t;

static tr_extfunc_entry_t *extfunc_map;
static unsigned int extfunc_map_length;
static unsigned int extfunc_map_entries;


/* additional hardcoded timers */
static tr_measureptr_t startup_mptr;
static timeR_t         start_time, end_time;

char *timeR_output_file;
int   timeR_output_raw     = 0;
int   timeR_reduced_output = 1;
int   timeR_exclude_init   = 0;
long  timeR_scale          = 1;

/*** internal functions ***/

static int compare_binnames(const void *a_void, const void *b_void) {
    const tr_bin_t * const *a = a_void;
    const tr_bin_t * const *b = b_void;

    int res = 0;

    if ((*a)->prefix != NULL && (*b)->prefix != NULL) {
	res = strcmp((*a)->prefix, (*b)->prefix);
    } else if ((*a)->prefix == NULL && (*b)->prefix != NULL) {
	return -1;
    } else if ((*a)->prefix != NULL && (*b)->prefix == NULL) {
	return 1;
    }

    if (res == 0)
	res = strcmp((*a)->name, (*b)->name);

    return res;
}

static int compare_selftime_desc(const void *a_void, const void *b_void) {
    const tr_bin_t * const *a = a_void;
    const tr_bin_t * const *b = b_void;

    if ((*a)->sum_self < (*b)->sum_self)
	return 1;
    else if ((*a)->sum_self > (*b)->sum_self)
	return -1;
    else
	return 0;
}

/* merge duplicated bins by name */
static void merge_dupes(tr_bin_t **binpointers, unsigned int count) {
    qsort(binpointers, count, sizeof(tr_bin_t *),
	  compare_binnames);

    tr_bin_t *prev_bin = binpointers[0];

    for (unsigned int i = 1; i < next_bin - first_userfn_idx; i++) {
	tr_bin_t *cur_bin = binpointers[i];

	if (!compare_binnames(&prev_bin, &cur_bin)) {
	    // same name, merge and disable the current one
	    prev_bin->sum_self  += cur_bin->sum_self;
	    prev_bin->sum_total += cur_bin->sum_total;
	    prev_bin->starts    += cur_bin->starts;
	    prev_bin->aborts    += cur_bin->aborts;
	    prev_bin->bcode     |= cur_bin->bcode;

	    cur_bin->name[0]   = 0;
	    cur_bin->sum_self  = 0;
	    cur_bin->sum_total = 0;
	    cur_bin->starts    = 0;
	} else {
	    prev_bin = cur_bin;
	}
    }
}

static void timeR_print_bin(FILE *fd, tr_bin_t *bin, bool force,
			    timeR_t all_self) {
    if (timeR_reduced_output && bin->starts == 0 && !force)
	return;

    if (bin->prefix != NULL)
	fprintf(fd, "%s:", bin->prefix);

    fprintf(fd, "%s\t", bin->name);

    if (all_self != 0)
	fprintf(fd, "%.2f%%\t", (double)bin->sum_self / all_self * 100.0);

    fprintf(fd, "%lld\t" "%lld\t" "%llu\t" "%llu\t" "%d\n",
            bin->sum_self  / timeR_scale,
            bin->sum_total / timeR_scale,
            bin->starts,
            bin->aborts,
            bin->bcode);
}

static void timeR_dump_raw(FILE *fd) {
#ifdef TIME_R_FUNTAB
    /* calculate and print sums for the builtin/special timers */
    timeR_t            bself_sum  = 0, btotal_sum = 0, sself_sum  = 0, stotal_sum = 0;
    unsigned long long bstart_sum = 0, babort_sum = 0, sstart_sum = 0, sabort_sum = 0;

    int i = 0;
    while (R_FunTab[i].name != NULL) {
	unsigned int bin_id = TR_StaticBinCount + i;

	if ((R_FunTab[i].eval % 10) == 0) {
	    /* SPECIALSXP */
	    sself_sum  += timeR_bins[bin_id].sum_self;
	    stotal_sum += timeR_bins[bin_id].sum_total;
	    sstart_sum += timeR_bins[bin_id].starts;
	    sabort_sum += timeR_bins[bin_id].aborts;
	} else {
	    /* BUILTINSXP */
	    bself_sum  += timeR_bins[bin_id].sum_self;
	    btotal_sum += timeR_bins[bin_id].sum_total;
	    bstart_sum += timeR_bins[bin_id].starts;
	    babort_sum += timeR_bins[bin_id].aborts;
	}

	i++;
    }

    fprintf(fd, "#!LABEL\tself\ttotal\tcalls\taborts\n");

    fprintf(fd, "BuiltinSum\t%lld\t%lld\t%llu\t%llu\n",
	    bself_sum  / timeR_scale,
	    btotal_sum / timeR_scale,
	    bstart_sum, babort_sum);
    fprintf(fd, "SpecialSum\t%lld\t%lld\t%llu\t%llu\n",
	    sself_sum  / timeR_scale,
	    stotal_sum / timeR_scale,
	    sstart_sum, sabort_sum);
#endif

#if defined(TIME_R_USERFUNCTIONS) || defined(TIME_R_EXTFUNC)
    /* sort the user function timers by name */
    tr_bin_t **binpointers = malloc(sizeof(tr_bin_t *) * (next_bin - first_userfn_idx));
    if (binpointers == NULL)
	abort();

    // use this opportunity to calculate the user function sum too
    timeR_t            uself_sum  = 0, utotal_sum = 0;
    unsigned long long ustart_sum = 0, uabort_sum = 0;

    for (unsigned int i = 0; i < next_bin - first_userfn_idx; i++) {
	tr_bin_t *bin = &timeR_bins[i + first_userfn_idx];

	binpointers[i] = bin;
	uself_sum     += bin->sum_self;
	utotal_sum    += bin->sum_total;
	ustart_sum    += bin->starts;
	uabort_sum    += bin->aborts;
    }

    fprintf(fd, "#!LABEL\tself\ttotal\tcalls\taborts\n");

#  ifdef TIME_R_USERFUNCTIONS
    fprintf(fd, "UserFunctionSum\t%lld\t%lld\t%llu\t%llu\n",
	    uself_sum  / timeR_scale,
	    utotal_sum / timeR_scale,
	    ustart_sum, uabort_sum);
#  endif

    merge_dupes(binpointers, next_bin - first_userfn_idx);
#endif // TIME_R_USERFUNCTIONS

    /* print static and function table timers */
    fprintf(fd, "#!LABEL\tself\ttotal\tcalls\taborts\thas_bcode\n");

#if !defined(TIME_R_STATICTIMERS) && defined(TIME_R_USERFUNCTIONS)
    // ensure the fallback timer is printed if static timers are off
    timeR_print_bin(fd, &timeR_bins[TR_UserFuncFallback], true, 0);
#endif

#ifdef TIME_R_STATICTIMERS
    for (unsigned int i = TR_Startup; i < TR_StaticBinCount; i++) {
	/* skip disabled timers */
	if (!timer_enables[i])
	    continue;

	timeR_print_bin(fd, &timeR_bins[i], true, 0);
    }
#endif

#ifdef TIME_R_EXTFUNC
    timeR_print_bin(fd, &timeR_bins[TR_HashOverhead], false, 0);
#endif

#ifdef TIME_R_FUNTAB
    for (unsigned int i = TR_StaticBinCount; i < first_userfn_idx; i++)
	timeR_print_bin(fd, &timeR_bins[i], false, 0);
#endif

#if defined(TIME_R_USERFUNCTIONS) || defined(TIME_R_EXTFUNC)
    /* print user function timers */
    for (unsigned int i = 0; i < next_bin - first_userfn_idx; i++) {
	tr_bin_t *bin = binpointers[i];

	if (bin->name[0] != 0)
	    /* print only if it has a name */
	    timeR_print_bin(fd, bin, false, 0);
    }

    free(binpointers);
#endif
}

// FIXME: Too much duplication between this and timeR_dump_raw
static void timeR_dump_processed(FILE *fd, timeR_t total_runtime) {
#ifdef TIME_R_FUNTAB
    /* calculate and print sums for the builtin/special timers */
    timeR_t            bself_sum  = 0, btotal_sum = 0, sself_sum  = 0, stotal_sum = 0;
    unsigned long long bstart_sum = 0, babort_sum = 0, sstart_sum = 0, sabort_sum = 0;

    int i = 0;
    while (R_FunTab[i].name != NULL) {
	unsigned int bin_id = TR_StaticBinCount + i;

	if ((R_FunTab[i].eval % 10) == 0) {
	    /* SPECIALSXP */
	    sself_sum  += timeR_bins[bin_id].sum_self;
	    stotal_sum += timeR_bins[bin_id].sum_total;
	    sstart_sum += timeR_bins[bin_id].starts;
	    sabort_sum += timeR_bins[bin_id].aborts;
	} else {
	    /* BUILTINSXP */
	    bself_sum  += timeR_bins[bin_id].sum_self;
	    btotal_sum += timeR_bins[bin_id].sum_total;
	    bstart_sum += timeR_bins[bin_id].starts;
	    babort_sum += timeR_bins[bin_id].aborts;
	}

	i++;
    }

    fprintf(fd, "#!LABEL\tself_percentage\tself\ttotal\tcalls\taborts\n");

    fprintf(fd, "BuiltinSum\t%.2f%%\t%lld\t%lld\t%llu\t%llu\n",
	    (double)bself_sum / total_runtime * 100.0,
	    bself_sum  / timeR_scale,
	    btotal_sum / timeR_scale,
	    bstart_sum, babort_sum);
    fprintf(fd, "SpecialSum\t%.2f%%\t%lld\t%lld\t%llu\t%llu\n",
	    (double)sself_sum / total_runtime * 100.0,
	    sself_sum  / timeR_scale,
	    stotal_sum / timeR_scale,
	    sstart_sum, sabort_sum);
#endif

#ifdef TIME_R_USERFUNCTIONS
    /* calculate user function sum */
    timeR_t            uself_sum  = 0, utotal_sum = 0;
    unsigned long long ustart_sum = 0, uabort_sum = 0;

    for (unsigned int i = 0; i < next_bin - first_userfn_idx; i++) {
	tr_bin_t *bin = &timeR_bins[i + first_userfn_idx];

	uself_sum  += bin->sum_self;
	utotal_sum += bin->sum_total;
	ustart_sum += bin->starts;
	uabort_sum += bin->aborts;
    }

    fprintf(fd, "#!LABEL\tself\ttotal\tcalls\taborts\n");

    fprintf(fd, "UserFunctionSum\t%lld\t%lld\t%llu\t%llu\n",
	    uself_sum  / timeR_scale,
	    utotal_sum / timeR_scale,
	    ustart_sum, uabort_sum);
#endif

    /* sort timers by name to remove duplicates */
    tr_bin_t **binpointers = malloc(sizeof(tr_bin_t *) * next_bin);
    if (binpointers == NULL)
	abort();

    for (unsigned int i = TR_Startup; i < next_bin; i++)
	binpointers[i - TR_Startup] = timeR_bins + i;

    merge_dupes(binpointers, next_bin - TR_Startup);

    /* now sort by self time, descending */
    qsort(binpointers, next_bin - TR_Startup, sizeof(tr_bin_t *),
	  compare_selftime_desc);

    /* print all timers */
    fprintf(fd, "# --- individual timers\tself_percentage\tself\ttotal\tcalls\taborts\thas_bcode\n");

    for (unsigned int i = TR_Startup; i < next_bin; i++) {
	tr_bin_t *bin = binpointers[i - TR_Startup];

	if (bin->name[0] != 0)
	    timeR_print_bin(fd, bin, false, total_runtime);
    }

    free(binpointers);
}

static void timeR_dump(FILE *fd) {
    // FIXME: Check for errors in fprintfs
    struct rusage my_rusage;
    char strbuf[PATH_MAX+1];
    time_t now;

    if (getcwd(strbuf, sizeof(strbuf)) != NULL) {
	fprintf(fd, "Workdir\t%s\n", strbuf);
    }

    now = time(NULL);
    fprintf(fd, "TraceDate\t%s", ctime(&now));

    getrusage(RUSAGE_SELF, &my_rusage);
    fprintf(fd, "RusageMaxResidentMemorySet\t%ld\n", my_rusage.ru_maxrss);
    fprintf(fd, "RusageSharedMemSize\t%ld\n", my_rusage.ru_ixrss);
    fprintf(fd, "RusageUnsharedDataSize\t%ld\n", my_rusage.ru_idrss);
    fprintf(fd, "RusagePageReclaims\t%ld\n", my_rusage.ru_minflt);
    fprintf(fd, "RusagePageFaults\t%ld\n", my_rusage.ru_majflt);
    fprintf(fd, "RusageSwaps\t%ld\n", my_rusage.ru_nswap);
    fprintf(fd, "RusageBlockInputOps\t%ld\n", my_rusage.ru_inblock);
    fprintf(fd, "RusageBlockOutputOps\t%ld\n", my_rusage.ru_oublock);
    fprintf(fd, "RusageIPCSends\t%ld\n", my_rusage.ru_msgsnd);
    fprintf(fd, "RusageIPCRecv\t%ld\n", my_rusage.ru_msgrcv);
    fprintf(fd, "RusageSignalsRcvd\t%ld\n", my_rusage.ru_nsignals);
    fprintf(fd, "RusageVolnContextSwitches\t%ld\n", my_rusage.ru_nvcsw);
    fprintf(fd, "RusageInvolnContextSwitches\t%ld\n", my_rusage.ru_nivcsw);
    fprintf(fd, "TimerUnit\t%ld %s\n", timeR_scale, TIME_R_UNIT);

    fprintf(fd, "#!LABEL\tsmall\tmedium\n");
    fprintf(fd, "OverheadEstimates\t%.3f\t%.3f\n",
	    (timeR_bins[TR_OverheadTest2].sum_self / (double)timeR_bins[TR_OverheadTest2].starts) / timeR_scale,
	    (timeR_bins[TR_OverheadTest1].sum_self / (double)timeR_bins[TR_OverheadTest1].starts) / timeR_scale);
    fprintf(fd, "TotalRuntime\t%ld\n", (unsigned long)(end_time - start_time));

    if (timeR_output_raw)
	timeR_dump_raw(fd);
    else
	timeR_dump_processed(fd, end_time - start_time);
}


/*** exported functions ***/

void timeR_init_early(void) {
    unsigned int i;

    /* slightly ugly, but this is called before any part of R is initialized */
    if (!rtime_check_working()) {
	fprintf(stderr, "ERROR: The chosen timing method reports that it does not work!\n");
	exit(2);
    }

    /* allocate the first set of timer blocks */
    timeR_measureblocks[0] = calloc(TIME_R_MBLOCK_SIZE, sizeof(tr_timer_t));
    if (timeR_measureblocks[0] == NULL) {
	fprintf(stderr, "ERROR: Failed to allocate the first block of timers!\n");
	exit(2);
    }

    timeR_current_mblock  = timeR_measureblocks[0];
    timeR_current_mblockidx = 0;
    timeR_next_mindex = 1; // the very first timer is just a canary

    /* initialize static bins */
    bin_count = TR_StaticBinCount + TIME_R_INITIAL_EMPTY_BINS;
    timeR_bins = calloc(bin_count, sizeof(tr_bin_t));
    if (timeR_bins == NULL) {
	fprintf(stderr, "ERROR: Failed to allocate the timing bins!\n");
	exit(2);
    }

    for (i = 0; i < next_bin; i++) {
	timeR_bins[i].name = bin_names[i];
    }

    /* add bins for the .Internal/.Primitive functions */
    /* this happens even when function table timers are disabled */
    /* to simplify the rest of the code                          */
    i = 0;
    while (R_FunTab[i].name != NULL) {
	// FIXME: BEGIN_PRIMFUN_TIMER and the dump code assume this
	//        block of fns starts at TR_StaticBinCount
	unsigned int bin_id = timeR_add_userfn_bin();

	timeR_name_bin(bin_id, R_FunTab[i].name);

	if ((R_FunTab[i].eval / 10) % 10 == 1) {
	    // .Internal
	    timeR_bins[bin_id].prefix = "<.Internal>";
	} else {
	    // .Primitive
	    timeR_bins[bin_id].prefix = "<.Primitive>";
	}

	i++;
    }

    first_userfn_idx = i + TR_StaticBinCount;

    /* allocate external function map */
    extfunc_map_length  = TIME_R_EXTFUNC_MAP_STEP;
    extfunc_map_entries = 0;

    extfunc_map = calloc(extfunc_map_length, sizeof(tr_extfunc_entry_t));
    if (extfunc_map == NULL) {
	fprintf(stderr, "ERROR: Failed to allocate memory for external function map!\n");
	exit(2);
    }

    /* run an overhead test with just a single iteration */
    BEGIN_TIMER(TR_OverheadTest1);
    END_TIMER(TR_OverheadTest1);

    /* measure the startup time of R */
    startup_mptr = timeR_begin_timer(TR_Startup);
    start_time   = tr_now();
}

void timeR_startup_done(void) {
    timeR_end_timer(&startup_mptr);

    if (timeR_exclude_init) {
	/* reset all bins */
	for (unsigned int i = TR_HashOverhead; i < next_bin; i++) {
	    tr_bin_t *bin = timeR_bins + i;

	    if (bin->name[0] != 0) {
		bin->sum_self  = 0;
		bin->sum_total = 0;
		bin->starts    = 0;
		bin->aborts    = 0;
		bin->bcode     = 0;
	    }
	}

	assert(timeR_current_mblock == timeR_measureblocks[0]); // we shouldn't have many active timers here

	/* reset all stack entries */
	start_time = tr_now();

	for (unsigned int i = 1; i < timeR_next_mindex; i++) {
	    timeR_current_mblock[i].start     = start_time;
	    timeR_current_mblock[i].lower_sum = 0;
	}
    }
}

void timeR_finish(void) {
    unsigned int i;

    if (timeR_current_mblockidx != 0 || timeR_next_mindex != 1) {
	/* manually build a mptr to the first timer */
	tr_measureptr_t fini = {
	    timeR_measureblocks[0], 1 // timer 0 is a canary value
	};

	/* end every remaining timer */
	timeR_end_timer(&fini);
    }

    end_time = tr_now();

    /* run a second overhead test with a large number of iterations */
    for (i = 0; i < 1000; i++) {
	BEGIN_TIMER(TR_OverheadTest2);
	END_TIMER(TR_OverheadTest2);
    }

    /* dump data to file */
    FILE *fd;

    if (timeR_output_file == NULL)
	return;

    fd = fopen(timeR_output_file, "w");
    if (fd == NULL)
	return;

    timeR_dump(fd);

    // FIXME: Check for errors
    fclose(fd);
}

void timeR_measureblock_full(void) {
    /* current block is now full, switch to the next one */
    timeR_current_mblockidx++;

    /* check for overflow */
    if (timeR_current_mblockidx == TIME_R_MAX_MBLOCKS) {
        /* abort here - timeR_measureblocks can't be realloced because all
           existing tr_measureptr_t reference into it */
        fprintf(stderr, "ERROR: Too many timers allocated!\n"
                "increase TIME_R_MAX_MBLOCKS and recompile\n");
        abort();
    }

    /* allocate a new block if neccessary */
    if (timeR_measureblocks[timeR_current_mblockidx] == NULL) {
        timeR_measureblocks[timeR_current_mblockidx] = malloc(sizeof(tr_timer_t) * TIME_R_MBLOCK_SIZE);
        if (timeR_measureblocks[timeR_current_mblockidx] == NULL) {
            fprintf(stderr, "ERROR: Timer block allocation failed!\n");
            abort();
        }
        max_mbindex = timeR_current_mblockidx;
    }

    timeR_current_mblock = timeR_measureblocks[timeR_current_mblockidx];
    timeR_next_mindex    = 0;
}

void timeR_end_timers_slowpath(const tr_measureptr_t *mptr, timeR_t when) {
    /* loop until the passed timer has been processed */
    do {
	timeR_end_latest_timer(when);

	/* update abort counter if this isn't the top timer */
	if (timeR_current_mblock != mptr->curblock ||
	    timeR_next_mindex    != mptr->index)
            timeR_bins[timeR_current_mblock[timeR_next_mindex].bin_id].aborts++;

    } while (timeR_current_mblock != mptr->curblock ||
             timeR_next_mindex    != mptr->index);
}

unsigned int timeR_add_userfn_bin(void) {
    /* check if there are bins available */
    if (next_bin >= bin_count) {
        // FIXME: consider switching to (limited?) exponential resizes
	tr_bin_t *newbins =
	    realloc(timeR_bins, (bin_count + TIME_R_REALLOC_BINS) *
	                  sizeof(tr_bin_t));

	if (newbins == NULL)
	    /* realloc failed, return the fallback */
	    return TR_UserFuncFallback;

	/* clear the new entries */
	memset(newbins + bin_count, 0, sizeof(tr_bin_t) * TIME_R_REALLOC_BINS);

	/* update bookkeeping */
	timeR_bins = newbins;
	bin_count += TIME_R_REALLOC_BINS;
    }

    return next_bin++;
}

void timeR_name_bin(unsigned int bin_id, const char *name) {
    /* create permanent copy of name */
    char *copy = strdup(name);
    if (!copy)
        copy = userfunc_unknown_str;

    if (timeR_bins[bin_id].name != NULL)
        free(timeR_bins[bin_id].name);

    timeR_bins[bin_id].name = copy;
}

/* set a bin name for an anonymous function */
void timeR_name_bin_anonfunc(unsigned int bin_id, const char *filename,
                             unsigned int line, unsigned int pos) {
  char nametmp[1024];

  nametmp[sizeof(nametmp) - 1] = 0;
  snprintf(nametmp, sizeof(nametmp), "%s:<anon function defined in line %d column %d>",
           filename, line, pos);
  timeR_name_bin(bin_id, nametmp);
}

/* explicitly stop timers if a SETJMP returns */
/* This function is not inlined because it is assumed that */
/* it will only rarely be called.                          */
void timeR_release(tr_measureptr_t *marker) {
    /* check if anything needs to be done at all */
    if (marker->curblock == timeR_current_mblock &&
        marker->index    == timeR_next_mindex)
        return;

    /* marker points to an allocated measurement, end it */
    timeR_end_timer(marker);
}

/* debug aid: dump the current timer stack to stderr */
void timeR_dump_timer_stack(void) {
    tr_timer_t  *cur_mblock = timeR_measureblocks[0];
    unsigned int mbidx      = 0;
    unsigned int idx        = 1;
    unsigned int i          = 0;

    fprintf(stderr,"--- current timer stack:\n");

    while (mbidx != timeR_current_mblockidx ||
	   idx   != timeR_next_mindex) {
	fprintf(stderr, "%3d: %3d (%s)\n", i,
		cur_mblock[idx].bin_id,
		timeR_bins[cur_mblock[idx].bin_id].name);
	i++;
	idx++;
	if (idx >= TIME_R_MBLOCK_SIZE) {
	    idx = 0;
	    mbidx++;
	    cur_mblock = timeR_measureblocks[mbidx];
	}
    }
}


/*** external function timing ***/

/* adapted version of djbhash */
static unsigned int hash_address(const void *addr) {
    unsigned int hash = 5381;
    unsigned int i    = 0;
    char data[sizeof(addr)];

    memcpy(data, &addr, sizeof(addr));
    for (i = 0; i < sizeof(addr); i++) {
	hash = ((hash << 5) + hash) + data[i];
    }

    return hash;
}

/* look up bin id for external function in our map, return 0 if not found */
static unsigned int lookup_extfunc(void *addr) {
    unsigned int hash = hash_address(addr);
    unsigned int modhash = hash % extfunc_map_length;

    if (extfunc_map[modhash].addr == addr) {
	/* found entry in map */
	return extfunc_map[modhash].bin_id;
    }

    return 0;
}

static unsigned int lookupadd_extfunc(char *name, void *addr);

/* add bin for external function to our map */
static unsigned int add_extfunc(char *name, void *addr) {
    unsigned int hash = hash_address(addr);
    unsigned int modhash = hash % extfunc_map_length;

    if (extfunc_map[modhash].addr == NULL) {
	/* did not find an entry, but an empty slot */
	unsigned int bin_id = timeR_add_userfn_bin();
	timeR_name_bin(bin_id, name);
	timeR_bins[bin_id].prefix = "<ExternalCode>";

	extfunc_map[modhash].addr   = addr;
	extfunc_map[modhash].bin_id = bin_id;
	extfunc_map_entries++;

	return bin_id;
    }

    /* found a collision */
    // Rebuild hash table with larger size and retry
    // FIXME: There are better algorithms for this
    tr_extfunc_entry_t *newmap;
    unsigned int newlength = extfunc_map_length + TIME_R_EXTFUNC_MAP_STEP;

 retry:
    newmap = calloc(newlength, sizeof(tr_extfunc_entry_t));
    if (newmap == NULL) {
	abort();
    }

    for (unsigned int i = 0; i < extfunc_map_length; i++) {
	if (extfunc_map[i].addr != NULL) {
	    unsigned int newhash = hash_address(extfunc_map[i].addr) % newlength;

	    if (newmap[newhash].addr != NULL) {
		/* collision on rehashing */
		free(newmap);
		newlength += TIME_R_EXTFUNC_MAP_STEP;
		goto retry;
	    }

	    newmap[newhash].addr   = extfunc_map[i].addr;
	    newmap[newhash].bin_id = extfunc_map[i].bin_id;
	}
    }

    /* switch to new map */
    free(extfunc_map);
    extfunc_map        = newmap;
    extfunc_map_length = newlength;

    /* use a recursive call for the actual insertion */
    return lookupadd_extfunc(name, addr);
}

/* look up bin id for external function in our map, add if not found */
static unsigned int lookupadd_extfunc(char *name, void *addr) {
    unsigned int bin_id = lookup_extfunc(addr);

    if (bin_id != 0)
	return bin_id;
    else
	return add_extfunc(name, addr);
}

tr_measureptr_t timeR_begin_external(char *name, void *addr) { // FIXME: should use DL_FUNC
    /* look up address in the map */
    BEGIN_TIMER(TR_HashOverhead);
    // TODO: Seperate overhead measurement, subtracted from total of other timers?
    unsigned int bin_id = lookupadd_extfunc(name, addr);
    END_TIMER(TR_HashOverhead);

    return timeR_begin_timer(bin_id);
}
