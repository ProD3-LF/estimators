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

#ifndef _PD3_ESTIMATOR_HASHMAP_H_
#define _PD3_ESTIMATOR_HASHMAP_H_

#include "pd3_estimator.h"
#include "aggregatordata.h"
#include "reporterdata.h"

#define HASHTABLESIZE 1024

/* Hashmap Keys */
enum hashMapKeyType {
  HMK_UNKNOWN = 0, /* unknown at initialization */
  HMK_STREAMTUPLE,
  HMK_FLOWTUPLE
};

struct hashMapKey {
  enum hashMapKeyType keytype;
  union {
      stream_tuple stream;
  } key;
  uint8_t has_hash;
  unsigned long hash;
};

struct value_struct {
    struct aggregatorData agg_data;
    struct reporterData rep_data;
    struct stateData state_data;
};

/* Hash Map Item */
struct hashMapItem {
    struct hashMapKey key;

    // FIXME: revert to union to save space
    struct value_struct value;
    uint8_t marked_for_deletion;
    struct hashMapItem *hashnext;
    struct hashMapItem *next;
    struct hashMapItem *partitionnext;
};

struct hashMapItemList {
  struct hashMapItem *head, *tail;
};

/* Hash Map */
struct hashMap {
  struct hashMapItem *hash_table[HASHTABLESIZE];
  struct hashMapItemList items;
  struct hashMap *previous, *next;
};

struct hashMapList {
  unsigned int count;
  struct hashMap *earliest, *latest;
};

/* Hash Map Partitions */
struct hashMapPartition {
  struct hashMapItem *intersection;
  struct hashMapItem *difference;
};

struct hashMapItem *hashmap_retrieve(struct hashMap *hm, struct hashMapKey *k);
void add_hashmap(struct hashMapList *list, struct hashMapList *freelist);

unsigned long hash_key(struct hashMapKey *hmk);
char *hashMapKey2String(char *, struct hashMapKey*);

#define keycpy(_to, _from)    memcpy((void *) (_to), (void *) (_from), \
                                     sizeof (struct hashMapKey))
#define equal_key(_k1, _k2)   (memcmp((void *) (_k1), (void *) (_k2), \
                                      sizeof (struct hashMapKey)) == 0)

struct hashMapItem *add_hashmapitem(struct hashMapItemList *list,
                                    struct hashMapItemList *freelist);
void move_hmilist(struct hashMapItemList *to, struct hashMapItemList *from);
void purge_hmilist(struct hashMapItemList *list,
                   struct hashMapItemList *freelist);

void add_hashmap(struct hashMapList *list, struct hashMapList *freelist);
void moveone_hashmap(struct hashMapList *to, struct hashMapList *from);
void moveall_hashmap(struct hashMapList *to, struct hashMapList *from);
struct hashMapItem *hashmap_force(struct hashMap *hm, struct hashMapKey *k,
                                  struct hashMapItemList *freelist);

void purge_hashmap(struct hashMap *hm, struct hashMapItemList *freelist);
void zeroout_hashmap(struct hashMap *hm, struct hashMapItemList *freelist);
char *hashMap2String(char *, struct hashMap *);

void set_streamtuple(struct hashMapKey *hmk, stream_tuple *stream);
void set_flowtuple(struct hashMapKey *hmk, stream_tuple *stream);

void partition_hashmap(struct hashMapPartition *hmp,
                       struct hashMap *splitme, struct hashMap *reference);
void partition_cleanup(struct hashMapPartition *hmp);

void hashmap_item_list_destroy(struct hashMapItemList *list);
void hashmap_list_destroy(struct hashMapList *list);

#endif /* _PD3_ESTIMATOR_HASHMAP_H_ */
