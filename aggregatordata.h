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

#ifndef _PD3_ESTIMATOR_AGGREGATOR_DATA_H_
#define _PD3_ESTIMATOR_AGGREGATOR_DATA_H_

#include "packetdata.h"
#include "lossdata.h"
#include "reorderdata.h"

struct aggregatorData {
    struct packetData received;
    struct lossDataA loss;
    struct reorderDataA reorder;
};

#endif /* _PD3_ESTIMATOR_AGGREGATOR_DATA_H_ */
