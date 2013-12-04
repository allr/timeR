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

/* initial/increment size of external function map */
#define TIME_R_EXTFUNC_MAP_STEP 100

#endif
