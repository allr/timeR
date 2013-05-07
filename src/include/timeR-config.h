#ifndef TIME_R_CONFIG_H
#define TIME_R_CONFIG_H

/* number of timers allocated per block */
#define TIME_R_MBLOCK_SIZE    10000

/* maximum number of timer blocks allocated */
#define TIME_R_MAX_MBLOCKS    100

/* number additional bins allocated initially */
/* (~690 for R_FunTab)                        */
#define TIME_R_INITIAL_EMPTY_BINS 750

/* number of bins allocated at once when all existing are in use */
#define TIME_R_REALLOC_BINS 100

#endif
