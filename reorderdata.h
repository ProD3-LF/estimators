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

#ifndef _PD3_ESTIMATOR_REORDERDATA_H_
#define _PD3_ESTIMATOR_REORDERDATA_H_

#include "datatypes.h"
#include "rbtree.h"
#include "queue.h"

#define REORDER_MAX_HISTORY (REORDER_MAX_EXTENT * 2)

typedef int ReorderDistance;

struct reorderDataA {
    struct seqnoRangeList ranges;  /* linked with next pointer */
};

struct reorderDataR {
    /* Extents learned by processing the packets in this stream
     * record. In-order packets have extent 0. Reordered packets have
     * extent computed based on missed packet information. */
    PACKETCOUNT extentToCount[REORDER_MAX_EXTENT + 1];

    /* Reorder Distance metric */
    PACKETCOUNT FD[REORDER_WINDOW_SIZE]; /* Frequency of lateness and earliness */

    /* Packets that we assume to be dropped because they were recorded
     * as missing but never observed */
    PACKETCOUNT extent_assumed_drops;
    PACKETCOUNT rd_assumed_drops;
};

/* Keyed by SEQNO */
struct reorderMissingPacket {
    /* Key */
    SEQNO seq;

    /* Has this packet been observed? */
    uint8_t observed;

    /* Reference index for this missing packet */
    PACKETCOUNT refIndex;

    /* Computed extent, initialized to -1 */
    int extent;

    /* To allow insertion in rbtree */
    RBTreeNode n;
};

struct rdWindowEntry {
    /* The key */
    SEQNO seq;

    QueueNode n;
};

struct rdBufferEntry {
    /* The key */
    SEQNO seq;

    /* To allow insertion into rbtree */
    RBTreeNode n;
};

struct rdState {
    /* 0 means processing window items, 1 means looking for next
     * arrival */
    int state;

    /* Receive index */
    SEQNO RI;

    /* Has the window been initialized with DT+1 unique sequence
     * numbers? */
    uint8_t window_initialized;

    Queue window;
    RBTree buffer;
};

struct reorderState {
    /* Has the state been initialized yet? */
    uint8_t initialized;

    /*------Extent data structures-----*/

    /* How many packets have arrived on this stream so far? */
    uint64_t numArrivals;

    /* Next expected sequence number */
    SEQNO nextExp;

    /* Stores missing packet records */
    RBTree missingPackets;

    /*------Reorder-Distance data structures-----*/
    struct rdState RD;
};

/* Returns 0 on success, -1 on error */
int reorderdata_init(bool measure_reorder_extent, bool measure_reorder_density);

/* Invoked by aggregator. Returns 0 on success, -1 on error */
int reorderdata_arrival(struct reorderDataA *rd, SEQNO seqno, struct seqnoRangeList *free_ranges);

/* Invoked by reporter */
void reorderdata_a2r(struct reorderDataR *dr, struct reorderDataA *da,
                     struct reorderState *rstate);

/* Accumulate over multiple per-stream records */
void reorderdata_accumulate_time(struct reorderDataR *accum,
                                 struct reorderDataR *unit);

/* Accumulate per-stream records into a flow record */
void reorderdata_accumulate_flows(struct reorderDataR *accum,
                                  struct reorderDataR *unit);

void reorderdata_destroy_missing_packets(RBTree *missingPackets);
void reorderdata_destroy_rd_buffer(RBTree *buffer);
void reorderdata_destroy_rd_window(Queue *window);

#endif /* _PD3_ESTIMATOR_REORDERDATA_H */
