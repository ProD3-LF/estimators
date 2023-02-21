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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lossdata.h"
#include "datatypes.h"
#include "hashmap2.h"

// N bytes * 8 bits/byte / 2 (half the space) = N * 4
#define WRAPAROUND    (((SEQNO) 1) << (sizeof(SEQNO) * 4))

int lossdata_init()
{
    return 0;
}

static void lossdata_accumulate(struct lossDataR *accum, struct lossDataR *unit)
{
    if (accum->received + accum->dropped != 0 &&
        unit->received + unit->dropped != 0) {
        accum->received += unit->received;
        accum->dropped += unit->dropped;
        accum->consecutive_drops += unit->consecutive_drops;
        accum->gap_total += unit->gap_total;
        accum->gap_count += unit->gap_count;
        accum->gap_min = min(accum->gap_min, unit->gap_min);
        accum->gap_max = max(accum->gap_max, unit->gap_max);
    } else if (unit->received + unit->dropped != 0) {
        memcpy(accum, unit, sizeof(*unit));
    }
}

void lossdata_accumulate_time(struct lossDataR *accum, struct lossDataR *unit)
{
    lossdata_accumulate(accum, unit);
    accum->flowstate = flowstate_concatenate(accum->flowstate, unit->flowstate);
}

void lossdata_accumulate_flows(struct lossDataR *accum, struct lossDataR *unit)
{
    if (flowstate_error(unit->flowstate)) {
        accum->badflows++;
    } else {
        lossdata_accumulate(accum, unit);
    }
}

static void lossdata_a2r_begin(struct lossDataR *ldr, struct lossDataA *lda)
{
    struct seqnoRange *r;

    for (r = lda->ranges.head; r; r = r->next) {
        r->wraparound = 0;
        r->arrival_period = ARR_PRESENT;    /* can't trust prior setting */
        r->next_r = r->next;
    }
    ldr->ranges.head = lda->ranges.head;
    ldr->ranges.tail = lda->ranges.tail;
}

static void lossdata_a2r_add(struct lossDataR *ldr, struct seqnoRange *r,
                             enum arrivalPeriod arr)
{
    r->wraparound = 0;
    r->arrival_period = arr;
    r->next_r = ldr->ranges.head;
    ldr->ranges.head = r;
    if (!ldr->ranges.tail) {
        ldr->ranges.tail = r;
    }
}

static int rangecmp(const void *x, const void *y)
{
    struct seqnoRange *rx, *ry;

    rx = *((struct seqnoRange **) x);
    ry = *((struct seqnoRange **) y);
    if (rx->wraparound != ry->wraparound) {
        return (ry->wraparound - rx->wraparound);
    } else {
        return (rx->low - ry->low);
    }
}

static struct seqnoRange **a2rarray;
static unsigned int a2rarraysize;
static uint8_t lossdata_a2r_compute(struct lossDataR *ldr, struct lossState *state,  SEQNO *present_high)
{
    struct seqnoRange **array;
    unsigned int arraysize;
    unsigned int n, i, begin, end, gap, recd;
    struct seqnoRange *r;

    /* count ranges and make sure array is large enough */
    for (n = 0, r = ldr->ranges.head; r; n++, r = r->next_r) {
    }

    if (n == 0) {
        return 0;
    }

    array = a2rarray;
    arraysize = a2rarraysize;

    if (n > arraysize) {
        if (array) {
            free(array);
        }
        array = a2rarray = calloc(n, sizeof(struct seqnoRange *));
        a2rarraysize = n;
    }

    /* copy to array and sort */
    for (i = 0, r = ldr->ranges.head; r; i++, r = r->next_r) {
        array[i] = r;
    }

    qsort(array, n, sizeof(struct seqnoRange *), rangecmp);

    /* detect wraparound */
    if (WRAPAROUND != 0) {
        for (i = 0; i < n; i++) {
            r = array[i];
            r->wraparound = 1;    /* harmless if all marked as wrapping around */
            if (i < n - 1 && array[i+1]->low - r->high > (WRAPAROUND / 2)) {
                qsort((void *) array, n, sizeof (struct seqnoRange *), rangecmp);
                break;
            }
        }
    }

#ifdef RANGE_DEBUG
    for (i = 0; i < n; i++) {
        r = array[i];
        fprintf(stderr, "Range %u: [%u, %u]\n", i, r->low, r->high);
    }
    fprintf(stderr, "\n");
#endif

    /* begin at [0] or first array element after ARR_PAST */
    /* end at final array element that isn't ARR_FUTURE */
    for (begin = 0, end = n, i = 0; i < n; i++) {
        if (array[i]->arrival_period == ARR_PAST) {
            begin = i + 1;
        }
        if (array[i]->arrival_period != ARR_FUTURE) {
            end = i;
        }
    }
    if (end >= n) {
        return (0);
    }

    /* compute */
    SEQNO base;

    /* If this is the first range, pretend like we got the packet just
     * before this one so that we're sure to process this one. */
    if (!state->has_last_range) {
        memset(&state->last_range, 0, sizeof(state->last_range));
        state->last_range.low = array[begin]->low - 1;
        state->last_range.high = array[begin]->low - 1;
        state->has_last_range = 1;
    }

    /* Base from which we compute distances */
    base = state->last_range.high;
    for (i = begin; i <= end; i++) {
        struct seqnoRange *prev = &state->last_range;;
        r = array[i];
        SEQNO d_prev_high = modular_distance(base, prev->high);
        SEQNO d_this_low = modular_distance(base, r->low);
        SEQNO d_this_high = modular_distance(base, r->high);

        /* If this range overlaps with the previous one */
        if (d_this_low <= d_prev_high) {
            /* This range is subsumed by the previous one. Skip it. */
            if (d_this_high <= d_prev_high) {
                continue;
            }
            /* Otherwise, rewrite the low side of this range to be one
             * more than the overlap point. Example: (1, 5), (4,
             * 6). When considering (4, 6), rewrite the low end to
             * 6. Since the numbers are unsigned, we get modular
             * arithmetic for free. */
            r->low = min(r->high, prev->high) + 1;
        }
        /* Make sure we don't wrap around to base. Since the numbers
         * are unsigned, we get modular arithmetic for free. */
        if (r->high < r->low) {
            r->high = base - 1;
        }

        recd = r->high - r->low + 1;    /* shouldn't be 0 */

        SEQNO distance = modular_distance(prev->high, r->low);
        /* Example: (x, 4), (7, y) --> distance = 3, gap = 2 (for
         * sequence numbers 5 and 6) */
        gap = (distance > 0) ? distance - 1 : 0;

        /* Update the last processed range */
        state->last_range = *r;

        /* Update the tallies */
        ldr->received += recd;
        ldr->dropped += gap;

        if (gap > 1) {
            ldr->consecutive_drops += gap - 1;
        }
        if (gap > 0) {
            if (ldr->gap_count == 0 || gap < ldr->gap_min) {
                ldr->gap_min = gap;
            }
            if (ldr->gap_count == 0 || gap > ldr->gap_max) {
                ldr->gap_max = gap;
            }
            ldr->gap_total += gap;
            ldr->gap_count++;
        }
    }

    /* cleanup */
    *present_high = array[end]->high;
    for (r = ldr->ranges.head; r; r = r->next_r) {
        r->wraparound = 0;
        r->arrival_period = ARR_PRESENT;
        r->next_r = NULL;
    }
    ldr->ranges.head = NULL;
    ldr->ranges.tail = NULL;

    return 1;
}

void lossdata_destroy_a2r_compute_array()
{
    if (a2rarray) {
        free(a2rarray);
        a2rarray = NULL;
        a2rarraysize = 0;
    }
}

void lossdata_a2r(struct lossDataR *ldr, struct lossDataA *lda,
                  struct lossState *lstate, void *future, void *key,
                  unsigned int periods_to_wait)
{
    struct seqnoRange past_seqno, *range;
    unsigned int i;
    struct hashMap *hm_f;
    struct hashMapItem *hmi_af;
    SEQNO present_high;

    /* initialize with data from current aggregator period */
    lossdata_a2r_begin(ldr, lda);
    ldr->flowstate = lda->flowstate;

    /* create fake range for past, if not delimited */
    if (flowstate_beginp(lda->flowstate) && lstate->has_high_seqno) {
        past_seqno.low = lstate->high_seqno;
        past_seqno.high = lstate->high_seqno;
        lossdata_a2r_add(ldr, &past_seqno, ARR_PAST);
    }

    /* link in ranges from the future */
    for (i = 1, hm_f = future; i < periods_to_wait && hm_f; i++, hm_f = hm_f->next) {
        hmi_af = hashmap_retrieve(hm_f, (struct hashMapKey *) key);
        if (hmi_af) {
            for (range = hmi_af->value.agg_data.loss.ranges.head; range; range = range->next) {
                lossdata_a2r_add(ldr, range, ARR_FUTURE);
            }
        }
    }

    /* compute loss-related metric values, store high seqno for next time */
    lstate->has_high_seqno = 0;
    if (lossdata_a2r_compute(ldr, lstate, &present_high)) {
        lstate->has_high_seqno = 1;
        lstate->high_seqno = present_high;
    }
}

int lossdata_arrival(struct lossDataA *lda, SEQNO seqno, struct seqnoRangeList *free_ranges)
{
    struct seqnoRange *newrange;

    if (lda->ranges.head && lda->ranges.head->high == seqno - 1 && seqno != 0) {
        lda->ranges.head->high = seqno;  /* next packet in sequence, no wraparound */
    } else {
        if (free_ranges && free_ranges->head) {
            newrange = free_ranges->head;
            free_ranges->head = newrange->next;
            if (newrange == free_ranges->tail) {
                free_ranges->tail = NULL;
            }
        } else {
            newrange = malloc(sizeof(*newrange));
            if (!newrange) {
                fprintf(stderr, "malloc failed\n");
                return -1;
            }
        }
        memset(newrange, 0, sizeof(*newrange));
        newrange->low = seqno;
        newrange->high = seqno;
        newrange->next = lda->ranges.head;
        lda->ranges.head = newrange;
        if (!lda->ranges.tail) {
            lda->ranges.tail = newrange;
        }
    }
    lda->flowstate = flowstate_packet(lda->flowstate);

    return 0;
}
