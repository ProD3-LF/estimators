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

#include "flowstate.h"

enum flowState flowstate_delimit(enum flowState fs) {
  if (fs == FS_NULL || fs == FS_D) {
    return (FS_D);
  } else if (fs == FS_P || fs == FS_PD) {
    return (FS_PD);
  } else if (fs == FS_DP || fs == FS_DPD) {
    return (FS_DPD);
  } else {
    return (FS_ERROR);
  }
}

enum flowState flowstate_packet(enum flowState fs) {
  if (fs == FS_NULL || fs == FS_P) {
    return (FS_P);
  } else if (fs == FS_D || fs == FS_DP) {
    return (FS_DP);
  } else {
    return (FS_ERROR);
  }
}

enum flowState flowstate_concatenate(enum flowState fs1, enum flowState fs2) {
  if (fs2 == FS_NULL) {
    return (fs1);
  } else if (fs2 == FS_D) {
    return (flowstate_delimit(fs1));
  } else if (fs2 == FS_P) {
    return (flowstate_packet(fs1));
  } else if (fs2 == FS_DP) {
    return (flowstate_packet(flowstate_delimit(fs1)));
  } else if (fs2 == FS_PD) {
    return (flowstate_delimit(flowstate_packet(fs1)));
  } else if (fs2 == FS_DPD) {
    return (flowstate_delimit(flowstate_packet(flowstate_delimit(fs1))));
  } else {
    return (FS_ERROR);
  }
}

char *flowstate_tostring(enum flowState fs) {
  if (fs == FS_NULL) { return ("NULL"); }
  else if (fs == FS_D) { return ("D"); }
  else if (fs == FS_P) { return ("P"); }
  else if (fs == FS_DP) { return ("DP"); }
  else if (fs == FS_PD) { return ("PD"); }
  else if (fs == FS_DPD) { return ("DPD"); }
  else if (fs == FS_ERROR) { return ("ERROR"); }
  else { return ("?"); }
}
