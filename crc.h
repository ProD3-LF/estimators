/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2012-2023 Applied Communication Sciences
 * (now Peraton Labs Inc.)
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

/************************************************************************/
/*                                                                      */
/*                              crc                                     */
/*                                                                      */
/* Author:      Bruce Siegell (bsiegell@appcomsci.com)                  */
/* File:        crc.h                                                   */
/* Date:        Fri Sep 21 10:53:22 EDT 2012                            */
/*                                                                      */
/* Description:                                                         */
/*      Definitions and function prototypes for crc.                    */
/************************************************************************/

#ifndef _PD3_ESTIMATOR_CRC_H_
#define _PD3_ESTIMATOR_CRC_H_

/************************************************************************/
/*                                                                      */
/*      data structures.                                                */
/*                                                                      */
/************************************************************************/


/************************************************************************/
/*                                                                      */
/*      global variables.                                               */
/*                                                                      */
/************************************************************************/


/************************************************************************/
/*                                                                      */
/*      crc_generateTable - generate the CRC table and write it to the  */
/*              specified file.                                         */
/*                                                                      */
/************************************************************************/

void crc_generateTable(char *filename);


/************************************************************************/
/*                                                                      */
/*      crc_generate - generate the crc (CRC32C) for the given buffer.  */
/*                                                                      */
/************************************************************************/

unsigned long crc_generate(unsigned char *buffer, unsigned int length);

#endif /* _PD3_ESTIMATOR_CRC_H_ */
