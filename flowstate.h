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

#ifndef _PD3_ESTIMATOR_FLOWSTATE_H_
#define _PD3_ESTIMATOR_FLOWSTATE_H_

enum flowState {FS_NULL = 0, FS_D, FS_P, FS_DP, FS_PD, FS_DPD, FS_ERROR};

enum flowState flowstate_delimit(enum flowState fs);
enum flowState flowstate_packet(enum flowState fs);
enum flowState flowstate_concatenate(enum flowState fs1, enum flowState fs2);
char *flowstate_tostring(enum flowState fs);

#define flowstate_beginp(_fs) ((_fs) == FS_P || (_fs) == FS_PD)
#define flowstate_endp(_fs)   ((_fs) == FS_P || (_fs) == FS_DP)
#define flowstate_error(_fs)  ((_fs) == FS_ERROR)

#endif /* _PD3_ESTIMATOR_FLOWSTATE_H_ */
