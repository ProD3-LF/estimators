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

#ifndef _PD3_ESTIMATOR_PACKETDATA_H_
#define _PD3_ESTIMATOR_PACKETDATA_H_

#include <string.h>
#include "datatypes.h"

struct packetData {
    PACKETCOUNT packet_count;
    TIMESTAMP earliest, latest;
    SEQNO minSeq, maxSeq;
};

/*
 *	packet arrival           packetdata_arrival()
 *	aggregator to reporter   packetdata_a2r()
 *	accumulate over time     packetdata_accumulate()
 *	accumulate over group    packetdata_accumulate()
 */

void packetdata_arrival(struct packetData *pd, TIMESTAMP ts, SEQNO seq);
void packetdata_accumulate(struct packetData *accum, struct packetData *unit);

#define packetdata_a2r(_to, _from) \
  ((void) memcpy((void *) (_to), (void *) (_from), sizeof (struct packetData)))

#endif /* _PD3_ESTIMATOR_PACKETDATA_H_ */
