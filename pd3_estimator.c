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

#include <math.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include "pd3_estimator.h"
#include "fistq.h"
#include "datatypes.h"
#include "hashmap2.h"
#include "reportschedule.h"

/* Private definition of handle data structure */
struct pd3_estimator_handle_s {
    fistq_handle *handle;
};

/* fistq names */
static char *FISTQ_SRC = "pd3_estimator_client";
static char *FISTQ_DST = "pd3_estimator_aggregator";

/* Library lifecycle management */
static int pd3_estimator_started = 0;
static int pd3_estimator_done = 0;

/* Configuration */
static struct timespec aggregator_interval;
static bool loss_enabled = true;
static bool reorder_extent_enabled = true;
static bool reorder_density_enabled = true;
static pd3_estimator_callbacks callbacks;

/* Thread synchronization */
static pthread_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t shared_mutex;
static pthread_cond_t shared_cond;

/* Aggregator objects */
static pthread_t aggregator_tid;
static struct hashMapList working_a;
static struct seqnoRangeList free_lossranges_a;
static struct seqnoRangeList free_reorderranges_a;
static struct hashMapList free_hashmaps_a;
static struct hashMapItemList free_hashmapitems_a;

/* Reporter objects */
static pthread_t reporter_tid;
static unsigned int periods_to_wait;
static int reporter_sleeping;
static struct hashMapList working_r;
static struct hashMapList free_hashmaps_r;
static struct hashMapItemList free_hashmapitems_r;
static struct seqnoRangeList free_lossranges_r;
static struct seqnoRangeList free_reorderranges_r;
static struct hashMapItemList free_hmis_local; /* storage remains in reporter */

/* Shared objects */
static char schedule[128];
static struct hashMapList working_sh;
static struct hashMapList free_hashmaps_sh;
static struct hashMapItemList free_hashmapitems_sh;
static struct seqnoRangeList free_lossranges_sh;
static struct seqnoRangeList free_reorderranges_sh;

/* Local declarations */
static void *aggregator_thread(void *arg);
static void *reporter_thread(void *arg);

int pd3_estimator_init(pd3_estimator_options *options, pd3_estimator_callbacks *cbs)
{
    double agg_int;

    if (!options) {
        fprintf(stderr, "Invalid options: null\n");
        return -1;
    }

    if (options->aggregation_interval < 0) {
        fprintf(stderr, "Invalid options: aggregation interval must be non-negative\n");
        return -1;
    }

    /* Make sure we initialize at most once */
    pthread_mutex_lock(&init_mutex);
    if (pd3_estimator_started) {
        pthread_mutex_unlock(&init_mutex);
        return 0;
    }

    /* Store the user-provided callbacks */
    memset(&callbacks, 0, sizeof(callbacks));
    if (cbs) {
        callbacks = *cbs;
    }

    /* Initialize fistq service */
    fistq_init();

    /* Shared variables */
    pthread_mutex_init(&shared_mutex, NULL);
    pthread_cond_init(&shared_cond, NULL);

    /* Aggregator variables */
    agg_int = options->aggregation_interval;
    aggregator_interval.tv_sec = (time_t) floor(agg_int);
    aggregator_interval.tv_nsec = (long) ((agg_int - floor(agg_int)) * 1e9);
    memset(&free_lossranges_a, 0, sizeof(free_lossranges_a));
    memset(&free_reorderranges_a, 0, sizeof(free_reorderranges_a));
    memset(&free_hashmaps_a, 0, sizeof(free_hashmaps_a));
    memset(&free_hashmapitems_a, 0, sizeof(free_hashmapitems_a));

    /* Reporter variables */
    periods_to_wait = options->reporter_min_batches;
    reporter_sleeping = 0;

    memset(schedule, 0, sizeof(schedule));
    strncpy(schedule, options->reporter_schedule, sizeof(schedule) - 1);
    if (set_schedule(schedule) == -1) {
        fprintf(stderr, "could not set schedule\n");
        return -1;
    }

    /* Initialize each estimator */
    loss_enabled = options->measure_loss;
    reorder_extent_enabled = options->measure_reorder_extent;
    reorder_density_enabled = options->measure_reorder_density;
    if (loss_enabled) {
        fprintf(stdout, "Initializing loss estimator...\n");
        lossdata_init();
    }
    if (reorder_extent_enabled || reorder_density_enabled) {
        fprintf(stdout, "Initializing reorder estimator...\n");
        reorderdata_init(reorder_extent_enabled, reorder_density_enabled);
    }

    /* Create the aggregator thread */
    if (pthread_create(&aggregator_tid, NULL, aggregator_thread, NULL) != 0) {
        perror("pthread");
        pthread_mutex_unlock(&init_mutex);
        return -1;
    }

    /* Create the reporter thread */
    if (pthread_create(&reporter_tid, NULL, reporter_thread, NULL) != 0) {
        perror("pthread");
        pthread_mutex_unlock(&init_mutex);
        return -1;
    }

    pd3_estimator_started = 1;
    pthread_mutex_unlock(&init_mutex);

    return 0;
}

pd3_estimator_handle *pd3_estimator_create_handle()
{
    pd3_estimator_handle *h;

    h = malloc(sizeof(*h));
    if (!h) {
        fprintf(stderr, "malloc failed\n");
        return NULL;
    }

    h->handle = fistq_getHandle(FISTQ_SRC, FISTQ_DST, FISTQ_FREE, NULL);
    if (!h->handle) {
        fprintf(stderr, "Could not create handle\n");
        return NULL;
    }

    return h;
}

int pd3_estimator_push_packet_info(pd3_estimator_handle *handle,
                                   pd3_estimator_packet_info *pinfo)
{
    pd3_estimator_packet_info *p;

    if (!handle) {
        fprintf(stderr, "NULL handle\n");
        return -1;
    }

    /* FIXME: Consider using a pool of pinfos to avoid malloc */
    p = malloc(sizeof(*p));
    if (!p) {
        fprintf(stderr, "malloc failed\n");
        return -1;
    }
    memcpy(p, pinfo, sizeof(*p));

    return fistq_enqueue_any(handle->handle, p, FISTQ_TYPE_PINFO, FISTQ_NOFLUSH);
}

int pd3_estimator_flush(pd3_estimator_handle *handle)
{
    return fistq_flush(handle->handle);
}

int pd3_estimator_destroy_handle(pd3_estimator_handle *handle)
{
    if (!handle) {
        return -1;
    }

    fistq_destroyHandle(handle->handle);
    free(handle);

    return 0;
}

int pd3_estimator_destroy()
{
    pthread_mutex_lock(&init_mutex);
    if (!pd3_estimator_started) {
        pthread_mutex_unlock(&init_mutex);
        return 0;
    }

    pthread_mutex_unlock(&init_mutex);

    /* Tell the threads we're done */
    pd3_estimator_done = 1;

    if (pthread_join(aggregator_tid, NULL) != 0) {
        perror("pthread_join");
        return -1;
    }

    pthread_mutex_lock(&shared_mutex);
    pthread_cond_signal(&shared_cond);
    pthread_mutex_unlock(&shared_mutex);

    if (pthread_join(reporter_tid, NULL) != 0) {
        perror("pthread_join");
        return -1;
    }

    /* Clean up sequence number ranges */
    free_seqnorangelist(&free_lossranges_a);
    free_seqnorangelist(&free_lossranges_r);
    free_seqnorangelist(&free_lossranges_sh);
    free_seqnorangelist(&free_reorderranges_a);
    free_seqnorangelist(&free_reorderranges_r);
    free_seqnorangelist(&free_reorderranges_sh);

    memset(&free_lossranges_a, 0, sizeof(free_lossranges_a));
    memset(&free_lossranges_r, 0, sizeof(free_lossranges_r));
    memset(&free_lossranges_sh, 0, sizeof(free_lossranges_sh));
    memset(&free_reorderranges_a, 0, sizeof(free_reorderranges_a));
    memset(&free_reorderranges_r, 0, sizeof(free_reorderranges_r));
    memset(&free_reorderranges_sh, 0, sizeof(free_reorderranges_sh));

    /* Clean up the free lists */
    hashmap_item_list_destroy(&free_hmis_local);
    hashmap_item_list_destroy(&free_hashmapitems_a);
    hashmap_item_list_destroy(&free_hashmapitems_r);
    hashmap_item_list_destroy(&free_hashmapitems_sh);
    memset(&free_hmis_local, 0, sizeof(free_hmis_local));
    memset(&free_hashmapitems_a, 0, sizeof(free_hashmapitems_a));
    memset(&free_hashmapitems_r, 0, sizeof(free_hashmapitems_r));
    memset(&free_hashmapitems_sh, 0, sizeof(free_hashmapitems_sh));

    hashmap_list_destroy(&free_hashmaps_a);
    hashmap_list_destroy(&free_hashmaps_r);
    hashmap_list_destroy(&free_hashmaps_sh);
    memset(&free_hashmaps_a, 0, sizeof(free_hashmaps_a));
    memset(&free_hashmaps_r, 0, sizeof(free_hashmaps_r));
    memset(&free_hashmaps_sh, 0, sizeof(free_hashmaps_sh));

    /* Clean up working storage */
    hashmap_list_destroy(&working_a);
    hashmap_list_destroy(&working_r);
    hashmap_list_destroy(&working_sh);
    memset(&working_a, 0, sizeof(working_a));
    memset(&working_r, 0, sizeof(working_r));
    memset(&working_sh, 0, sizeof(working_sh));

    pthread_mutex_destroy(&shared_mutex);
    pthread_cond_destroy(&shared_cond);

    destroy_schedule();
    lossdata_destroy_a2r_compute_array();

    /* Go back to our original state. The init_mutex remainds
     * statically initialized. */
    pd3_estimator_started = 0;
    pd3_estimator_done = 0;

    return 0;
}

// increment tv by the given time increment.
// one millisecond and one second in nanoseconds
#define ONE_MS (1000*1000)
#define ONE_SECOND (1000*ONE_MS)
static void setNextInterval(struct timespec *tv, struct timespec *incr)
{
    tv->tv_nsec += incr->tv_nsec;
    if (tv->tv_nsec >= ONE_SECOND)
    {
        tv->tv_nsec -= ONE_SECOND;
        tv->tv_sec += 1;
    }
}

static inline int timeCmp(struct timespec *lhs, struct timespec *rhs)
{
    if (lhs->tv_sec > rhs->tv_sec)
        return 1;
    if (lhs->tv_sec < rhs->tv_sec)
        return -1;
    if (lhs->tv_nsec > rhs->tv_nsec)
        return 1;
    if (lhs->tv_nsec < rhs->tv_nsec)
        return -1;

    return 0;
}

static void data_exchange_aggregator_unsafe()
{
    /* move earliest working hashmap into shared area */
    moveone_hashmap(&working_sh, &working_a);

    /* move freelists into aggregator */
    moveall_hashmap(&free_hashmaps_a, &free_hashmaps_sh);
    move_hmilist(&free_hashmapitems_a, &free_hashmapitems_sh);
    move_seqnorangelist(&free_lossranges_a, &free_lossranges_sh);
    move_seqnorangelist(&free_reorderranges_a, &free_reorderranges_sh);
}

/* Invoked by the aggregator thread */
static void period_transition()
{
    pthread_mutex_lock(&shared_mutex);

    data_exchange_aggregator_unsafe();
    if (reporter_sleeping) {
        pthread_cond_signal(&shared_cond);
    }
    pthread_mutex_unlock(&shared_mutex);

    add_hashmap(&working_a, &free_hashmaps_a);
}

static void handle_packet_arrival(void *data)
{
    pd3_estimator_packet_info *ppi;
    struct hashMapKey key;
    struct hashMapItem *hmi;
    struct aggregatorData *ad;
    struct packetData *pd;

    ppi = data;

    /* Look up the hash map item for this stream */
    set_streamtuple(&key, &ppi->stream);
    hmi = hashmap_force(working_a.latest, &key, &free_hashmapitems_a);
    ad = &hmi->value.agg_data;
    pd = &ad->received;

    /* Get timestamp of this packet arrival */
    struct timeval now;
    TIMESTAMP ts;
    gettimeofday(&now, NULL);
    ts = (now.tv_sec * 1e6) + now.tv_usec;

    /* Tell relevant parties about the new packet */
    packetdata_arrival(pd, ts, ppi->seq);

    if (loss_enabled) {
        lossdata_arrival(&ad->loss, ppi->seq, &free_lossranges_a);
    }
    if (reorder_extent_enabled || reorder_density_enabled) {
        reorderdata_arrival(&ad->reorder, ppi->seq, &free_reorderranges_a);
    }
}

static void *aggregator_thread(void *arg)
{
    fistq_handle *client2agg;
    struct timespec now, ref;
    clockid_t clock;

    (void)arg;

    /* Create fistq handle for receiving events from the client */
    client2agg = fistq_getHandle(FISTQ_SRC, FISTQ_DST, FISTQ_FREE, NULL);
    if (!client2agg) {
        fprintf(stderr, "Could not create handle\n");
        return NULL;
    }

    /* Allocate the initial hashmap */
    add_hashmap(&working_a, NULL);

    clock = fistq_getclock();
    clock_gettime(clock, &ref);
    setNextInterval(&ref, &aggregator_interval);

    while (!pd3_estimator_done) {
        fistq_data_type type;
        void *data;

        clock_gettime(clock, &now);

        /* Time to start the next interval */
        if (timeCmp(&now, &ref) > 0 || (data = fistq_timeddequeue_any(client2agg, &type, &ref)) == NULL) {
            period_transition();
            setNextInterval(&ref, &aggregator_interval);
            continue;
        }

        /* Something to process */
        if (type == FISTQ_TYPE_PINFO) {
            handle_packet_arrival(data);
        }

        /* Clean up */
        free(data);
    }

    fistq_destroyHandle(client2agg);

    return NULL;
}

static inline unsigned int pending_hashmaps()
{
    return working_sh.count;
}

static void data_exchange_reporter_unsafe()
{
    /* move all working hashmaps into reporter */
    moveall_hashmap(&working_r, &working_sh);

    /* move freelists into shared area */
    moveall_hashmap(&free_hashmaps_sh, &free_hashmaps_r);
    move_hmilist(&free_hashmapitems_sh, &free_hashmapitems_r);
    move_seqnorangelist(&free_lossranges_sh, &free_lossranges_r);
    move_seqnorangelist(&free_reorderranges_sh, &free_reorderranges_r);
}

static void accumulate_time(struct reporterData *accum, struct reporterData *unit)
{
    packetdata_accumulate(&accum->received, &unit->received);

    if (loss_enabled) {
        lossdata_accumulate_time(&accum->loss, &unit->loss);
    }
    if (reorder_extent_enabled || reorder_density_enabled) {
        reorderdata_accumulate_time(&accum->reorder, &unit->reorder);
    }
}

static void accumulate_flow(struct reporterData *accum, struct reporterData *unit)
{
    packetdata_accumulate(&accum->received, &unit->received);

    if (loss_enabled) {
        lossdata_accumulate_flows(&accum->loss, &unit->loss);
    }
    if (reorder_extent_enabled || reorder_density_enabled) {
        reorderdata_accumulate_flows(&accum->reorder, &unit->reorder);
    }
}


static pd3_estimator_results build_callback_results(struct hashMapItem *hmi_r, TIMEINTERVAL duration, struct hashMap *cumulative_data)
{
    pd3_estimator_results results;
    (void) cumulative_data;

    memset(&results, 0, sizeof(results));

    /* Set up the flow key */
    memcpy(results.flow_key, hmi_r->key.key.stream.flow_key, sizeof(results.flow_key));

    /* Set bounding timestamps for measurements */
    results.earliest = hmi_r->value.rep_data.received.earliest;
    results.latest = hmi_r->value.rep_data.received.latest;

    /* Set bounding sequence numbers for measurements */
    results.min_seq = hmi_r->value.rep_data.received.minSeq;
    results.max_seq = hmi_r->value.rep_data.received.maxSeq;

    /* Set the packet count */
    results.packet_count = hmi_r->value.rep_data.received.packet_count;

    /* Set the duration */
    results.duration = duration;

    /* Set loss results */
    if (loss_enabled) {
        struct lossDataR *ldr = &hmi_r->value.rep_data.loss;
        if (ldr->received > 0) {
            double r, d, c, ac, loss;
            r = (double) ldr->received;
            d = (double) ldr->dropped;
            loss = d / (r + d);
            c = ldr->consecutive_drops;
            ac = (d != 0.0 ? ((c*r) + (c*d) - (d*d)) / (d*r) : 0.0);
            results.loss_results.packets_received = r;
            results.loss_results.packets_dropped = d;
            results.loss_results.consecutive_drops = c;
            results.loss_results.autocorr = ac;
            results.loss_results.value = loss;
            results.loss = 1;
        }
    }

    /* Set the reorder extent results */
    if (reorder_extent_enabled) {
        struct reorderDataR *rdr = &hmi_r->value.rep_data.reorder;

        size_t num_bins = 0;
        for (int i = 0; i < REORDER_MAX_EXTENT; i++) {
            results.reorder_extent_results.bins[i] = rdr->extentToCount[i];
            if (rdr->extentToCount[i] > 0) {
                num_bins++;
            }
        }
        if (num_bins > 0) {
            results.reorder_extent_results.num_bins = REORDER_MAX_EXTENT;
        }
        results.reorder_extent_results.assumed_drops = rdr->extent_assumed_drops;
        /* We have something useful to say */
        if (num_bins > 0 || rdr->extent_assumed_drops > 0) {
            results.reorder_extent = 1;
        }
    }

    /* Set the reorder density results */
    if (reorder_density_enabled) {
        struct reorderDataR *rdr = &hmi_r->value.rep_data.reorder;

        /* Count the number of non-zero entries */
        size_t num_entries = 0;
        for (int i = 0; i < REORDER_WINDOW_SIZE; i++) {
            PACKETCOUNT frequency = rdr->FD[i];
            int distance = i - REORDER_DT;
            results.reorder_density_results.bins[i].frequency = frequency;
            results.reorder_density_results.bins[i].distance = distance;
            if (frequency > 0) {
                num_entries++;
            }
        }
        /* We have something useful to say */
        if (num_entries > 0) {
            results.reorder_density_results.num_bins = REORDER_WINDOW_SIZE;
        }
        // FIXME: not currently calculating assumed drops for RD
        // results.reorder_density_results.assumed_drops = rdr->rd_assumed_drops;
        if (num_entries > 0 || rdr->rd_assumed_drops > 0) {
            results.reorder_density = 1;
        }
    }

    return results;
}

static void *reporter_thread(void *arg)
{
    struct hashMapItem *hmi_a, *hmi_st, *hmi_r, *hmi_g;
    struct hashMap state_data;
    struct hashMapKey flowkey;
    struct reporterData rd;
    unsigned int ntrackers;
    char *outlets;
    struct hashMap *trackers = NULL;

    (void) arg;

    /* Set up the trackers */
    ntrackers = schedule_parallelism();
    if (ntrackers == 0) {
        fprintf(stderr, "reporter error: no trackers\n");
        return NULL;
    }
    trackers = calloc(ntrackers, sizeof(*trackers));
    if (!trackers) {
        fprintf(stderr, "calloc failed\n");
        return NULL;
    }

    memset(&state_data, 0, sizeof(state_data));

    while (!pd3_estimator_done) {
        /* Wait for a new hashmap */
        pthread_mutex_lock(&shared_mutex);
        reporter_sleeping = 1;
        while (!pending_hashmaps() && !pd3_estimator_done) {
            pthread_cond_wait(&shared_cond, &shared_mutex);
        }
        reporter_sleeping = 0;

        if (pd3_estimator_done) {
            pthread_mutex_unlock(&shared_mutex);
            break;
        }

        /* Grab the hashmaps */
        data_exchange_reporter_unsafe();

        pthread_mutex_unlock(&shared_mutex);

        /* process hashmaps */
        while (working_r.count >= periods_to_wait) {
            for (hmi_a = working_r.earliest->items.head; hmi_a; hmi_a = hmi_a->next) {
                /* convert aggregator data structures to reporter data structures */
                hmi_st = hashmap_force(&state_data, &hmi_a->key, &free_hmis_local);
                memset(&rd, 0, sizeof(rd));
                packetdata_a2r(&rd.received, &hmi_a->value.agg_data.received);
                if (loss_enabled) {
                    lossdata_a2r(&rd.loss, &hmi_a->value.agg_data.loss,
                                 &hmi_st->value.state_data.loss,
                                 working_r.earliest->next, &hmi_a->key,
                                 periods_to_wait);
                }
                if (reorder_extent_enabled || reorder_density_enabled) {
                    reorderdata_a2r(&rd.reorder, &hmi_a->value.agg_data.reorder,
                                    &hmi_st->value.state_data.reorder);
                }
                for (unsigned int i = 0; i < ntrackers; i++) {
                    hmi_r = hashmap_force(&trackers[i], &hmi_a->key, &free_hmis_local);
                    accumulate_time(&hmi_r->value.rep_data, &rd);
                }
            }

            /* Report! */
            for (unsigned int i = 0; i < ntrackers; i++) {
                outlets = schedule_outlets(i);
                if (outlets == NULL) {
                    continue;
                }
                /* Consolidate stream-level information into flow-level information */
                for (hmi_r = trackers[i].items.head; hmi_r; hmi_r = hmi_r->next) {
                    if (hmi_r->key.keytype == HMK_STREAMTUPLE) {
                        hashmap_force(&state_data, &hmi_r->key, &free_hmis_local);
                        /* Adding flowtuples to hashmap is safe, because:
                           (a) prepending before point of iteration, and
                           (b) iteration only operates on streams */
                        set_flowtuple(&flowkey, &hmi_r->key.key.stream);
                        hmi_g = hashmap_force(&trackers[i], &flowkey, &free_hmis_local);
                        accumulate_flow(&hmi_g->value.rep_data, &hmi_r->value.rep_data);
                    }
                }
                if (strchr(outlets, 'c')) {
                    if (callbacks.cb) {
                        /* now includes flowgroups */
                        for (hmi_r = trackers[i].items.head; hmi_r; hmi_r = hmi_r->next) {
                            /* Only process flows */
                            if (hmi_r->key.keytype != HMK_FLOWTUPLE) {
                                continue;
                            }
                            /* Skip over this flow if we didn't see any packets */
                            if (hmi_r->value.rep_data.received.packet_count == 0) {
                                continue;
                            }
                            pd3_estimator_results results = build_callback_results(hmi_r, get_duration(i), NULL);
                            callbacks.cb(callbacks.context, &results);
                        }
                    }
                }
                else {
                    fprintf(stderr, "Unsupported outlet: %s\n", outlets);
                }
                schedule_reset(i);
                /* zeroout_hashmap() should not and does not free ranges in reporter data objects */
                zeroout_hashmap(&trackers[i], &free_hmis_local);
            }
            /* recycle storage, eventually to aggregator */
            for (hmi_a = working_r.earliest->items.head; hmi_a; hmi_a = hmi_a->next) {
                move_seqnorangelist(&free_lossranges_r, &hmi_a->value.agg_data.loss.ranges);
                move_seqnorangelist(&free_reorderranges_r, &hmi_a->value.agg_data.reorder.ranges);
            }
            move_hmilist(&free_hashmapitems_r, &working_r.earliest->items);
            moveone_hashmap(&free_hashmaps_r, &working_r);
        }
    }

    /* Move reporter items back to a free list so they can be freed */
    for (unsigned int i = 0; i < ntrackers; i++) {
        zeroout_hashmap(&trackers[i], &free_hmis_local);
    }
    zeroout_hashmap(&state_data, &free_hmis_local);
    free(trackers);

    return NULL;
}
