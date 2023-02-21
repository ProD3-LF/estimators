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

#include <stdlib.h>
#include "datatypes.h"

void move_seqnorangelist(struct seqnoRangeList *to, struct seqnoRangeList *from)
{
    if (!from->head) {
        return;    /* nothing to move */
    } else if (!to->head) {    /* replace */
        to->head = from->head;
        to->tail = from->tail;
    } else {    /* append */
        to->tail->next = from->head;
        to->tail = from->tail;
    }
    from->head = NULL;
    from->tail = NULL;
}

void free_seqnorangelist(struct seqnoRangeList *l)
{
    struct seqnoRange *r;

    if (!l) {
        return;
    }

    r = l->head;

    while (r) {
        struct seqnoRange *victim;
        victim = r;
        r = r->next;
        free(victim);
    }
}

int seqcmp(SEQNO s, SEQNO t)
{
    SEQNO diff;
    static SEQNO upper = (SEQNO) 1 << (sizeof(s) * 8 - 1);

    diff = t - s;
    if (diff > 0 && diff < upper) {
        return -1;
    }
    if (s == t) {
        return 0;
    }

    return 1;
}

SEQNO modular_distance(SEQNO s, SEQNO t)
{
    if (t >= s) {
        return t - s;
    }

    return (t - s + ((SEQNO)-1));
}
