# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2022-2023 Peraton Labs Inc.
#
# This software was developed in work supported by the following U.S.
# Government contracts:
#
# HR0011-15-C-0098
# HR0011-20-C-0160
#
# Any opinions, findings and conclusions or recommendations expressed in
# this material are those of the author(s) and do not necessarily reflect
# the views, either expressed or implied, of the U.S. Government.
#
# DoD Distribution Statement A
# Approved for Public Release, Distribution Unlimited
#
# DISTAR Case 37651, cleared February 13, 2023.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0

.PHONY: clean depend test

CC = gcc

CFLAGS =
CFLAGS += -g
CFLAGS += -Wall
CFLAGS += -Werror
CFLAGS += -Wextra
CFLAGS += -fPIC

OBJECTS =
OBJECTS += crc.o
OBJECTS += datatypes.o
OBJECTS += fistq.o
OBJECTS += flowstate.o
OBJECTS += hashmap2.o
OBJECTS += lossdata.o
OBJECTS += packetdata.o
OBJECTS += pd3_estimator.o
OBJECTS += queue.o
OBJECTS += rbtree.o
OBJECTS += reorderdata.o
OBJECTS += reportschedule.o

SOURCES = $(OBJECTS:.o=.c)

LIB_TARGET = libpd3_estimator.so

LDLIBS =
LDLIBS += -lm
LDLIBS += -lpthread

TEST_TARGET = test_loss test_reorder

default: depend $(LIB_TARGET)

$(LIB_TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $(LIB_TARGET) $(OBJECTS) -shared

depend: .depend

.depend: $(SOURCES)
	rm -f "$@"
	$(CC) $(CFLAGS) -MM $^ > "$@"

include .depend

test: $(TEST_TARGET)

test_loss: $(LIB_TARGET) test_loss.o
	$(CC) -o $@ test_loss.o -L. -lpd3_estimator $(LDLIBS)

test_reorder: $(LIB_TARGET) test_reorder.o
	$(CC) -o $@ test_reorder.o -L. -lpd3_estimator $(LDLIBS)

clean:
	rm -f *.o
	rm -f $(LIB_TARGET)
	rm -f .depend
	rm -f $(TEST_TARGET)
