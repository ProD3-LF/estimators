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
/*                              fistq.c                                 */
/*                                                                      */
/* Author:      Tom Tantillo (ttantillo@appcomsci.com)                  */
/* File:        fistq.c                                                 */
/* Date:        Wed Aug 12 22:02:11 EDT 2015                            */
/*                                                                      */
/* Description: Module that implements the fistq data structure that    */
/*               threads use to pass messages to each other             */
/************************************************************************/

/**
 * The big idea:
 * Create a mutex-protected queue to allow threads to move data between them
 * hiding details of the mutex protection.
 * In addition, create a local queue to be used on a per-thread basis,
 * to minimize the amount of mutex locking.
 * The local queue can be appended to by the owning thread without regard
 * to locks, and periodically can flush to the mutex-protected queue.
 * The flush is a constant-time operation, since the queues are linked lists
 * and all the flush has to do, once it gets the lock, is append the head
 * of the local queue to the tail of the mutex-protected queue.
 * Reading is similar: the reading thread gets the lock, moves the entire
 * queue to its local queue by grabbing the head (which is the same as grabbing
 * the whole list), releases the lock, then returns one item at a time from
 * the local queue without a lock.
 *
 *
 * Intended Usage (diagram)
 * Reader threads (may be many)
 * and writer thread (should be one)
 * each have their own fistq_handle.
 * The fistq_handle has a localq (for the owning thread)
 * and a shared queue
 * The localq can be accessed without locks
 * and localq transfers to shared queue transfers to localq.
 *
 *                          /-----------------\
 *                          |                 |
 *                          | mutex-protected |
 *                          |                 |
 *            /-------------+------\   /------+-------------\
 *            |             .      |   |      .             |
 *            |writer thread.      |   |      .reader thread|
 *            |fistq_handle .      |   |      .fistq_handle |
 *            |             .      |   |      .             |
 * enqueue() -+->  localq  -+-> shared queue -+->  localq  -+-> dequeue()
 *            |             .      |   |      .             |
 *            \-------------+------/   \------+-------------/
 *                          |                 |
 *                          |                 |
 *                          \-----------------/
 */

#include "errno.h"
#include "fistq.h"

/************************************************************************/
/*                                                                      */
/*      Global Variables                                                */
/*                                                                      */
/************************************************************************/

fistq_t           fistq_list_head; /* head of queue of existing fistq_t */
u_int32_t         fistq_list_size; /* size of fistq_t list */
pthread_mutex_t   fistq_list_lock; /* mutex lock for managing queue */

#if 0
extern u_int32_t init_perf_low_watermark;      /* performance monitoring */
extern u_int32_t init_perf_high_watermark;     /* performance monitoring */
extern u_int32_t init_perf_high_watermark_gap; /* performance monitoring */
#endif

/************************************************************************/
/*                                                                      */
/*      Private Function Forward Declarations                           */
/*                                                                      */
/************************************************************************/

static void fistq_free(fistq_t *fq);
static void queue_free(queue_t *q);
static fistq_t *fistq_create_unsafe(char *src, char *dst, u_int32_t value, fistq_data_cb *cb);
static fistq_t *fistq_find_unsafe(char *src, char *dst);

/************************************************************************/
/*                                                                      */
/*      Function Definitions                                            */
/*                                                                      */
/************************************************************************/

/***************************************************************************/
/* Initialize the fistq list                                               */
/***************************************************************************/
void fistq_init()
{
    memset(&fistq_list_head, 0, sizeof(fistq_t));
    fistq_list_size = 0;
    pthread_mutex_init(&fistq_list_lock, NULL);
}

/***************************************************************************/
/* Destroy the fistq list                                                  */
/***************************************************************************/
void fistq_destroy()
{
    fistq_t *fq;

    /* Grab the management lock first in case another thread if finishing up */
    pthread_mutex_lock(&fistq_list_lock);

    /* Delete (and clean memory) of all remaining allocated fistq */
    while (fistq_list_head.next != NULL) {
        fq = fistq_list_head.next;
        fistq_list_head.next = fq->next;
        fistq_free(fq);
    }

    /* Unlock the mutex and destroy the lock */
    pthread_mutex_unlock(&fistq_list_lock);
    pthread_mutex_destroy(&fistq_list_lock);
}

/***************************************************************************/
/* Cleanup the internal fist_q data structures                             */
/***************************************************************************/
static void fistq_free(fistq_t *fq)
{
    /* Sanity check for the fistq object */
    if (fq == NULL)
        return;

    /* Sanity check for string memory */
    if (fq->src != NULL)
        free(fq->src);
    if (fq->dst != NULL)
        free(fq->dst);

    /* Destroy mutex and condition variables */
    pthread_mutex_destroy(&fq->mutex);
    pthread_cond_destroy(&fq->cond);

    /* Free the internal shared queue -- this should be safe because   */
    /*   either no fistq_handles (localq) objects reference this fistq */
    /*   or we have reached EOL (graceful exit)                        */
    queue_free(&fq->internal);
    free(fq);
}

/***************************************************************************/
/* Cleanup a queue, potentially freeing the stored data pointers. We can   */
/*   store a pointer to a function that will clean the data pointer - if   */
/*   it is NULL, don't bother trying to free the data                      */
/***************************************************************************/
static void queue_free(queue_t *q)
{
    queue_node_t *qn;

    /* Sanity check on queue object */
    if (q == NULL)
        return;

    /* Go through remaning queue nodes, free data if the option is
     * set, and finally free the queue_node itself */
    while (q->head.next != NULL) {
        qn = q->head.next;
        q->head.next = qn->next;
        if (q->free_data == FISTQ_FREE)
            q->cb ? (*q->cb)(qn->data) : free(qn->data);
        free(qn);
    }
}

/***************************************************************************/
/* Get a fistq_handle (localq_t object) -- Makes a new local queue and     */
/*   uses the src and dst names to look for an existing fistq. If no fistq */
/*   exists, one is created (this is a mutex-protected operation that is   */
/*   executed internally. In addition, sets the threshold of the local     */
/*   queue to the default value (DEFAULT_THRESHOLD)                        */
/***************************************************************************/
fistq_handle *fistq_getHandle(char *src, char *dst, u_int32_t value,
                                fistq_data_cb *cb)
{
    fistq_handle *fh;

    /* Sanity check on the input */
    if (src == NULL || strcmp(src, "") == 0)
        return NULL;
    if (dst == NULL || strcmp(dst, "") == 0)
        return NULL;

    /* Allocate a new fistq_handle object and fill in accordingly */
    fh = calloc(1, sizeof(*fh));
    if (!fh) {
        fprintf(stderr, "calloc failed\n");
        return NULL;
    }
    fh->lq.tail = &fh->lq.head;
    fh->lq.cb = cb;
    fh->lq.free_data = (free_option_t)value;
    fh->threshold = DEFAULT_THRESHOLD;
    // fh->perf_low_watermark = init_perf_low_watermark;
    // fh->perf_high_watermark = init_perf_high_watermark;
    // fh->perf_high_watermark_gap = init_perf_high_watermark_gap;

    /* Grab the management lock to search for the fistq */
    pthread_mutex_lock(&fistq_list_lock);

    /* Try to find an existing fistq, if not, create a new one */
    fh->fq = fistq_find_unsafe(src, dst);
    if (fh->fq == NULL) {
        fh->fq = fistq_create_unsafe(src, dst, value, cb);
        if (!fh->fq) {
            free(fh);
            return NULL;
        }
    }

    /* Release the lock */
    pthread_mutex_unlock(&fistq_list_lock);

    return fh;
}

/***************************************************************************/
/* Search for a fistq_t object                                             */
/***************************************************************************/
static fistq_t *fistq_find_unsafe(char *src, char *dst)
{
    fistq_t *fq = fistq_list_head.next;

    /* Perform a linear search for a matching (src,dst) fistq */
    while (fq != NULL) {
        /* If we find a match, grab the specific fistq mutex to increase */
        /*   the ref_count safely                                        */
        if (strcmp(fq->src, src) == 0 && strcmp(fq->dst, dst) == 0) {
            pthread_mutex_lock(&fq->mutex);
            fq->ref_count++;
            pthread_mutex_unlock(&fq->mutex);
            return fq;
        }

        fq = fq->next;
    }

    return NULL;
}

// the clock for the pthread_cond_timedwait call.
// CLOCK_MONOTONIC_COARSE doesn't seem to work on nash in the cond var.
static clockid_t fistq_clockid = CLOCK_MONOTONIC;
void fistq_setclock(clockid_t cid)
{
    fistq_clockid = cid;
}

clockid_t fistq_getclock(void)
{
    return fistq_clockid;
}

/***************************************************************************/
/* Create a fistq_t object                                                 */
/***************************************************************************/
static fistq_t *fistq_create_unsafe(char *src, char *dst, u_int32_t value, fistq_data_cb *cb)
{
    pthread_condattr_t ca;

    /* Allocate memory for a new fistq object */
    fistq_t *fq = calloc(1, sizeof(*fq));
    if (!fq) {
        fprintf(stderr, "calloc failed\n");
        return NULL;
    }

    /* Create the associated mutex and condition variable */
    pthread_mutex_init(&fq->mutex, NULL);

    pthread_condattr_init(&ca);
    pthread_condattr_setclock(&ca, fistq_clockid);
    pthread_cond_init(&fq->cond, &ca);

    /* Init the shared internal queue */
    fq->internal.tail = &fq->internal.head;
    fq->internal.cb = cb;
    fq->internal.free_data = (free_option_t)value;

    /* Assign the src, dst, value, and ref_count */
    fq->src = strdup(src);
    fq->dst = strdup(dst);
    fq->value = value;
    fq->ref_count = 1;

    /* Insert the new fistq at the head of the list -- temporal locality */
    /*   is preserved with notion that more handles to this fistq in the */
    /*   near future                                                     */
    fq->next = fistq_list_head.next;
    fq->prev = &fistq_list_head;
    if (fistq_list_head.next != NULL)
        fistq_list_head.next->prev = fq;
    fistq_list_head.next = fq;

    /* Increment size of overall number of fistq objects */
    fistq_list_size++;

    return fq;
}

/***************************************************************************/
/* Destroy a fistq_handle -- If this is the last local queue that          */
/*   references the shared fistq, destroy the fistq from the global list   */
/***************************************************************************/
int fistq_destroyHandle(fistq_handle *fh)
{
    unsigned char removeFQ = 0;

    /* Sanity check on fistq_handle */
    if (fh == NULL)
        return -1;

    /* First, free the local queue */
    queue_free(&fh->lq);

    /* Grab the mutex lock on the fistq manager list and specific fistq
       We use this specific order of (global, local) locks because it matches
       the order of the fistq_getHandle and we need to keep this order to
       avoid deadlock. Instead, we can lock the local here
       only, check ref_count, release local, get global + local and check
       ref_count again. If still 0, proceed, Else, abort. */
    pthread_mutex_lock(&fistq_list_lock);
    pthread_mutex_lock(&fh->fq->mutex);

    /* Decrease the ref count */
    fh->fq->ref_count--;

    /* If this was the last handle to the fistq, delete the fistq */
    if (fh->fq->ref_count == 0) {
        /* Set the remove flag for later on */
        removeFQ = 1;

        /* Remove this fistq from the fistq_list and update the size */
        fh->fq->prev->next = fh->fq->next;
        if (fh->fq->next != NULL)
            fh->fq->next->prev = fh->fq->prev;
        fistq_list_size--;
    }

    /* Unlock the specific fistq mutex and list lock */
    pthread_mutex_unlock(&fh->fq->mutex);
    pthread_mutex_unlock(&fistq_list_lock);

    /* If this is the last ref count, destroy the fistq object */
    if (removeFQ)
        fistq_free(fh->fq);

    /* Free the fistq_handle object */
    free(fh);

    return 0;
}

static void fistq_direct(fistq_handle *fh, queue_node_t *qn);

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
int fistq_enqueue_any(fistq_handle *fh, void *data, fistq_data_type type, flush_option_t op)
{
    queue_node_t *qn;

    /* Sanity check on the fistq_handle and data to be stored */
    if (fh == NULL || data == NULL) {
        return -1;
    }

    /* Allocate a new queue_node and assign fields */
    qn = calloc(1, sizeof(*qn));
    if (!qn) {
        fprintf(stderr, "calloc failed\n");
        return -1;
    }

    qn->data = data;
    qn->type = type;
    qn->next = NULL;

    if (op == FISTQ_FLUSH) {
        fistq_direct(fh, qn);
    }
    else {
        /* Append new queue_node to the end of the local queue, update size */
        fh->lq.tail->next = qn;
        fh->lq.tail = qn;
        fh->lq.size++;

        /* Flush the local queue to the shared internal if it is time to flush */
        if (op != FISTQ_NOFLUSH && fh->lq.size >= fh->threshold) {
            fistq_flush(fh);
        }
    }

    return 0;
}

/***************************************************************************/
/* Flush the local queue to the fistq internal queue. */
/***************************************************************************/
int fistq_flush(fistq_handle *fh)
{
    /* Sanity check on input */
    if (fh == NULL || fh->lq.size == 0)
        return -1;

    /* Grab the lock on the internal fistq queue */
    pthread_mutex_lock(&fh->fq->mutex);

    /* Move from the local queue to the internal queue */
    fh->fq->internal.tail->next = fh->lq.head.next;
    fh->fq->internal.tail = fh->lq.tail;

    /* Reset the local queue to be empty */
    fh->lq.head.next = NULL;
    fh->lq.tail = &fh->lq.head;

    /* Update sizes of both queues */
    fh->fq->internal.size += fh->lq.size;
    fh->lq.size = 0;

    /* Release the lock on the shared internal queue and signal any
     * thread that is waiting to read off of the internal qeueue */
    pthread_cond_signal(&fh->fq->cond);
    pthread_mutex_unlock(&fh->fq->mutex);

    return 0;
}

// Add a single item directly to the internal queue.
// This bypasses the local un-mutex'd queue.
// This is for the problem of multiple threads wanting to use the same queue.
// In particular, multiple classifier threads sharing a flow,
// and the flow has a specific worker assigned to it.
static void fistq_direct(fistq_handle *fh, queue_node_t *qn)
{
    /* Grab the lock on the internal fistq queue */
    pthread_mutex_lock(&fh->fq->mutex);

    /* Add this one item to the internal queue */
    fh->fq->internal.tail->next = qn;
    fh->fq->internal.tail = qn;
    fh->fq->internal.size += 1;

    /* Release the lock on the shared internal queue and signal any */
    /*   thread that is waiting to read off of the internal qeueue  */
    pthread_cond_signal(&fh->fq->cond);
    pthread_mutex_unlock(&fh->fq->mutex);
}

/***************************************************************************/
/* Dequeue the oldest data object (front of queue) -- Remove the head      */
/*   (oldest object) from the queue. If the local queue is not empty, grab */
/*   the head of the local queue. If this queue is empty, wait on the      */
/*   fistq internal queue to transfer new data to the local queue, then    */
/*   return the head of the now populated local queue. NULL is returned at */
/*   graceful shutdown (EOL)                                               */
/***************************************************************************/
void *fistq_dequeue_any(fistq_handle *fh, fistq_data_type *type)
{
    void *dat;
    queue_node_t *qn;

    /* Sanity check on input fistq_handle */
    if (fh == NULL) {
        *type = FISTQ_TYPE_NULL;
        return NULL;
    }

    /* If local queue is empty... */
    if (fh->lq.size == 0) {
        /* Wait for new messages -- wait on condition variable to be signaled */
        pthread_mutex_lock(&fh->fq->mutex);
        while (fh->fq->internal.size == 0)
            pthread_cond_wait(&fh->fq->cond, &fh->fq->mutex);

        /* Move from the internal queue to the local queue */
        fh->lq.tail->next = fh->fq->internal.head.next;
        fh->lq.tail = fh->fq->internal.tail;

        /* Reset the local queue to be empty */
        fh->fq->internal.head.next = NULL;
        fh->fq->internal.tail = &fh->fq->internal.head;

        /* Update sizes of both queues */
        fh->lq.size += fh->fq->internal.size;
        fh->fq->internal.size = 0;

        /* Release lock on the internal fistq */
        pthread_mutex_unlock(&fh->fq->mutex);

        /* Sanity check */
        if (fh->lq.size == 0) {
            *type = FISTQ_TYPE_NULL;
            return NULL;
        }
    }

    /* Remove the head queue_node from the local queue */
    qn = fh->lq.head.next;
    fh->lq.head.next = qn->next;
    fh->lq.size--;
    if (fh->lq.size == 0)
        fh->lq.tail = &fh->lq.head;

    /* Grab a pointer to the data and free the queue_node */
    dat = qn->data;
    *type = qn->type;
    free(qn);

    return dat;
}

void *fistq_timeddequeue_any(fistq_handle *fh, fistq_data_type *type, struct timespec *tv)
{
    void *dat;
    queue_node_t *qn;

    /* If local queue is empty... */
    if (fh->lq.size == 0)
    {
        int timeout;

        /* Wait for new messages -- wait on condition variable to be signaled */
        pthread_mutex_lock(&fh->fq->mutex);
        while (fh->fq->internal.size == 0)
            if (pthread_cond_timedwait(&fh->fq->cond, &fh->fq->mutex, tv) == ETIMEDOUT)
                break;

        if (fh->fq->internal.size == 0)
            timeout = 1;
        else
        {
            timeout = 0;

            /* Move from the internal queue to the local queue */
            fh->lq.tail->next = fh->fq->internal.head.next;
            fh->lq.tail = fh->fq->internal.tail;

            /* Reset the local queue to be empty */
            fh->fq->internal.head.next = NULL;
            fh->fq->internal.tail = &fh->fq->internal.head;

            /* Update sizes of both queues */
            fh->lq.size += fh->fq->internal.size;
            fh->fq->internal.size = 0;
        }

        /* Release lock on the internal fistq */
        pthread_mutex_unlock(&fh->fq->mutex);

        if (timeout) {
            *type = FISTQ_TYPE_TIMEOUT;
            return NULL;
        }
    }

    /* Remove the head queue_node from the local queue */
    qn = fh->lq.head.next;
    fh->lq.head.next = qn->next;
    fh->lq.size--;
    if (fh->lq.size == 0) {
        fh->lq.tail = &fh->lq.head;
    }

    /* Grab a pointer to the data and free the queue_node */
    dat = qn->data;
    *type = qn->type;
    free(qn);

    return dat;
}

char *fistq_type2name(fistq_data_type type)
{
    char *name;

    switch (type)
    {
    case FISTQ_TYPE_NULL:    name = "NULL"; break;
    case FISTQ_TYPE_TIMEOUT: name = "TIMEOUT"; break;
    case FISTQ_TYPE_PINFO:   name = "PINFO"; break;
    default:                 name = "Undefined"; break;
    }

    return name;
}

/***************************************************************************/
/* Set the threshold of flushing from the local to fistq internal queue    */
/***************************************************************************/
void fistq_setThreshold(fistq_handle *fh, u_int16_t t)
{
    /* Sanity check on input */
    if (fh == NULL)
        return;

    /* Assign new threshold value */
    fh->threshold = t;
}

/***************************************************************************/
/* Get the size of the local queue                                         */
/***************************************************************************/
int fistq_getLocalSize(fistq_handle *fh)
{
    /* Sanity check on input */
    if (fh == NULL)
        return -1;

    /* Return the local queue size */
    return fh->lq.size;
}

/***************************************************************************/
/* Get the size of the fistq internal queue                                */
/***************************************************************************/
int fistq_getSize(fistq_handle *fh)
{
    int size;

    /* Sanity check on input */
    if (fh == NULL || fh->fq == NULL)
        return -1;

    /* Grab the mutex of the internal fistq */
    pthread_mutex_lock(&fh->fq->mutex);

    /* Get the size of the internal queue */
    size = fh->fq->internal.size;

    /* Release the lock on the internal queue */
    pthread_mutex_unlock(&fh->fq->mutex);

    return size;
}
