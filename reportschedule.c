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

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include "datatypes.h"
#include "reportschedule.h"

/* Schedule is specified by a string of semicolon-separated repeating reports.
 * Each repeating report specification is comma-separated and contains:
 * - destination(s)
 * - repeating interval (in seconds)
 * - offset (in seconds)
 *
 * example:d,1;hr,5,0;h,5,2.5
 * - report to 'd' every second
 * - report to 'h' every 2.5 seconds, each report covering 5 seconds
 * - report to 'r' every 5 seconds
 */

struct repeating_item {
  char *outlets;
  TIMEINTERVAL interval;
  TIMESTAMP next_run;
};

static TIMESTAMP timezero;
static unsigned int nitems = 0;
static struct repeating_item *schedule;

static char *tokenize(char *s, char sep);

static TIMESTAMP now() {
  struct timeval tv;

  (void) gettimeofday(&tv, NULL);
  return (TIMESTAMP) tv.tv_sec * 1e6 + (TIMESTAMP) tv.tv_usec;
}

int set_schedule(char *sch)
{
    unsigned int n;
    char *t;

    for (n = 1, t = sch; *t != '\0'; t++) {
        if (*t == ';') {
            n++;
        }
    }
    schedule = calloc(n, sizeof(*schedule));
    if (!schedule) {
        fprintf(stderr, "calloc failed\n");
        return -1;
    }
    timezero = now();
    while (sch) {
        t = tokenize(sch, ';');
        schedule[nitems].outlets = sch;
        sch = tokenize(sch, ',');
        if (!sch) {
            return -1;
        } else if (!isdigit(*sch) && *sch != '.') {
            return -1;
        } else {
            schedule[nitems].interval = (TIMEINTERVAL) (1000000 * atof(sch));
            schedule[nitems].next_run = timezero + schedule[nitems].interval;
        }
        sch = tokenize(sch, ',');
        if (!sch || atof(sch) == 0.0) {
            /* nothing else to do */
        } else if (!isdigit(*sch) && *sch != '.') {
            return -1;
        } else {
            schedule[nitems].next_run = timezero + (TIMEINTERVAL) (1000000 * atof(sch));
        }

        /* success */
        nitems++;
        sch = t;
    }
    return 0; /* No errors */
}

void destroy_schedule()
{
    free(schedule);
    schedule = NULL;
    nitems = 0;
}

static char *tokenize(char *s, char sep) {
  /* caller should set a pointer to first token */
  /* return value is beginning of next token */
  for ( ; ; s++) {
    if (*s == sep) {
      *s = '\0';
      return (s + 1);
    } else if (*s == '\0') {
        return NULL;
    }
  }
}

unsigned int schedule_parallelism(void) {
  return (nitems);
}

char *schedule_outlets(unsigned int x) {
  return (now() < schedule[x].next_run ? NULL : schedule[x].outlets);
}

void schedule_reset(unsigned int x) {
  struct repeating_item *ri;

  ri = schedule + x;
  ri->next_run += (TIMEINTERVAL) (ri->interval *
          ceil((double) (now() - ri->next_run) / (double) ri->interval));
}

TIMEINTERVAL get_duration(unsigned int x) {
  if (x < nitems) {
    return (schedule[x].interval);
  } else {
    return ((TIMEINTERVAL) 0);
  }
}
