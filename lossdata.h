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

#ifndef _PD3_ESTIMATOR_LOSSDATA_H_
#define _PD3_ESTIMATOR_LOSSDATA_H_

#include "datatypes.h"
#include "flowstate.h"

struct lossDataA {
  struct seqnoRangeList ranges;    /* linked with next pointer */
  enum flowState flowstate;
};

struct lossDataR {
  struct seqnoRangeList ranges;    /* linked with next_r pointer */
  enum flowState flowstate;
  unsigned int badflows;           /* for flowgroups */
  /* loss and autocorrelation coefficient */
  PACKETCOUNT received, dropped, consecutive_drops;
  /* lossburstsize */
  PACKETCOUNT gap_total, gap_count, gap_min, gap_max;
};

struct lossState {
    uint8_t has_high_seqno;
    SEQNO high_seqno;

    uint8_t has_last_range;
    struct seqnoRange last_range;
};

/*
 *	packet arrival           lossdata_arrival()
 *	flow event               lossdata_birthdeath()
 *	aggregator to reporter   lossdata_a2r()
 *	accumulate over time     lossdata_accumulate_time()
 *	accumulate over group    lossdata_accumulate_flows()
 *	print aggregator         lossdata_tostringA()
 *	print reporter           lossdata_tostringR()
 */

/* Returns 0 on success, -1 on error */
int lossdata_init(void);
int lossdata_arrival(struct lossDataA *lda, SEQNO seqno, struct seqnoRangeList *free_ranges);
void lossdata_birthdeath(struct lossDataA *ld);
void lossdata_a2r(struct lossDataR *ldr, struct lossDataA *lda,
                  struct lossState *lstate, void *future, void *key,
                  unsigned int periods_to_wait);
void lossdata_accumulate_time(struct lossDataR *accum, struct lossDataR *unit);
void lossdata_accumulate_flows(struct lossDataR *accum, struct lossDataR *unit);
char *lossdata_tostringA(char *s, struct lossDataA *lda);
char *lossdata_tostringR(char *s, struct lossDataR *ldr);
char *lossstate_tostring(char *s, struct lossState *ls);
char *lossdata_debugA(char *s, struct lossDataA *lda);
char *lossdata_debugR(char *s, struct lossDataR *ldr);
void lossdata_destroy_a2r_compute_array(void);

#endif /* _PD3_ESTIMATOR_LOSSDATA_H_ */
