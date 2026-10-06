## Host unit tests for the hardware independent parts of the firmware
##
##   make -C firmware/tests

CC      ?= cc
CFLAGS  := -std=gnu99 -Wall -Wextra -Wno-unused-parameter -g -I../include -I../source '-D__attribute__(x)='  # the firmware's linker section attributes are ARM ELF only
SOURCES := dmrDataTest.c ../source/functions/dmrData.c ../source/hotspot/CRC.c ../source/hotspot/dmrUtils.c ../source/hotspot/dmrDataFrame.c ../source/hotspot/BPTC19696.c ../source/hotspot/Hamming.c ../source/hotspot/DMRSlotType.c ../source/hotspot/dmrDefines.c

all: run

dmrDataTest: $(SOURCES) ../include/functions/dmrData.h ../include/hotspot/dmrDataFrame.h
	$(CC) $(CFLAGS) -o $@ $(SOURCES)

run: dmrDataTest
	./dmrDataTest > dmrDataTest.out; status=$$?; grep -E "^(FAIL|# (PASSED|FAILED))" dmrDataTest.out; exit $$status

clean:
	rm -f dmrDataTest dmrDataTest.out

.PHONY: all run clean
