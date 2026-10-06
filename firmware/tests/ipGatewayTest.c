/*
 * Host unit test for the USB network gateway (functions/ipGateway.c)
 *
 * Every frame the gateway sends is also written to ipGatewayTest.pcap, so that an independent dissector
 * (tshark, see the Makefile) can check the headers and checksums.
 */

#include <stdio.h>
#include <string.h>
#include "functions/ipGateway.h"

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static const uint8_t GW_MAC[6] = { 0x02, 0x47, 0x44, 0x37, 0x37, 0x01 };
static const uint8_t BCAST_MAC[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static const uint8_t HOST_MAC[6] = { 0x02, 0x47, 0x44, 0x37, 0x37, 0x02 };

#define RADIO_ID		10005
#define HOST_IP			(IPGW_INDIVIDUAL_NET | RADIO_ID)		// 12.0.39.21 with the default ranges

#define FITS_INDIVIDUAL(id)	(((id) & IPGW_MASK(IPGW_INDIVIDUAL_PREFIX)) == 0)

static uint32_t radioId = RADIO_ID;
#ifndef PCAP_NAME
#define PCAP_NAME		"ipGatewayTest.pcap"
#endif

static FILE *pcap;
static uint8_t lastFrame[2048];
static int lastLength;
static int framesSent;

static struct
{
	int calls;
	bool group;
	uint32_t dst;
	uint16_t srcPort;
	uint16_t dstPort;
	uint8_t payload[600];
	int length;
} air;

static void pcapWrite(const uint8_t *frame, int length)
{
	uint32_t record[4] = { 0, 0, (uint32_t)length, (uint32_t)length };
	fwrite(record, sizeof(record), 1, pcap);
	fwrite(frame, length, 1, pcap);
}

bool ipGatewaySendFrame(const uint8_t *frame, int length)
{
	memcpy(lastFrame, frame, length);
	lastLength = length;
	framesSent++;
	pcapWrite(frame, length);
	return true;
}

bool ipGatewayToAir(bool group, uint32_t dst, uint16_t srcPort, uint16_t dstPort, const uint8_t *payload, int length)
{
	air.calls++;
	air.group = group;
	air.dst = dst;
	air.srcPort = srcPort;
	air.dstPort = dstPort;
	memcpy(air.payload, payload, length);
	air.length = length;
	return true;
}

uint32_t ipGatewayRadioId(void)
{
	return radioId;
}

static void in(const uint8_t *frame, int length)
{
	framesSent = 0;
	ipGatewayEthernetIn(frame, length);
}

static uint16_t sum16(const uint8_t *p, int n, uint32_t s)
{
	for (; n > 1; p += 2, n -= 2) s += (p[0] << 8) | p[1];
	if (n) s += p[0] << 8;
	while (s >> 16) s = (s & 0xFFFF) + (s >> 16);
	return ~s;
}

// Builds Ethernet + IPv4 + UDP from the host, with correct checksums
static int hostUdp(uint8_t *f, const uint8_t *dstMac, uint32_t src, uint32_t dst, uint16_t sport, uint16_t dport, const uint8_t *payload, int n)
{
	memcpy(f, dstMac, 6); memcpy(f + 6, HOST_MAC, 6); f[12] = 0x08; f[13] = 0x00;
	uint8_t *ip = f + 14, *u = ip + 20;
	memset(ip, 0, 20);
	ip[0] = 0x45; ip[2] = (28 + n) >> 8; ip[3] = 28 + n; ip[6] = 0x40; ip[8] = 64; ip[9] = 17;
	for (int i = 0; i < 4; i++) { ip[12 + i] = src >> (24 - 8 * i); ip[16 + i] = dst >> (24 - 8 * i); }
	uint16_t c = sum16(ip, 20, 0); ip[10] = c >> 8; ip[11] = c;
	u[0] = sport >> 8; u[1] = sport; u[2] = dport >> 8; u[3] = dport; u[4] = (8 + n) >> 8; u[5] = 8 + n; u[6] = u[7] = 0;
	memcpy(u + 8, payload, n);
	uint32_t ps = (src >> 16) + (src & 0xFFFF) + (dst >> 16) + (dst & 0xFFFF) + 17 + 8 + n;
	c = sum16(u, 8 + n, ps); u[6] = c >> 8; u[7] = c;
	return 14 + 28 + n;
}

static void put32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

static uint32_t get32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

static const uint8_t *dhcpOption(const uint8_t *bootp, int len, uint8_t code)
{
	for (int i = 240; i < len && bootp[i] != 255;)
	{
		if (bootp[i] == 0) { i++; continue; }
		if (bootp[i] == code) return &bootp[i];
		i += 2 + bootp[i + 1];
	}
	return NULL;
}

static void testDHCP(uint8_t type, uint32_t requested, uint8_t expectedReply)
{
	uint8_t bootp[300] = { 1, 1, 6, 0, 0x12, 0x34, 0x56, 0x78 };
	uint8_t f[400];

	bootp[10] = 0x80;// broadcast flag
	memcpy(&bootp[28], HOST_MAC, 6);
	bootp[236] = 0x63; bootp[237] = 0x82; bootp[238] = 0x53; bootp[239] = 0x63;
	bootp[240] = 53; bootp[241] = 1; bootp[242] = type;
	bootp[243] = 50; bootp[244] = 4;
	for (int i = 0; i < 4; i++) bootp[245 + i] = requested >> (24 - 8 * i);
	bootp[249] = 255;

	in(f, hostUdp(f, BCAST_MAC, 0, 0xFFFFFFFF, 68, 67, bootp, sizeof(bootp)));
	CHECK(framesSent == 1);

	const uint8_t *r = &lastFrame[14 + 20 + 8];
	int rlen = lastLength - 42;
	CHECK(rlen >= 300);
	CHECK(r[0] == 2);
	CHECK(get32(&r[4]) == 0x12345678);
	CHECK(memcmp(&r[28], HOST_MAC, 6) == 0);
	const uint8_t *o = dhcpOption(r, rlen, 53);
	CHECK(o && o[2] == expectedReply);
	if (expectedReply == 6)
	{
		CHECK(get32(&r[16]) == 0);
		CHECK(dhcpOption(r, rlen, 1) == NULL);
		return;
	}
	CHECK(get32(&r[16]) == HOST_IP);
	o = dhcpOption(r, rlen, 1);
	CHECK(o && get32(&o[2]) == IPGW_NETMASK);
	o = dhcpOption(r, rlen, 121);// the multicast range only, the group range is on the link
	int octets = (IPGW_MULTICAST_PREFIX + 7) / 8;
	CHECK(o && o[1] == 1 + octets + 4 && o[2] == IPGW_MULTICAST_PREFIX && get32(&o[3]) >> (32 - 8 * octets) == IPGW_MULTICAST_NET >> (32 - 8 * octets));
	CHECK(o && get32(&o[3 + octets]) == IPGW_GATEWAY_IP);
	CHECK(dhcpOption(r, rlen, 3) == NULL);// no default route
}

static void testDHCPNoReply(void)
{
	uint8_t bootp[300] = { 1, 1, 6, 0, 0x12, 0x34, 0x56, 0x78 };
	uint8_t f[400];

	memcpy(&bootp[28], HOST_MAC, 6);
	bootp[236] = 0x63; bootp[237] = 0x82; bootp[238] = 0x53; bootp[239] = 0x63;
	bootp[240] = 53; bootp[241] = 1; bootp[242] = 1; bootp[243] = 255;
	in(f, hostUdp(f, BCAST_MAC, 0, 0xFFFFFFFF, 68, 67, bootp, sizeof(bootp)));
	CHECK(framesSent == 0);
}

static void testARP(void)
{
	uint8_t f[42] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	memcpy(f + 6, HOST_MAC, 6); f[12] = 0x08; f[13] = 0x06;
	uint8_t a[28] = { 0, 1, 8, 0, 6, 4, 0, 1 };
	memcpy(a + 8, HOST_MAC, 6);
	put32(&a[14], HOST_IP);
	put32(&a[24], IPGW_GROUP_NET | 9);// a talkgroup
	memcpy(f + 14, a, 28);

	in(f, 42);
	CHECK(framesSent == 1);
	CHECK(lastFrame[21] == 2);// reply
	CHECK(memcmp(&lastFrame[22], GW_MAC, 6) == 0);
	CHECK(get32(&lastFrame[28]) == (IPGW_GROUP_NET | 9));

	// Address conflict probe (sender 0.0.0.0) for the host's own address: no reply
	memset(f + 28, 0, 4);
	put32(&f[38], HOST_IP);
	in(f, 42);
	CHECK(framesSent == 0);
}

static void testPing(void)
{
	uint8_t f[100];
	memcpy(f, GW_MAC, 6); memcpy(f + 6, HOST_MAC, 6); f[12] = 0x08; f[13] = 0x00;
	uint8_t *ip = f + 14, *ic = ip + 20;
	memset(ip, 0, 20);
	ip[0] = 0x45; ip[3] = 20 + 16; ip[8] = 64; ip[9] = 1;
	put32(&ip[12], HOST_IP);
	put32(&ip[16], IPGW_GATEWAY_IP);
	uint16_t c = sum16(ip, 20, 0); ip[10] = c >> 8; ip[11] = c;
	memset(ic, 0, 16); ic[0] = 8; ic[5] = 1; ic[7] = 1; memcpy(ic + 8, "gd77ping", 8);
	c = sum16(ic, 16, 0); ic[2] = c >> 8; ic[3] = c;

	in(f, 14 + 36);
	CHECK(framesSent == 1);
	CHECK(lastFrame[14 + 9] == 1 && lastFrame[34] == 0);// echo reply
	CHECK(sum16(&lastFrame[34], 16, 0) == 0);
	CHECK(memcmp(&lastFrame[42], "gd77ping", 8) == 0);
}

static void testToAir(void)
{
	uint8_t f[200];
	const uint8_t lrrp[] = { 0x05, 0x08, 0x22, 0x04, 0x00, 0x00, 0x00, 0x01, 0x51, 0x40 };

	air.calls = 0;
	in(f, hostUdp(f, GW_MAC, HOST_IP, IPGW_INDIVIDUAL_NET | 235, 4001, 4001, lrrp, sizeof(lrrp)));// to 12.0.0.235
	CHECK(air.calls == 1 && !air.group && air.dst == 235 && air.srcPort == 4001 && air.dstPort == 4001);
	CHECK(air.length == sizeof(lrrp) && memcmp(air.payload, lrrp, sizeof(lrrp)) == 0);

	in(f, hostUdp(f, GW_MAC, HOST_IP, IPGW_GROUP_NET | 9, 5000, 4007, lrrp, 4));// to 13.0.0.9
	CHECK(air.calls == 2 && air.group && air.dst == 9 && air.srcPort == 5000 && air.dstPort == 4007);

	in(f, hostUdp(f, GW_MAC, HOST_IP, IPGW_MULTICAST_NET | 10, 5000, 4007, lrrp, 4));// to 225.0.0.10
	CHECK(air.calls == 3 && air.group && air.dst == 10);

	in(f, hostUdp(f, BCAST_MAC, HOST_IP, IPGW_BROADCAST_IP, 4005, 4005, lrrp, 4));// subnet broadcast: all call
	CHECK(air.calls == 4 && air.group && air.dst == 0xFFFFFF);

	in(f, hostUdp(f, BCAST_MAC, HOST_IP, IPGW_BROADCAST_IP, 137, 137, lrrp, 4));// NetBIOS broadcast: dropped
	in(f, hostUdp(f, BCAST_MAC, HOST_IP, 0xFFFFFFFF, 17500, 17500, lrrp, 4));// LAN sync broadcast: dropped
	in(f, hostUdp(f, GW_MAC, HOST_IP, 0x08080808, 53, 53, lrrp, 4));// elsewhere: dropped
	in(f, hostUdp(f, GW_MAC, HOST_IP, IPGW_GATEWAY_IP, 4001, 4001, lrrp, 4));// to the radio itself: dropped
	in(f, hostUdp(f, GW_MAC, IPGW_INDIVIDUAL_NET | 235, IPGW_INDIVIDUAL_NET | 236, 4001, 4001, lrrp, 4));// not from the host (forwarded): dropped
	CHECK(air.calls == 4 && framesSent == 0);

	// Corrupt IP header checksum: dropped
	int n = hostUdp(f, GW_MAC, HOST_IP, IPGW_INDIVIDUAL_NET | 235, 4001, 4001, lrrp, 4);
	f[24] ^= 0x01;
	in(f, n);
	CHECK(air.calls == 4);
}

static void testDeliver(void)
{
	const uint8_t report[] = { 0x0D, 0x0A, 0x22, 0x04, 0x00, 0x00, 0x00, 0x01, 0x66, 0x10, 0x23, 0x45 };

	CHECK(ipGatewayDeliverUDP(false, RADIO_ID, 235, 4001, 4001, report, sizeof(report)));// to us
	CHECK(memcmp(lastFrame, HOST_MAC, 6) == 0);
	CHECK(get32(&lastFrame[26]) == (IPGW_INDIVIDUAL_NET | 235));// from 12.0.0.235
	CHECK(get32(&lastFrame[30]) == HOST_IP);
	CHECK(sum16(&lastFrame[14], 20, 0) == 0);

	CHECK(ipGatewayDeliverUDP(false, 236, 235, 4001, 4001, report, sizeof(report)));// between two other radios
	CHECK(memcmp(lastFrame, HOST_MAC, 6) != 0 && memcmp(lastFrame, GW_MAC, 6) != 0 && !(lastFrame[0] & 1));
	CHECK(get32(&lastFrame[30]) == (IPGW_INDIVIDUAL_NET | 236));

	CHECK(ipGatewayDeliverUDP(true, 9, 235, 4007, 4007, report, sizeof(report)));
	const uint32_t mip = IPGW_MULTICAST_NET | 9;
	const uint8_t mcast[6] = { 0x01, 0x00, 0x5E, (mip >> 16) & 0x7F, (mip >> 8) & 0xFF, mip & 0xFF };
	CHECK(memcmp(lastFrame, mcast, 6) == 0);
	CHECK(get32(&lastFrame[30]) == mip);
	// IDs bigger than the range are truncated: in a /16, data to radio 0x010000 | RADIO_ID is also for the host
	CHECK(ipGatewayDeliverUDP(false, 0x010000 | RADIO_ID, 0x123456, 4001, 4001, report, sizeof(report)));
	CHECK((memcmp(lastFrame, HOST_MAC, 6) == 0) == !FITS_INDIVIDUAL(0x010000));// in a /8 it's another radio
	CHECK(get32(&lastFrame[26]) == (IPGW_INDIVIDUAL_NET | (0x123456 & ~IPGW_MASK(IPGW_INDIVIDUAL_PREFIX))));

	CHECK(ipGatewayDeliverUDP(true, 0xFFFFFF, 235, 4007, 4007, report, sizeof(report)));// all call
	CHECK(memcmp(lastFrame, BCAST_MAC, 6) == 0);
	CHECK(get32(&lastFrame[30]) == IPGW_BROADCAST_IP);

	const uint8_t record[] = { 1, 1, 1, 6, 0, 3, 0xBD, 0x00, 0x80 };
	CHECK(ipGatewayDeliverMonitor(record, sizeof(record)));
	CHECK(memcmp(lastFrame, BCAST_MAC, 6) == 0);
	CHECK(get32(&lastFrame[26]) == IPGW_GATEWAY_IP && get32(&lastFrame[30]) == IPGW_BROADCAST_IP);
	CHECK(lastFrame[36] == (IPGW_MONITOR_PORT >> 8) && lastFrame[37] == (IPGW_MONITOR_PORT & 0xFF));
}

static void testIdChange(void)
{
	if (!FITS_INDIVIDUAL(0x010000))
	{
		radioId = 0x010000 | RADIO_ID;// a high ID gets its truncated address
		testDHCP(1, 0, 2);
		radioId = 0x010000;// truncates to 0: no lease
		framesSent = 0;
		testDHCPNoReply();
	}

	radioId = 200;
	testDHCP(3, HOST_IP, 6);// renewing 12.0.39.21 is refused
	radioId = RADIO_ID;
}

int main(void)
{
	uint32_t header[6] = { 0xA1B2C3D4, 0x00040002, 0, 0, 65535, 1 };// pcap, Ethernet

	pcap = fopen(PCAP_NAME, "wb");
	fwrite(header, sizeof(header), 1, pcap);

	ipGatewayInit(GW_MAC, HOST_MAC);
	testDHCP(1, 0, 2);
	testDHCP(3, HOST_IP, 5);
	testARP();
	testPing();
	testToAir();
	testDeliver();
	testIdChange();

	fclose(pcap);
	printf("# %s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
	return failures ? 1 : 0;
}
