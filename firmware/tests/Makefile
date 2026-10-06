## Host unit tests for the hardware independent parts of the firmware
##
##   make -C firmware/tests

CC      ?= cc
CFLAGS  := -std=gnu99 -Wall -Wextra -Wno-unused-parameter -g -I../include -I../source '-D__attribute__(x)='  # the firmware's linker section attributes are ARM ELF only
SOURCES := dmrDataTest.c ../source/functions/dmrData.c ../source/hotspot/CRC.c ../source/hotspot/dmrUtils.c ../source/hotspot/dmrDataFrame.c ../source/hotspot/BPTC19696.c ../source/hotspot/Hamming.c ../source/hotspot/DMRSlotType.c ../source/hotspot/dmrDefines.c

TSHARK  ?= $(firstword $(wildcard /Applications/Wireshark*.app/Contents/MacOS/tshark) $(shell command -v tshark))

all: run

dmrDataTest: $(SOURCES) ../include/functions/dmrData.h ../include/hotspot/dmrDataFrame.h
	$(CC) $(CFLAGS) -o $@ $(SOURCES)

ipGatewayTest: ipGatewayTest.c ../source/functions/ipGateway.c ../include/functions/ipGateway.h
	$(CC) $(CFLAGS) -o $@ ipGatewayTest.c ../source/functions/ipGateway.c

# The same gateway test with a /16 plan: radios 10.250.0.0/16, talkgroups 10.251.0.0/16, multicast 239.250.0.0/16
IPGW16  := -DIPGW_INDIVIDUAL_NET=0x0AFA0000u -DIPGW_INDIVIDUAL_PREFIX=16 -DIPGW_GROUP_NET=0x0AFB0000u -DIPGW_GROUP_PREFIX=16 \
           -DIPGW_MULTICAST_NET=0xEFFA0000u -DIPGW_MULTICAST_PREFIX=16 -DIPGW_LINK_PREFIX=15 '-DPCAP_NAME="ipGatewayTest16.pcap"'

ipGatewayTest16: ipGatewayTest.c ../source/functions/ipGateway.c ../include/functions/ipGateway.h
	$(CC) $(CFLAGS) $(IPGW16) -o $@ ipGatewayTest.c ../source/functions/ipGateway.c

# The default ranges on a /31 link, and on a shared /8 link, instead of the /32
ipGatewayTest31: ipGatewayTest.c ../source/functions/ipGateway.c ../include/functions/ipGateway.h
	$(CC) $(CFLAGS) -DIPGW_LINK_PREFIX=31 '-DPCAP_NAME="ipGatewayTest31.pcap"' -o $@ ipGatewayTest.c ../source/functions/ipGateway.c

ipGatewayTestShared: ipGatewayTest.c ../source/functions/ipGateway.c ../include/functions/ipGateway.h
	$(CC) $(CFLAGS) -DIPGW_LINK_PREFIX=8 '-DPCAP_NAME="ipGatewayTestShared.pcap"' -o $@ ipGatewayTest.c ../source/functions/ipGateway.c

run: dmrDataTest ipGatewayTest ipGatewayTest16 ipGatewayTest31 ipGatewayTestShared
	./dmrDataTest > dmrDataTest.out; status=$$?; grep -E "^(FAIL|# (PASSED|FAILED))" dmrDataTest.out; exit $$status
	./ipGatewayTest
	./ipGatewayTest16
	./ipGatewayTest31
	./ipGatewayTestShared
ifneq ($(TSHARK),)
	@# Independent check of the frames the gateway sent: no malformed packets or bad checksums
	@for f in ipGatewayTest.pcap ipGatewayTest16.pcap ipGatewayTest31.pcap ipGatewayTestShared.pcap; do "$(TSHARK)" -r $$f -o ip.check_checksum:TRUE -o udp.check_checksum:TRUE -z expert,note -q; done > ipGatewayTest.expert
	@if grep -q -i -E "malformed|bad|incorrect|error" ipGatewayTest.expert; then echo "# tshark found problems"; cat ipGatewayTest.expert; exit 1; else echo "# tshark: no malformed packets or bad checksums"; fi
endif

clean:
	rm -f dmrDataTest dmrDataTest.out ipGatewayTest ipGatewayTest16 ipGatewayTest31 ipGatewayTestShared ipGatewayTest.pcap ipGatewayTest16.pcap ipGatewayTest31.pcap ipGatewayTestShared.pcap ipGatewayTest.expert

.PHONY: all run clean
