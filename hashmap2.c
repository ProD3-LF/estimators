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

#include <stdlib.h>
#include "crc.h"
#include "hashmap2.h"
#include "reorderdata.h"

/* hashmap keys */

void set_streamtuple(struct hashMapKey *hmk, stream_tuple *stream)
{
    memset(hmk, 0, sizeof(*hmk));
    hmk->keytype = HMK_STREAMTUPLE;
    hmk->key.stream = *stream;
}

void set_flowtuple(struct hashMapKey *hmk, stream_tuple *stream)
{
    memset(hmk, 0, sizeof(*hmk));
    hmk->keytype = HMK_FLOWTUPLE;
    hmk->key.stream = *stream;
    hmk->key.stream.stream_id = 0;  // Map all stream IDs to 0
}

unsigned long hash_key(struct hashMapKey *hmk)
{
    if (!hmk->has_hash) {
        hmk->has_hash = 1;
        hmk->hash = crc_generate((unsigned char *) hmk, sizeof(*hmk));
    }
    return (hmk->hash);
}

/* hashmap items */

struct hashMapItem *add_hashmapitem(struct hashMapItemList *list,
                                    struct hashMapItemList *freelist)
{
    struct hashMapItem *hmi;

    if (freelist && freelist->head) {
        hmi = freelist->head;
        freelist->head = hmi->next;
        if (hmi == freelist->tail) {
            freelist->tail = NULL;
        }
    } else {
        hmi = malloc(sizeof(*hmi));
    }
    memset(hmi, 0, sizeof(*hmi));
    hmi->next = list->head;
    list->head = hmi;
    if (!list->tail) {
        list->tail = hmi;
    }
    return (hmi);
}

void move_hmilist(struct hashMapItemList *to, struct hashMapItemList *from)
{
    if (!from->head) {
        return;  /* nothing to move */
    } else if (!to->head) {  /* replace */
        to->head = from->head;
        to->tail = from->tail;
    } else {  /* append */
        to->tail->next = from->head;
        to->tail = from->tail;
    }
    from->head = NULL;
    from->tail = NULL;
}

void purge_hmilist(struct hashMapItemList *list,
                   struct hashMapItemList *freelist)
{
    struct hashMapItem **p, *d;

    for (p = &list->head, list->tail = NULL; *p;) {
        if ((*p)->marked_for_deletion) {
            d = *p;
            *p = d->next;
            d->next = freelist->head;
            freelist->head = d;
            if (!freelist->tail) {
                freelist->tail = d;
            }
        } else {
            list->tail = *p;
            p = &((*p)->next);
        }
    }
}

/* hashmaps */

static struct hashMap *pop_earliest(struct hashMapList *from);
static void push_latest(struct hashMapList *to, struct hashMap *hm);

void add_hashmap(struct hashMapList *list, struct hashMapList *freelist)
{
    struct hashMap *hm;

    hm = pop_earliest(freelist);
    if (!hm) {
        hm = malloc(sizeof(*hm));
    }
    memset(hm, 0, sizeof(*hm));
    push_latest(list, hm);
}

void moveone_hashmap(struct hashMapList *to, struct hashMapList *from)
{
    struct hashMap *hm;

    hm = pop_earliest(from);
    if (hm) {
        push_latest(to, hm);
    }
}

void moveall_hashmap(struct hashMapList *to, struct hashMapList *from)
{
    if (!from->earliest) {
        return;  /* nothing to move */
    } else if (!to->earliest) {  /* replace */
        to->earliest = from->earliest;
        to->latest = from->latest;
    } else {  /* from's hashMaps are more recent than to's hashMaps */
        to->latest->next = from->earliest;
        from->earliest->previous = to->latest;
        to->latest = from->latest;
    }
    to->count += from->count;
    from->count = 0;
    from->earliest = NULL;
    from->latest = NULL;
}

static struct hashMap *pop_earliest(struct hashMapList *from)
{
    struct hashMap *hm;

    if (!from || !from->earliest) {
        return NULL;
    }
    hm = from->earliest;
    from->count--;
    from->earliest = hm->next;
    if (hm->next) {
        hm->next->previous = NULL;
        hm->next = NULL;
    }
    if (hm == from->latest) {
        from->latest = NULL;
    }
    return (hm);
}

static void push_latest(struct hashMapList *to, struct hashMap *hm)
{
    if (!to->earliest) {
        to->earliest = hm;
    }
    if (to->latest) {
        to->latest->next = hm;
    }
    to->count++;
    hm->previous = to->latest;
    to->latest = hm;
}

struct hashMapItem *hashmap_force(struct hashMap *hm, struct hashMapKey *k,
                                  struct hashMapItemList *freelist)
{
    unsigned long hash;
    struct hashMapItem *hmi;

    hash = hash_key(k) % HASHTABLESIZE;
    for (hmi = hm->hash_table[hash]; hmi; hmi = hmi->hashnext) {
        if (equal_key(&hmi->key, k)) {
            return (hmi);
        }
    }
    hmi = add_hashmapitem(&hm->items, freelist);
    memcpy(&hmi->key, k, sizeof(*k));
    hmi->hashnext = hm->hash_table[hash];
    hm->hash_table[hash] = hmi;

    return (hmi);
}

struct hashMapItem *hashmap_retrieve(struct hashMap *hm, struct hashMapKey *k)
{
    unsigned long hash;
    struct hashMapItem *hmi;

    hash = hash_key(k) % HASHTABLESIZE;
    for (hmi = hm->hash_table[hash]; hmi; hmi = hmi->hashnext) {
        if (equal_key(&hmi->key, k)) {
            return (hmi);
        }
    }
    return NULL;
}

void purge_hashmap(struct hashMap *hm, struct hashMapItemList *freelist)
{
    int i;
    struct hashMapItem **p;

    /* first, remove items from hashed lists */
    for (i = 0; i < HASHTABLESIZE; i++) {
        for (p = hm->hash_table + i; *p;) {
            if ((*p)->marked_for_deletion) {
                *p = (*p)->hashnext;
            } else {
                p = &((*p)->hashnext);
            }
        }
    }
    /* then, remove from master list and send to freelist */
    purge_hmilist(&hm->items, freelist);
}

void zeroout_hashmap(struct hashMap *hm, struct hashMapItemList *freelist)
{
    unsigned int i;

    for (i = 0; i < HASHTABLESIZE; i++) {
        hm->hash_table[i] = NULL;
    }
    move_hmilist(freelist, &hm->items);
}

static struct hashMapItem *hashlist_contains(struct hashMapItem *hmi,
                                             struct hashMapKey *k)
{
    for (; hmi; hmi = hmi->hashnext) {
        if (equal_key(&hmi->key, k)) {
            return (hmi);
        }
    }
    return NULL;
}

void partition_hashmap(struct hashMapPartition *hmp,
                       struct hashMap *splitme, struct hashMap *reference)
{
    unsigned int i;
    struct hashMapItem *hmi;

    hmp->intersection = NULL;
    hmp->difference = NULL;
    for (i = 0; i < HASHTABLESIZE; i++) {
        for (hmi = splitme->hash_table[i]; hmi; hmi = hmi->hashnext) {
            if (hashlist_contains(reference->hash_table[i], &hmi->key)) {
                hmi->partitionnext = hmp->intersection;
                hmp->intersection = hmi;
            } else {
                hmi->partitionnext = hmp->difference;
                hmp->difference = hmi;
            }
        }
    }
}

void partition_cleanup(struct hashMapPartition *hmp)
{
    struct hashMapItem **phmi, *hminext;

    for (phmi = &hmp->intersection; *phmi; ) {
        hminext = *phmi;
        *phmi = NULL;
        phmi = &(hminext->partitionnext);
    }
    for (phmi = &hmp->difference; *phmi;) {
        hminext = *phmi;
        *phmi = NULL;
        phmi = &(hminext->partitionnext);
    }
}

static void hashmap_item_destroy(struct hashMapItem *hmi)
{
    reorderdata_destroy_missing_packets(&hmi->value.state_data.reorder.missingPackets);
    reorderdata_destroy_rd_buffer(&hmi->value.state_data.reorder.RD.buffer);
    reorderdata_destroy_rd_window(&hmi->value.state_data.reorder.RD.window);

    free_seqnorangelist(&hmi->value.agg_data.reorder.ranges);
    free_seqnorangelist(&hmi->value.agg_data.loss.ranges);

    free_seqnorangelist(&hmi->value.rep_data.loss.ranges);

    /* Free the item itself */
    free(hmi);
}

void hashmap_item_list_destroy(struct hashMapItemList *list)
{
    struct hashMapItem *hmi = list->head;
    while (hmi) {
        struct hashMapItem *victim = hmi;
        hmi = hmi->next;
        hashmap_item_destroy(victim);
    }
}

void hashmap_list_destroy(struct hashMapList *list)
{
    struct hashMap *hm = list->earliest;
    while (hm) {
        struct hashMap *hm_victim;
        struct hashMapItemList *l = &hm->items;
        struct hashMapItem *hmi = l->head;
        while (hmi) {
            struct hashMapItem *victim = hmi;
            hmi = hmi->next;
            hashmap_item_destroy(victim);
        }
        hm_victim = hm;
        hm = hm->next;
        free(hm_victim);
    }
}
