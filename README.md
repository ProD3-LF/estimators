
[comment]: # (SPDX-License-Identifier: Apache-2.0)

## Overview

The **ProD3** project utilizes in-line programmable network elements
and virtualized network functions to enable real-time distributed
defense against cyber attacks that threaten service availability in 5G
and beyond. The project (which is part of the DARPA Open,
Programmable, Secure 5G program) is investigating two primary
defensive capabilities: (1) programmable DDoS mitigation that monitors
the data plane to identify malicious flows in real time, and then
deploys distributed filters that "push back" against this traffic to
defeat it before it reaches its targets; and (2) real-time response to
network compromise that pinpoints network elements responsible for
disrupting communication, and that triggers responses to restore
service capabilities.

This repository contains the **ProD3 Estimation Service**, a library
for computing real-time metrics for packet flows. The metrics
currently supported are packet loss, reorder extent (see [RFC
4737](https://datatracker.ietf.org/doc/rfc4737/)) and reorder density
(see [RFC 5236](https://datatracker.ietf.org/doc/rfc5236/)). The
estimation service reports results to the application by invoking a
user-provided callback at configurable intervals.

The estimation service tracks metrics separately for each logical
*flow*, each comprising one or more *streams*. When reporting metrics
for a given flow, the service aggregates the results from all of that
flow's streams to produce a single, per-flow value (or set of values)
for each metric of interest.

Why the distinction between flows and streams? In ProD3, we use the
[Vector Packet Processor (VPP)](https://s3-docs.fd.io/vpp/23.02/#)
framework for high-performance, user-space packet processing. A
transmitting ProD3 node uses multiple VPP worker threads to send
packets, and each worker thread maintains its own sequence number
space (thus avoiding the need to protect a common sequence number
variable via mutex, which would be expensive at high speeds). This
architecture means that in ProD3, multiple worker threads may transmit
packets for the same logical flow. Distinguishing the streams in a
flow gives the receive-side estimation service enough information to
both track metrics separately for each stream and roll up the
stream-level results to produce a per-flow value.

## Pushing Packet Meta-Data to the Service
An application creates a *handle* to the estimation service by calling
`pd3_estimator_create_handle()`. When a packet arrives, the
application invokes the `pd3_estimator_push_packet_info()` function to
push packet meta-data to the service, via the handle. Packet
meta-data is encapsulated within a `pd3_estimator_packet_info`
structure, which contains the following information about the arriving
packet:
* `flow_key`: An application-provided key that the service uses to distinguish one logical flow from another
* `stream_id`: Identifier of a packet stream within the given flow. The tuple `(flow_key, stream_id)` uniquely identifies a packet stream. Applications that need not distinguish flows from streams can simply set `stream_id` to a constant value (e.g., 0).
* `seq`: The sequence number of the packet within the given stream

To improve efficiency, pushing packet meta-data to the service occurs
in lock-free fashion. Pushed meta-data is not immediately available to
the service for processing.  At convenient intervals, the application
calls `pd3_estimator_flush()` to *flush* accumulated packet meta-data
to the service's Aggregator Thread (see below) for
processing. Flushing is made thread-safe via a lock on a queue shared
between the handle and the Aggregator Thread. In applications (such as
ours in ProD3) where multiple threads receive incoming packets from
distinct streams of the same flow, each thread should create its own
handle to the service. In ProD3, we generally flush after processing a
vector of packets.

The library spins up two threads on the application's behalf:
* The `Aggregator Thread` processes packet meta-data that has been
  flushed, and periodically throws aggregated meta-data over the fence
  to the Reporter Thread
* The `Reporter Thread` periodically processes aggregated meta-data,
  computes per-stream metric values, and then rolls up the per-stream
  metrics into flow-level metrics. It then invokes the
  application-provided callback function with the results. Thus, the
  callback runs in the context of the Reporter Thread.

## Processing Reported Results

Please refer to the `pd3_estimator_results` structure in
`pd3_estimator.h` for a description of the results structure passed as
an argument to the application-provided callback function. The
`test_loss.c` and `test_reorder.c` files demonstrate how to extract
the relevant values.

## Building

To build the library, simply type `make`.

### Build-time Options

You can pass the following build-time options to `make` to change various size values:
* `PD3_ESTIMATOR_KEY_SIZE`: Size (in bytes) of the key used to distinguish one logical flow from another. Defaults to `2`.
* `REORDER_MAX_EXTENT`: Maximum extent value tracked by the Reorder Extent metric. Defaults to `255`.
* `REORDER_DT`: Displacement threshold for the Reorder Density metric -- that is, the maximum size of the buffer. Distance values go from `-REORDER_DT` TO `+REORDER_DT`. Defaults to `8`.

## Configuring the Service

To initialize the estimation service, call `pd3_estimator_init()` once
within your application, before processing incoming packets. This
function takes a pointer to a `pd3_estimator_options` structure as an
argument. The options structure allows the application to configure the
following values:
* `aggregation_interval`: Period, in seconds, at which the Aggregator
  Thread throws data over the fence to the Reporter Thread.
* `reporter_schedule`: A string describing the schedule by which the
   Reporter Thread should invoke the application-provided callback
   function. The schedule is specified by a string of
   semicolon-separated repeating reports. Each repeating report
   specification is comma-separated and contains (1) one or more
   destination(s); (2) a repeating interval (in seconds); and (3) an
   offset (in seconds). For example, the schedule `c,5,0;c,5,2.5`
   causes the service to invoke the callback (`c`) every 2.5 seconds,
   each report covering 5 seconds. Note that `c` is the only valid
   destination at this time.
* `reporter_min_batches`: The Reporter Thread processes aggregated
  meta-data from the Aggregator Thread only when at least this many
  batches are present.
* `measure_loss`: Should the library measure packet loss?
* `measure_reorder_extent`: Should the library measure Reorder Extent?
* `measure_reorder_density`: Should the library measure Reorder Density?

## Running the Test Programs

We provide a couple of example programs that demonstrate how to configure
the library and use the API. To build the test programs, simply type `make test`.

To run the test programs, type following. Note that you may need to
set your `LD_LIBRARY_PATH` so that the OS can find the generated
`libpd3_estimator.so` library.

```
# Run the loss test program
./test_loss

# Run the reorder test program
./test_reorder
```

***

Copyright (c) 2023 Peraton Labs Inc.

DoD Distribution Statement A: Approved for Public Release, Distribution Unlimited.

DISTAR Case 37651, cleared February 13, 2023.

This software was developed in work supported by U.S. Government contracts HR0011-15-C-0098 and HR0011-20-C-0160.

Any opinions, findings and conclusions or recommendations expressed in
this material are those of the author(s) and do not necessarily
reflect the views, either expressed or implied, of the
U.S. Government.

All files are released under the Apache 2.0 license unless specifically noted otherwise.
