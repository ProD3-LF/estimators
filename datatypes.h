/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2015-2023 Peraton Labs Inc.
 *
 * This software was developed in work supported by the following U.S.
 * Government contracts:
 *
 * HR0011-15-C-0098
 * HR0011-20-C-0160
 *
 * Any opinions, findings and conclusions or recommendations expressed in
 * this material are those of the author(s) and do not necessarily reflect
 * the views, either expressed or implied, of the U.S. Government.
 *
 * DoD Distribution Statement A
 * Approved for Public Release, Distribution Unlimited
 *
 * DISTAR Case 37651, cleared February 13, 2023.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 */

#ifndef _PD3_ESTIMATOR_DATATYPES_H_
#define _PD3_ESTIMATOR_DATATYPES_H_

#include "pd3_estimator.h"

enum arrivalPeriod {ARR_PAST, ARR_PRESENT, ARR_FUTURE};

struct seqnoRange {
  SEQNO low;
  SEQNO high;
  unsigned int wraparound;      /* 1 = before wraparound, 0 = after */
  enum arrivalPeriod arrival_period;
  struct seqnoRange *next;
  struct seqnoRange *next_r;    /* private to a2r() routines */
                                /* next_r eliminates additional storage */
};

struct seqnoRangeList {
  struct seqnoRange *head;
  struct seqnoRange *tail;
};

#ifndef min
#define min(_x, _y)    ((_x) <= (_y) ? (_x) : (_y))
#endif
#ifndef max
#define max(_x, _y)    ((_x) >= (_y) ? (_x) : (_y))
#endif

void move_seqnorangelist(struct seqnoRangeList *to, struct seqnoRangeList *from);
void free_seqnorangelist(struct seqnoRangeList *l);

int seqcmp(SEQNO s, SEQNO t);
SEQNO modular_distance(SEQNO s, SEQNO t);

#endif /* _PD3_ESTIMATOR_DATATYPES_H_ */
