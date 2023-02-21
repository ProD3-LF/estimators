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
    fprintf(stdout, "loss results: %u\n", results->loss);
    if (results->loss) {
        fprintf(stdout, "\treceived: %f\n", results->loss_results.packets_received);
        fprintf(stdout, "\tdropped:  %f\n", results->loss_results.packets_dropped);
        fprintf(stdout, "\tvalue:    %f\n", results->loss_results.value);
        fprintf(stdout, "\tconsecutive drops: %f\n", results->loss_results.consecutive_drops);
        fprintf(stdout, "\tautocorr: %f\n", results->loss_results.autocorr);
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

    /* Push some data: flow = (42, 43), stream = 44 */
    fprintf(stdout, "TEST flow=(42,43), stream=44: seq 1 - 100, dropping all odd-numbered packets\n");
    memset(&ppi, 0, sizeof(ppi));
    ppi.stream.flow_key[0] = 42;
    ppi.stream.flow_key[1] = 43;
    ppi.stream.stream_id = 44;
    for (int i = 0; i < 100; i++) {
        ppi.seq = 1 + i;
        if (ppi.seq % 2 == 0) {
            pd3_estimator_push_packet_info(handle, &ppi);
        }
    }
    /* Flush packet meta-data to the service for processing */
    fprintf(stdout, "flushing...\n");
    pd3_estimator_flush(handle);
    sleep(10);

    /* Push some data: flow = (42, 43), stream = 44 */
    fprintf(stdout, "TEST flow=(42,43), stream=44: seq 101 - 2000, no drops\n");
    memset(&ppi, 0, sizeof(ppi));
    ppi.stream.flow_key[0] = 42;
    ppi.stream.flow_key[1] = 43;
    ppi.stream.stream_id = 44;
    for (int i = 101; i <= 2000; i++) {
        ppi.seq = i;
        pd3_estimator_push_packet_info(handle, &ppi);
    }
    /* Flush packet meta-data to the service for processing  */
    fprintf(stdout, "flushing...\n");
    pd3_estimator_flush(handle);
    sleep(10);

    /* Clean up the handle */
    fprintf(stdout, "destroying...\n");
    pd3_estimator_destroy_handle(handle);

    /* Clean up the service itself */
    pd3_estimator_destroy();
    fprintf(stdout, "done\n");
    fflush(stdout);

    fprintf(stdout, "re-initializing and destroying...\n");
    ret = pd3_estimator_init(&options, &callbacks);
    handle = pd3_estimator_create_handle();

    fprintf(stdout, "running stress-test with many flows...\n");
    SEQNO flow_seqs[256][8];
    for (int i = 0 ; i < 256; i++) {
        for (int j = 0; j < 8; j++) {
            flow_seqs[i][j] = 1;
        }
    }
    for (unsigned int i = 0; i < ((unsigned int)1 << 24); i++) {
        memset(&ppi, 0, sizeof(ppi));
        ppi.stream.flow_key[0] = rand() % 256;
        ppi.stream.flow_key[1] = 0;
        ppi.stream.stream_id = rand() % 8;
        ppi.seq = flow_seqs[ppi.stream.flow_key[0]][ppi.stream.stream_id]++;
        if (i % 42 != 0 && i % 43 != 0) {
            pd3_estimator_push_packet_info(handle, &ppi);
        }
        if (ppi.stream.flow_key[0] % 100 == 0) {
            pd3_estimator_flush(handle);
        }
        if (i % 50 == 0) {
            pd3_estimator_flush(handle);
        }
        if (i % 500000 == 0) {
            fprintf(stdout, "**finished %u***\n", i);
            fflush(stdout);
        }
    }
    pd3_estimator_flush(handle);
    sleep(10);

    pd3_estimator_destroy_handle(handle);
    pd3_estimator_destroy();

    fprintf(stdout, "Done!\n");

    return 0;
}
