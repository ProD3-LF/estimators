/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2015-2023 Applied Communication Sciences
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
/*                              fistq.h                                 */
/*                                                                      */
/* Author:      Tom Tantillo (ttantillo@appcomsci.com)                  */
/* File:        fistq.h                                                 */
/* Date:        Wed Aug 12 21:29:00 EDT 2015                            */
/*                                                                      */
/* Description: Module that implements the fistq data structure that    */
/*               threads use to pass messages to each other             */
/*                                                                      */
/************************************************************************/

#ifndef FISTQ_H
#define FISTQ_H

#include <assert.h>
#include <stdio.h>          /* standard input/output routines           */
#include <stdlib.h>         /* system calls - e.g. malloc()             */
#include <string.h>         /* for string functions                     */
#include <pthread.h>        /* for mutex and condition variables        */
#include <time.h>           /* for mutex and condition variables        */


/************************************************************************/
/*                                                                      */
/*      Program Parameters                                              */
/*                                                                      */
/************************************************************************/

#define DEFAULT_THRESHOLD 5 /* Flush local queue to internal fistq if   */
                            /*   local queue reaches this threshold     */

/* fistq enqueue flush options */
typedef enum {
    FISTQ_DEFAULT,          /* Normal operation: flush at threshold     */
    FISTQ_FLUSH,            /* Force immediate flush (after this        */
                            /*   element is enqueued)                   */
    FISTQ_NOFLUSH,          /* Prevent flush after this element is      */
                            /*   enqueued (even if >= threshold)        */
} flush_option_t;

typedef enum {
    FISTQ_FREE,             /* Free data leftover in the queue at EOL   */
    FISTQ_NOFREE,           /* Don't free data leftover in queue at EOL */
} free_option_t;

// When changing/adding types, be sure to update fistq_type2name()
typedef enum
{
    FISTQ_TYPE_NULL,
    FISTQ_TYPE_TIMEOUT,

    FISTQ_TYPE_PINFO,
} fistq_data_type;

/************************************************************************/
/*                                                                      */
/*      Data Structures                                                 */
/*                                                                      */
/************************************************************************/

/* Wrapper for callback function to free data stored in queue_nodes  */
/*   during EOL cleanup/destroy functions                            */
typedef void fistq_data_cb(void *);

/* Node for storing generic data pointers */
typedef struct queue_node {
    void                *data;      /* Pointer to the data */
    struct queue_node   *next;      /* Next pointer */
    fistq_data_type      type;
} queue_node_t;

/* Queue containing generic data nodes */
typedef struct queue {
    u_int32_t           size;       /* Number of nodes in the queue */
    queue_node_t        head;       /* Dummy node for head of queue */
    queue_node_t        *tail;      /* Tail pointer */
    fistq_data_cb       *cb;        /* Callback function to free data */
    free_option_t       free_data;  /* Option to free leftover data at EOL */
    /* obj_type type; */            /* Type of data stored in the queue */
} queue_t;

/* Fistq data structure -- Mutex and condition variable correspong to */
/* locking and signaling the internal queue -- Threads connect to a   */
/* fistq by specifying src and dst names                              */
typedef struct fistq {
    pthread_mutex_t mutex;          /* Mutex to lock/unlock */
    pthread_cond_t  cond;           /* Condition variable to wake threads */
    queue_t         internal;       /* Mutex-protected data queue */
    char            *src;           /* Source Name (e.g. redreader) */
    char            *dst;           /* Destination Name (e.g. redsender) */
    u_int32_t       value;          /* Optional parameters */
    int             ref_count;      /* Reference Count */
    struct fistq    *next;          /* Pointer to next fistq in linked list */
    struct fistq    *prev;          /* Pointer to prev fistq in linked list */
} fistq_t;

/* Localq data structure -- Users of a fistq have a pointer to the shared */
/* internal queue via the fistq_t pointer and maintain their own local    */
/* queue. Localq is flushed to internal fistq if threshold is reached     */
typedef struct localq {
    int id;                         /* id for owner of queue. */
    fistq_t         *fq;            /* Pointer to the fistq structure */
    queue_t         lq;             /* Local queue */
    u_int16_t       threshold;      /* Threshold at which to flush localq */
    /* reader/writer flag for internal assertions? */
    int perf_low_watermark;         /* Queue size low threshold */
    int perf_high_watermark;        /* Queue size high threshold */
    int perf_high_watermark_gap;    /* Queue size low high th diff */
} localq_t;

/* Wrapper for localq_t to make things nice for users of this module */
typedef localq_t fistq_handle;

/************************************************************************/
/*                                                                      */
/*      Public Function Declarations                                    */
/*                                                                      */
/************************************************************************/

/***************************************************************************/
/* Initialize the fistq manager -- Only called once                        */
/***************************************************************************/
void fistq_init();

/***************************************************************************/
/* Destroy the fistq manager -- Only called once                           */
/***************************************************************************/
void fistq_destroy();

/***************************************************************************/
/* Get a fistq_handle (localq_t object) -- Makes a new local queue and     */
/*   uses the src and dst names to look for an existing fistq. If no fistq */
/*   exists, one is created (this is a mutex-protected operation that is   */
/*   executed internally. In addition, sets the threshold of the local     */
/*   queue to the default value (DEFAULT_THRESHOLD)                        */
/***************************************************************************/
fistq_handle *fistq_getHandle(char *src, char *dst, u_int32_t value,
                                fistq_data_cb *cb);

/***************************************************************************/
/* Destroy a fistq_handle -- If this is the last local queue that          */
/*   references the shared fistq, destroy the fistq from the global list   */
/***************************************************************************/
int fistq_destroyHandle(fistq_handle *fh);

/***************************************************************************/
/* Enqueue a data object -- Creates a new queue node and appends it to the */
/*   end of the local queue. If the threshold has then been reached, the   */
/*   local queue is flushed to the fistq internal queue. This is the       */
/*   behavior of the default (FISTQ_DEFAULT) option. If FISTQ_FLUSH is     */
/*   specified, the local queue is flushed immediately after the enqueue.  */
/*   If FISTQ_NOFLUSH is specified, the local queue is not flushed, even if*/
/*   the threshold is reached (it will be flushed in the future during the */
/*   next FISTQ_DEFAULT, FISTQ_FLUSH, or explicit fistq_flush call.        */
/***************************************************************************/
extern int fistq_enqueue_any(fistq_handle *fh, void *data, fistq_data_type type, flush_option_t op);

struct packetinfo;

#define MK_SPECIFIC(v, n, C) \
static inline int fistq_enqueue_##n(fistq_handle *fh, v *data, flush_option_t op) \
{                                                                                 \
    return fistq_enqueue_any(fh, data, C, op);                                    \
}                                                                                 \

MK_SPECIFIC(struct packetinfo,        pinfo, FISTQ_TYPE_PINFO)
#undef MK_SPECIFIC

/***************************************************************************/
/* Flush the local queue to the fistq internal queue. */
/***************************************************************************/
int fistq_flush(fistq_handle *fh);

/***************************************************************************/
/* Dequeue the oldest data object (front of queue) -- Remove the head      */
/*   (oldest object) from the queue. If the local queue is not empty, grab */
/*   the head of the local queue. If this queue is empty, wait on the      */
/*   fistq internal queue to transfer new data to the local queue, then    */
/*   return the head of the now populated local queue. NULL is returned at */
/*   graceful shutdown (EOL)                                               */
/***************************************************************************/
extern void *fistq_dequeue_any(fistq_handle *fh, fistq_data_type *type);

#define MK_SPECIFIC(v, n, C)                                                    \
static inline v *fistq_dequeue_##n(fistq_handle *fh)                            \
{                                                                               \
    fistq_data_type type;                                                       \
    v *p;                                                                       \
    p = fistq_dequeue_any(fh, &type);                                           \
    assert((type == C && p != NULL) || (type == FISTQ_TYPE_NULL && p == NULL)); \
    return p;                                                                   \
}

MK_SPECIFIC(struct packetinfo,        pinfo, FISTQ_TYPE_PINFO)
#undef MK_SPECIFIC

extern void *fistq_timeddequeue_any(fistq_handle *fh, fistq_data_type *type, struct timespec *tv);

#define MK_SPECIFIC(v, n, C)                                                    \
static inline v *fistq_timeddequeue_##n(fistq_handle *fh, struct timespec *tv)  \
{                                                                               \
    fistq_data_type type;                                                       \
    v *p;                                                                       \
    p = fistq_timeddequeue_any(fh, &type, tv);                                           \
    assert((type == C && p != NULL) || ((type == FISTQ_TYPE_NULL || type == FISTQ_TYPE_TIMEOUT) && p == NULL)); \
    return p;                                                                   \
}

MK_SPECIFIC(struct packetinfo,        pinfo, FISTQ_TYPE_PINFO)
#undef MK_SPECIFIC

extern char *fistq_type2name(fistq_data_type type);

extern clockid_t fistq_getclock(void);
extern void fistq_setclock(clockid_t cid);

/***************************************************************************/
/* Set the threshold of flushing from the local to fistq internal queue    */
/***************************************************************************/
void fistq_setThreshold(fistq_handle *fh, u_int16_t t);

/***************************************************************************/
/* Get the size of the local queue                                         */
/***************************************************************************/
int fistq_getLocalSize(fistq_handle *fh);

/***************************************************************************/
/* Get the size of the fistq internal queue                                */
/***************************************************************************/
int fistq_getSize(fistq_handle *fh);

/*************************************************************************************/
/* Log queue size threshold crossings                                                */
/*************************************************************************************/
// void fistq_monitorQueueSize(fistq_handle* qh, zlog_category_t* zc_perf);

/* void fistq_query(); */

#endif /* FISTQ_H */
