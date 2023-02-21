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

#ifndef _PD3_ESTIMATOR_H_
#define _PD3_ESTIMATOR_H_

#include <stdint.h>
#include <stdbool.h>

/******************* Compile-time sizes **************************/

/* Size of key used to distinguish one logical flow from another */
#ifndef PD3_ESTIMATOR_KEY_SIZE
#define PD3_ESTIMATOR_KEY_SIZE 2
#endif

/* Reorder extent: maximum extent value that will be considered */
#ifndef REORDER_MAX_EXTENT
#define REORDER_MAX_EXTENT 255
#endif

/* Reorder density: Displacement threshold, i.e., maximum size of
 * buffer. Distance values go from -DT to +DT. */
#ifndef REORDER_DT
#define REORDER_DT 8
#endif
/*****************************************************************/


/******************** Types **************************************/
typedef uint32_t SEQNO;
typedef uint8_t STREAM_ID;
typedef uint64_t TIMESTAMP;
typedef uint64_t TIMEINTERVAL;
typedef uint32_t PACKETCOUNT;
/*****************************************************************/


/******************** Data Structures*****************************/

typedef struct stream_tuple {
    /* User-provided key used to distinguish one logical flow from
     * another */
    uint8_t flow_key[PD3_ESTIMATOR_KEY_SIZE];

    /* Identifier of a packet stream within the given flow. The tuple
     * (flow_key, stream_id) is assumed to uniquely identify a packet
     * stream. */
    STREAM_ID stream_id;
} stream_tuple;

/* Public structure for providing per-packet metadata to estimator
 * service */
typedef struct pd3_estimator_packet_info {
    stream_tuple stream;

    /* Sequence number of the packet within the stream */
    SEQNO seq;
} pd3_estimator_packet_info;

typedef struct pd3_estimator_loss_results {
    /* Number of packets received during the measurement
     * interval. Packets detected as duplicates do not count toward
     * the total. */
    double packets_received;

    /* Number of packets dropped during the measurement
     * interval. Packets detected as duplicates do not count toward
     * the total. */
    double packets_dropped;

    /* The loss value itself. */
    double value;

    /* Number of consecutive drops observed during the measurement
     * interval. A run of N contiguous dropped packets, for N >= 1,
     * increases the tally by (N - 1). */
    double consecutive_drops;

    /* Autocorrelation coefficient expressing how likely it is that
     * consecutive packets are both lost, normalized by the variance
     * (NOT by the probability that the first packet was lost). */
    double autocorr;
} pd3_estimator_loss_results;

typedef struct pd3_estimator_reorder_extent_results {
    /* Number of bins containing valid results */
    uint32_t num_bins;

    /* The bins making up the histogram. Only the first num_bins
     * entries are valid. Each entry contains the number of
     * non-duplicate packets observed with the given extent during the
     * measurement interval. */
    PACKETCOUNT bins[REORDER_MAX_EXTENT];

    /* The number of missing packets declared to be dropped during the
     * interval because their extent would exceed
     * REORDER_MAX_EXTENT. */
    PACKETCOUNT assumed_drops;
} pd3_estimator_reorder_extent_results;

typedef struct pd3_estimator_reorder_density_bin {
    /* Distance value, falling in the range -REORDER_DT to
     * REORDER_DT */
    int distance;

    /* Number of packets observed with the given distance during the
     * measurement interval. */
    PACKETCOUNT frequency;
} pd3_estimator_reorder_density_bin;

#define REORDER_WINDOW_SIZE (REORDER_DT * 2 + 1)
typedef struct pd3_estimator_reorder_density_results {
    /* Number of bins containing valid results */
    uint32_t num_bins;

    /* The bins making up the histogram. Only the first num_bins
     * entries are valid. */
    pd3_estimator_reorder_density_bin bins[REORDER_WINDOW_SIZE];
} pd3_estimator_reorder_density_results;

typedef struct pd3_estimator_results {
    /* Flow to which the results apply */
    uint8_t flow_key[PD3_ESTIMATOR_KEY_SIZE];

    /* Bounding timestamps for the received packets on which the
     * results are based */
    TIMESTAMP earliest;
    TIMESTAMP latest;

    /* Duration of the measurement interval */
    TIMEINTERVAL duration;

    /* Bounding sequence numbers for the results */
    SEQNO min_seq;
    SEQNO max_seq;

    /* Number of observed packets */
    PACKETCOUNT packet_count;

    /* Are the loss results valid? */
    bool loss;
    pd3_estimator_loss_results loss_results;

    /* Are the reorder extent results valid? */
    bool reorder_extent;
    pd3_estimator_reorder_extent_results reorder_extent_results;

    /* Are the reorder density results valid?*/
    bool reorder_density;
    pd3_estimator_reorder_density_results reorder_density_results;
} pd3_estimator_results;

typedef struct pd3_estimator_callbacks {
    /* Optional user-provided context pointer to be passed as an
     * argument to each callback */
    void *context;

    /* Callback function to be invoked by the reporter thread */
    void (*cb)(void *context, pd3_estimator_results *results);
} pd3_estimator_callbacks;

/* Opaque handle to the estimator service */
typedef struct pd3_estimator_handle_s pd3_estimator_handle;

/* Configuration options */
typedef struct pd3_estimator_options {
    /* Period, in seconds, at which the aggregator thread throws data
     * over the fence to the reporter thread */
    double aggregation_interval;

    /* A string describing the schedule by which the reporter thread
     * should invoke the user-provided callback function.
     *
     * The schedule is specified by a string of semicolon-separated
     * repeating reports. Each repeating report specification is
     * comma-separated and contains:
     *   - destination(s)
     *   - repeating interval (in seconds)
     *   - offset (in seconds)
     *
     * Example:c,5,0;c,5,2.5
     * - invokes the callback ('c') every 2.5 seconds, each report covering 5 seconds
     *
     * 'c' is the only valid destination at this time.
     */
    char *reporter_schedule;

    /* The reporter thread processes information from the aggregator
     * only when there are at least this many batches. */
    unsigned int reporter_min_batches;

    /* Should the library measure loss? */
    bool measure_loss;

    /* Should the library measure reorder extent? */
    bool measure_reorder_extent;

    /* Should the library measure reorder density? */
    bool measure_reorder_density;
} pd3_estimator_options;

/*************************************** API *****************************/

/* Initialize the library. Starts a singleton instance of the
 * aggregator and reporter threads if not already started. `options`
 * is a mandatory argument providing configuration values. `callbacks`
 * is optional. Returns 0 on success, -1 on error. */
int pd3_estimator_init(pd3_estimator_options *options, pd3_estimator_callbacks *cbs);

/* Get a fresh handle to the pd3_estimator service. Returns the handle
 * on success, NULL on error. */
pd3_estimator_handle *pd3_estimator_create_handle(void);

/* Clean up a handle. Returns 0 on success, -1 on error. */
int pd3_estimator_destroy_handle(pd3_estimator_handle *handle);

/* Join thread(s), clean up. Returns 0 on success, -1 on error. */
int pd3_estimator_destroy(void);

/* Push meta-data about a packet. Returns 0 on success, -1 on error. */
int pd3_estimator_push_packet_info(pd3_estimator_handle *handle,
                                   pd3_estimator_packet_info *pinfo);

/* Flush packets to the estimator. Returns 0 on success, -1 on error. */
int pd3_estimator_flush(pd3_estimator_handle *handle);

#endif /* _PD3_ESTIMATOR_H_ */
