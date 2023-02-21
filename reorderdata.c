/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2021-2023 Peraton Labs Inc.
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
#include "reorderdata.h"

static bool reorder_extent_enabled = true;
static bool reorder_density_enabled = true;

/* Returns 0 on success, -1 on error */
int reorderdata_init(bool measure_reorder_extent, bool measure_reorder_density)
{
    reorder_extent_enabled = measure_reorder_extent;
    reorder_density_enabled = measure_reorder_density;

    return 0;
}

static void reorderdata_accumulate(struct reorderDataR *accum,
                                   struct reorderDataR *unit)
{
    if (reorder_extent_enabled) {
        /* Combine histogram buckets */
        for (int i = 0; i < REORDER_MAX_EXTENT; i++) {
            accum->extentToCount[i] += unit->extentToCount[i];
        }
        /* Sum the assumed drops */
        accum->extent_assumed_drops += unit->extent_assumed_drops;
    }

    if (reorder_density_enabled) {
        for (int i = 0; i < REORDER_WINDOW_SIZE; i++) {
            accum->FD[i] += unit->FD[i];
        }
        /* Sum the assumed drops */
        accum->rd_assumed_drops += unit->rd_assumed_drops;
    }
}

void reorderdata_accumulate_time(struct reorderDataR *accum, struct reorderDataR *unit)
{
    reorderdata_accumulate(accum, unit);
}

void reorderdata_accumulate_flows(struct reorderDataR *accum, struct reorderDataR *unit)
{
    reorderdata_accumulate(accum, unit);
}

/* Since we're just looking for the sequence number, we can use a
 * plain old comparison function and don't need to worry about
 * wraparound. */
static int mp_compare(const void *key, const RBTreeNode *node)
{
    struct reorderMissingPacket *mp;
    SEQNO seq;

    seq = *(SEQNO *)key;
    mp = rbtree_entry(node, struct reorderMissingPacket, n);

    if (seq < mp->seq) {
        return -1;
    }

    if (seq > mp->seq) {
        return 1;
    }

    return 0;
}

/* Since we're just looking for the sequence number, we can use a
 * plain old comparison function and don't need to worry about
 * wraparound. */
static int buffer_compare(const void *key, const RBTreeNode *node)
{
    SEQNO seq;
    struct rdBufferEntry *be;

    seq = *(SEQNO *)key;
    be = rbtree_entry(node, struct rdBufferEntry, n);

    if (seq < be->seq) {
        return -1;
    }

    if (seq > be->seq) {
        return 1;
    }

    return 0;
}

static int rd_maybe_add_seq_to_window(struct rdState *RD, SEQNO i)
{
    QueueNode *cursor;
    struct rdWindowEntry *e;

    queue_for_each(cursor, &RD->window) {
        e = queue_entry(cursor, struct rdWindowEntry, n);
        /* Don't add a duplicate. Return the old size. */
        if (e->seq == i) {
            return queue_size(&RD->window);
        }
    }

    e = malloc(sizeof(*e));
    if (!e) {
        fprintf(stderr, "malloc failed\n");
        return -1;
    }
    e->seq = i;
    queue_push(&RD->window, &e->n);

    return queue_size(&RD->window);
}

static int rd_window_contains(struct rdState *state, SEQNO seq)
{
    QueueNode *cursor;

    queue_for_each(cursor, &state->window) {
        struct rdWindowEntry *e = queue_entry(cursor, struct rdWindowEntry, n);
        if (e->seq == seq) {
            return 1;
        }
    }

    return 0;
}

static int rd_buffer_contains(struct rdState *state, SEQNO seq)
{
    if (rbtree_lookup_key(&state->buffer, &seq)) {
        return 1;
    }

    return 0;
}

static int rd_add_seq_to_window(struct rdState *RD, SEQNO i)
{
    struct rdWindowEntry *e;

    e = malloc(sizeof(*e));
    if (!e) {
        fprintf(stderr, "malloc failed\n");
        return -1;
    }

    e->seq = i;
    queue_push(&RD->window, &e->n);

    return 0;
}

static void rd_maybe_add_new_arrival_to_window(struct rdState *state, SEQNO seq)
{
    /* Wrong state */
    if (state->state == 0) {
        return;
    }

    if (seq >= state->RI &&
        !rd_window_contains(state, seq) &&
        !rd_buffer_contains(state, seq)) {
        rd_add_seq_to_window(state, seq);
        state->state = 0;
    }
}

static void rd_record_distance(struct reorderDataR *dr, ReorderDistance D)
{
    static int lower = REORDER_DT * -1;
    static int upper = REORDER_DT;
    int index;

    if (D < lower || D > upper) {
        return;
    }

    // lower maps to 0
    // upper maps to REORDER_DT * 2
    index = D + REORDER_DT;
    dr->FD[index]++;
}

static void rd_maybe_delete_from_buffer(struct rdState *state, SEQNO seq)
{
    RBTreeNode *n;

    n = rbtree_lookup_key(&state->buffer, &seq);
    if (n) {
        struct rdBufferEntry *e = rbtree_entry(n, struct rdBufferEntry, n);
        rbtree_remove(&state->buffer, n);
        free(e);
    }
}

static int rd_add_to_buffer(struct rdState *state, SEQNO seq)
{
    struct rdBufferEntry *e;

    e = malloc(sizeof(*e));
    if (!e) {
        fprintf(stderr, "malloc failed\n");
        return -1;
    }

    e->seq = seq;
    rbtree_insert(&state->buffer, &e->seq, &e->n);

    return 0;
}

static SEQNO rd_window_min(struct rdState *state)
{
    QueueNode *cursor;
    struct rdWindowEntry *e;
    SEQNO min = (SEQNO)-1;

    queue_for_each(cursor, &state->window) {
        e = queue_entry(cursor, struct rdWindowEntry, n);
        if (e->seq < min) {
            min = e->seq;
        }
    }

    return min;
}

static SEQNO rd_buffer_min(struct rdState *state)
{
    RBTreeNode *n;
    struct rdBufferEntry *e;
    SEQNO min = (SEQNO)-1;

    n = rbtree_first(&state->buffer);
    if (n) {
        e = rbtree_entry(n, struct rdBufferEntry, n);
        min = e->seq;
    }

    return min;
}

static void rd_advance_RI(struct rdState *state, struct reorderDataR *dr)
{
    SEQNO s1, s2, m;
    (void) dr;

    s1 = rd_window_min(state);
    s2 = rd_buffer_min(state);

    m = (s1 <= s2) ? s1 : s2;
    if (state->RI < m) {
        state->RI = m;
    }
    else {
        state->RI++;
    }
}

static void rd_process_next_packet(struct rdState *state, struct reorderDataR *dr)
{
    if (rd_window_contains(state, state->RI) || rd_buffer_contains(state, state->RI)) {
        ReorderDistance D, AD;
        QueueNode *qn = queue_pop(&state->window);
        struct rdWindowEntry *e = queue_entry(qn, struct rdWindowEntry, n);
        D = state->RI - e->seq;
        AD = (D >= 0) ? D : -D;

        /* Displacement within threshold */
        if (AD <= REORDER_DT) {
            rd_record_distance(dr, D);
            rd_maybe_delete_from_buffer(state, state->RI);
            if (D < 0) {
                rd_add_to_buffer(state, e->seq);
            }
            state->RI++;
        }
        /* Displacement beyond threshold */
        else {
        }
        free(e);

        /* Signal that we're looking for the next arrival */
        state->state = 1;
    }
    else {
        /* RI may have been dropped. */
        rd_advance_RI(state, dr);
        state->state = 0;  // Not looking for next arrival yet
    }
}

static int reorderdata_record_missing_packet(struct reorderState *rstate,
                                             SEQNO seq, PACKETCOUNT refIndex)
{
    struct reorderMissingPacket *mp;

    mp = malloc(sizeof(*mp));
    if (!mp) {
        fprintf(stderr, "malloc failed\n");
        return -1;
    }

    mp->seq = seq;
    mp->observed = 0;
    mp->refIndex = refIndex;
    mp->extent = -1;

    rbtree_insert(&rstate->missingPackets, &mp->seq, &mp->n);

    return 0;
}

/* Assumes numArrivals has already been incremented. Decrements
 * numArrivals upon detecting a duplicate packet. */
static void reorderdata_resolve_missing_packet(struct reorderDataR *dr,
                                               struct reorderState *rstate,
                                               SEQNO seq)
{
    RBTreeNode *node;
    struct reorderMissingPacket *mp;
    PACKETCOUNT arrivalIndex;

    node = rbtree_lookup_key(&rstate->missingPackets, &seq);
    if (!node) {
        return;
    }
    mp = rbtree_entry(node, struct reorderMissingPacket, n);

    arrivalIndex = rstate->numArrivals;

    if (!mp->observed) {
        /* Compute the extent, capped at the configured maximum */
        int extent = arrivalIndex - mp->refIndex;
        if (extent > REORDER_MAX_EXTENT) {
#ifdef REORDER_DEBUG
            fprintf(stderr, "Capping real extent %d to %d\n", extent, REORDER_MAX_EXTENT);
#endif
            extent = REORDER_MAX_EXTENT;
        }
        mp->observed = 1;
        dr->extentToCount[extent]++;
    }
    else {
        rstate->numArrivals--;
    }
}

static void reorderdata_prune_missing_packets(struct reorderDataR *dr,
                                              struct reorderState *rstate)
{
    RBTreeNode *cursor, *backup;

    rbtree_for_each_safe(cursor, backup, &rstate->missingPackets) {
        struct reorderMissingPacket *mp;
        mp = rbtree_entry(cursor, struct reorderMissingPacket, n);
        if ((seqcmp(mp->seq, rstate->nextExp) < 0) &&
            (modular_distance(mp->seq, rstate->nextExp) > REORDER_MAX_HISTORY)) {
            if (!mp->observed) {
                dr->extent_assumed_drops += 1;
            }
            rbtree_remove(&rstate->missingPackets, cursor);
            free(mp);
        }
    }
}

void reorderdata_a2r(struct reorderDataR *dr, struct reorderDataA *da,
                     struct reorderState *rstate)
{
    struct seqnoRange *r;

    /* Update rstate as we process packets for this stream. This
     * produces a set of missing packet records, some of which end up
     * as observed, with a computed extent. These are the reordered
     * packets. Populate a per-stream histogram.
     *
     * In accumulate_time(), combine the per-stream-record histograms
     * together to build a single histogram for the stream.
     *
     * We may have multiple streams for the same flow, and we want to
     * consolidate the information across streams. We want to report
     * the number of packets in each bucket, so we accumulate the
     * per-stream records into per-flow records in accumulate_flows():
     * Populate a common histogram at the flow level. */

    for (r = da->ranges.head; r; r = r->next) {
        PACKETCOUNT range_size = (r->high - r->low + 1);

        /* Special case of first packet */
        if (!rstate->initialized) {
            /* EXTENT: initialize nextExp and missingPackets */
            if (reorder_extent_enabled) {
                rstate->nextExp = r->low;
                rbtree_init(&rstate->missingPackets, mp_compare, NULL, NULL);
            }

            /* RD */
            if (reorder_density_enabled) {
                rstate->RD.state = 0;
                rstate->RD.window_initialized = 0;
                rstate->RD.RI = 0;
                queue_init(&rstate->RD.window);
                rbtree_init(&rstate->RD.buffer, buffer_compare, NULL, NULL);
            }

            rstate->initialized = 1;
        }

        if (reorder_density_enabled) {
            /* Update reorder distances */
            for (SEQNO i = r->low; i <= r->high; i++) {
                int processed_this = 0;

                /* Try to initialize the window if not already initialized */
                if (!rstate->RD.window_initialized) {
                    int num_unique = rd_maybe_add_seq_to_window(&rstate->RD, i);
                    /* Something went terribly wrong */
                    if (num_unique == -1) {
                        return;
                    }

                    if (num_unique == REORDER_DT + 1) {
                        rstate->RD.RI = 0;
                        rstate->RD.window_initialized = 1;
                    }
                }

                /* Window not yet initialized. Move along. */
                if (!rstate->RD.window_initialized) {
                    continue;
                }

                /* We're still looking for a new window item. Try to add
                 * this seq. */
                if (rstate->RD.state == 1) {
                    rd_maybe_add_new_arrival_to_window(&rstate->RD, i);
                    processed_this = 1;
                }

                /* Process one window item if we're in the right state */
                if (rstate->RD.state == 0) {
                    rd_process_next_packet(&rstate->RD, dr);
                }

                /* We're looking for a new window item. Try to add
                 * this seq. */
                if (rstate->RD.state == 1 && !processed_this) {
                    rd_maybe_add_new_arrival_to_window(&rstate->RD, i);
                }
            }
        }

        if (reorder_extent_enabled) {
            /* Case 1: This range of packets is in order, with or without
             * a sequence discontinuity. Jump all the counters ahead. If
             * there is a discontinuity, then create a missing packet
             * range. */
            if (seqcmp(r->low, rstate->nextExp) >= 0) {
                if (seqcmp(r->low, rstate->nextExp) > 0) {
                    PACKETCOUNT refIndex = rstate->numArrivals + 1;
                    for (SEQNO i = rstate->nextExp; i != r->low; i++) {
                        reorderdata_record_missing_packet(rstate, i, refIndex);
                    }
                }
                rstate->nextExp = (r->high + 1);
                rstate->numArrivals += range_size;
                dr->extentToCount[0] += range_size;
                continue;
            }

            /* Case 2: The first packet in the range is reordered.
             * Iterate over each packet in the range. If the packet is
             * missing and already observed, then ignore as duplicate. If
             * the packet is missing but not yet observed, then mark it as
             * observed and compute the extent. Update the histogram. */
            for (SEQNO i = r->low; i <= r->high; i++) {
                rstate->numArrivals++;
                /* Packet is in order */
                if (seqcmp(i, rstate->nextExp) >= 0) {
                    rstate->nextExp = i + 1;
                    dr->extentToCount[0]++;
                }
                /* Packet is reordered */
                else {
                    reorderdata_resolve_missing_packet(dr, rstate, i);
                }
            }
        }
    }

    /* EXTENT: Prune old missing packet records */
    if (reorder_extent_enabled) {
        reorderdata_prune_missing_packets(dr, rstate);
    }
}

int reorderdata_arrival(struct reorderDataA *rd, SEQNO seqno, struct seqnoRangeList *free_ranges)
{
    struct seqnoRange *newrange;

    if (rd->ranges.tail && rd->ranges.tail->high == seqno - 1 && seqno != 0) {
        rd->ranges.tail->high = seqno;  /* next packet in sequence, no wraparound */
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
        newrange->next = NULL;
        if (!rd->ranges.tail) {
            rd->ranges.tail = newrange;
            rd->ranges.head = newrange;
        }
        else {
            rd->ranges.tail->next = newrange;
            rd->ranges.tail = newrange;
        }
    }

    return 0;
}

void reorderdata_destroy_missing_packets(RBTree *missingPackets)
{
    RBTreeNode *cursor, *backup;

    rbtree_for_each_safe(cursor, backup, missingPackets) {
        struct reorderMissingPacket *mp;
        mp = rbtree_entry(cursor, struct reorderMissingPacket, n);
        rbtree_remove(missingPackets, cursor);
        free(mp);
    }
}

void reorderdata_destroy_rd_buffer(RBTree *buffer)
{
    RBTreeNode *cursor, *backup;

    rbtree_for_each_safe(cursor, backup, buffer) {
        struct rdBufferEntry *entry = rbtree_entry(cursor, struct rdBufferEntry, n);
        rbtree_remove(buffer, cursor);
        free(entry);
    }
}

void reorderdata_destroy_rd_window(Queue *window)
{
    if (!window) {
        return;
    }

    while (!queue_empty(window)) {
        QueueNode *qn = queue_pop(window);
        struct rdWindowEntry *e = queue_entry(qn, struct rdWindowEntry, n);
        free(e);
    }
}
