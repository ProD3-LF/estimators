/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2022-2023 Peraton Labs Inc.
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
#include <unistd.h>
#include "pd3_estimator.h"

/* Application-specific context created by the application and passed
 * by the estimation service as an argument to the callback
 * function. Example use case: The application wishes to send reported
 * metrics to statsd. You could create a handle to the statsd service
 * and add it to the context structure so that it is accessible within
 * the callback. */
typedef struct publish_context {
    /* Add application-specific fields here */
} publish_context;

/* Sample callback function that demonstrates how to process reported
 * results and simply dumps them to the screen. A more exotic callback
 * function might publish the results somewhere else (e.g., to
 * statsd). */
void my_callback(void *context, pd3_estimator_results *results)
{
    publish_context *con = (publish_context *)context;

    fprintf(stdout, "context=%p, results=%p\n", con, results);
    fprintf(stdout, "flow_key = (%u, %u)\n", results->flow_key[0], results->flow_key[1]);
    fprintf(stdout, "earliest = %lu\n", results->earliest);
    fprintf(stdout, "latest = %lu\n", results->latest);
    fprintf(stdout, "min_seq = %u, max_seq = %u\n", results->min_seq, results->max_seq);
    fprintf(stdout, "packet_count = %u\n", results->packet_count);
    fprintf(stdout, "duration = %lu\n", results->duration);
    fprintf(stdout, "reorder extent results: %u\n", results->reorder_extent);
    if (results->reorder_extent) {
        for (uint32_t i = 0; i < results->reorder_extent_results.num_bins; i++) {
            PACKETCOUNT frequency = results->reorder_extent_results.bins[i];
            if (frequency > 0) {
                fprintf(stdout, "\tExtent %u: %u\n", i, frequency);
            }
        }
        fprintf(stdout, "\tAssumed drops: %u\n",
                results->reorder_extent_results.assumed_drops);
    }
    fprintf(stdout, "reorder_density_results: %u\n", results->reorder_density);
    if (results->reorder_density) {
        for (uint32_t i = 0; i < results->reorder_density_results.num_bins; i++) {
            PACKETCOUNT frequency = results->reorder_density_results.bins[i].frequency;
            int distance = results->reorder_density_results.bins[i].distance;
            if (frequency > 0) {
                fprintf(stdout, "\tDistance %d: %u\n", distance, frequency);
            }
        }
    }
}

int main()
{
    int ret;
    pd3_estimator_packet_info ppi;

    /* Initialize estimation service: Set options and define
     * callback. */
    publish_context context;
    memset(&context, 0, sizeof(context));
    /* Optional: Set up application-specific context here */

    pd3_estimator_options options;
    memset(&options, 0, sizeof(options));
    options.aggregation_interval = 0.5;
    options.reporter_schedule = "c,5,0";
    options.reporter_min_batches = 5;
    options.measure_loss = true;
    options.measure_reorder_extent = true;
    options.measure_reorder_density = true;

    pd3_estimator_callbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.context = &context;
    callbacks.cb = my_callback;
    ret = pd3_estimator_init(&options, &callbacks);

    if (ret != 0) {
        fprintf(stderr, "Could not initialize pd3 estimator library\n");
        return -1;
    }

    /* Create a handle to the service, used to push packet metadata */
    pd3_estimator_handle *handle = pd3_estimator_create_handle();
    if (!handle) {
        fprintf(stderr, "Could not create handle to estimation service\n");
        return -1;
    }

    {
        int values[] = {0, 1, 2, 4, 5, 7, 6, 5, 3, 9, 8, 10};
        // inorder: 0, 1, 2, 4, 5, 7, 9, 10 ==> 8
        // extent 1: 6, 8                ==> 2
        // extent 5: 3                   ==> 1
        fprintf(stdout, "TEST flow=(1, 1), stream=44: {0, 1, 2, 4, 5, 7, 6, 5, 3, 9, 8, 10}\n");
        memset(&ppi, 0, sizeof(ppi));
        ppi.stream.flow_key[0] = 1;
        ppi.stream.flow_key[1] = 1;
        ppi.stream.stream_id = 44;
        for (size_t i = 0; i < (sizeof(values) / sizeof(int)); i++) {
            ppi.seq = values[i];
            pd3_estimator_push_packet_info(handle, &ppi);
        }
        fprintf(stdout, "flushing...\n");
        pd3_estimator_flush(handle);
        sleep(10);
    }

    {
        int values[] = {7, 8, 8, 8, 10, 12, 14, 11, 9, 30};
        fprintf(stdout, "TEST flow=(1,1), stream=44: {7, 8, 8, 8, 10, 12, 14, 11, 9, 30}\n");
        memset(&ppi, 0, sizeof(ppi));
        ppi.stream.flow_key[0] = 1;
        ppi.stream.flow_key[1] = 1;
        ppi.stream.stream_id = 44;
        for (size_t i = 0; i < (sizeof(values) / sizeof(int)); i++) {
            ppi.seq = values[i];
            pd3_estimator_push_packet_info(handle, &ppi);
        }
        fprintf(stdout, "flushing...\n");
        pd3_estimator_flush(handle);
        sleep(10);
    }

    {
        int values[] = {29, 31, 33, 35, 37, 39};
        fprintf(stdout, "TEST flow=(1,1), stream=44: {29, 31, 33, 35, 37, 39}\n");
        memset(&ppi, 0, sizeof(ppi));
        ppi.stream.flow_key[0] = 1;
        ppi.stream.flow_key[1] = 1;
        ppi.stream.stream_id = 44;
        for (size_t i = 0; i < (sizeof(values) / sizeof(int)); i++) {
            ppi.seq = values[i];
            pd3_estimator_push_packet_info(handle, &ppi);
        }
        fprintf(stdout, "flushing...\n");
        pd3_estimator_flush(handle);
        sleep(10);
    }

    /* Clean up the handle */
    fprintf(stdout, "destroying...\n");
    pd3_estimator_destroy_handle(handle);

    /* Clean up the service itself */
    pd3_estimator_destroy();
    fprintf(stdout, "done\n");
    fflush(stdout);

    return 0;
}
