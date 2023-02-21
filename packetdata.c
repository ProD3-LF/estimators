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

#include "packetdata.h"
#include "datatypes.h"

void packetdata_arrival(struct packetData *pd, TIMESTAMP ts, SEQNO seq)
{
    if (pd->packet_count == 0 || ts < pd->earliest) {
        pd->earliest = ts;
    }
    if (pd->packet_count == 0 || ts > pd->latest) {
        pd->latest = ts;
    }
    if (pd->packet_count == 0 || seqcmp(seq, pd->minSeq) < 0) {
        pd->minSeq = seq;
    }
    if (pd->packet_count == 0 || seqcmp(seq, pd->maxSeq) > 0) {
        pd->maxSeq = seq;
    }

    pd->packet_count++; /* do this last */
}

void packetdata_accumulate(struct packetData *accum, struct packetData *unit)
{
    /* Update min and max sequence numbers */
    if (accum->packet_count == 0) {
        accum->minSeq = unit->minSeq;
        accum->maxSeq = unit->maxSeq;
    }
    else {
        if (seqcmp(unit->minSeq, accum->minSeq) < 0) {
            accum->minSeq = unit->minSeq;
        }
        if (seqcmp(unit->maxSeq, accum->maxSeq) > 0) {
            accum->maxSeq = unit->maxSeq;
        }
    }

    accum->packet_count += unit->packet_count;
    accum->earliest = (accum->earliest == 0) ? unit->earliest : min(accum->earliest, unit->earliest);
    accum->latest = max(accum->latest, unit->latest);
}
