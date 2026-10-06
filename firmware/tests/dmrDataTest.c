/*
 * Host unit test for the DMR packet data layer (functions/dmrData.c)
 *
 *   make -C firmware/tests
 *
 * Prints the generated bursts as "<dataType> <hex payload>" lines so that an external decoder
 * (e.g. check_with_radiodesk.py) can verify them independently.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "functions/dmrData.h"
#include "hotspot/dmrDefines.h"
#include "hotspot/CRC.h"
#include "hotspot/dmrDataFrame.h"

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static void dumpBursts(const char *name, const dmrBurst_t *bursts, int count)
{
	printf("# %s\n", name);
	for (int i = 0; i < count; i++)
	{
		printf("%d ", bursts[i].dataType);
		for (int j = 0; j < bursts[i].length; j++)
		{
			printf("%02x", bursts[i].payload[j]);
		}
		printf("\n");
	}
}

static dmrDataRxResult_t feed(const dmrBurst_t *bursts, int count)
{
	dmrDataRxResult_t result = DMR_DATA_RX_NONE;

	dmrDataRxReset();
	for (int i = 0; i < count; i++)
	{
		result = dmrDataRxBurst(&bursts[i]);
		if ((result == DMR_DATA_RX_PACKET) || (result == DMR_DATA_RX_ERROR))
		{
			CHECK(i == (count - 1));
			break;
		}
	}
	return result;
}

static void testCRC32(void)
{
	// Packet from the RadioDesk test vectors (fec.crc32_dmr): CRC of "123456789" style data is checked against
	// the python implementation in check_with_radiodesk.py, here only the self consistency is checked.
	uint8_t data[8] = { 0x45, 0x00, 0x00, 0x2A, 0x00, 0x01, 0x00, 0x00 };
	uint32_t a = dmrDataCRC32(data, 8);
	data[0] ^= 1;
	CHECK(a != dmrDataCRC32(data, 8));
}

static void testTMSRoundTrip(bool group, int preambles)
{
	dmrBurst_t bursts[DMR_DATA_MAX_BURSTS];
	const char *text = "Hello from OpenGD77, this is a longer message to use a few blocks";
	int n = dmrDataBuildTMS(group, 235, 3141592, text, 5, false, preambles, bursts, DMR_DATA_MAX_BURSTS);

	CHECK(n > (preambles + 1));
	CHECK(bursts[preambles].dataType == DT_DATA_HEADER);
	for (int i = 0; i < preambles; i++)
	{
		CHECK(bursts[i].dataType == DT_CSBK);
		CHECK(bursts[i].payload[3] == (n - 1 - i));
		CHECK(dmrDataRxBurst(&bursts[i]) == DMR_DATA_RX_NONE);// preambles are swallowed
	}

	CHECK(feed(bursts, n) == DMR_DATA_RX_PACKET);
	CHECK(dmrDataRxPacket.src == 3141592);
	CHECK(dmrDataRxPacket.dst == 235);
	CHECK(dmrDataRxPacket.group == group);
	CHECK(dmrDataRxPacket.dpf == DMR_DPF_UNCONFIRMED);
	CHECK(dmrDataRxPacket.sap == DMR_SAP_IP);

	uint16_t port;
	const uint8_t *payload;
	int length;
	CHECK(dmrDataGetUDP(&dmrDataRxPacket, &port, &payload, &length));
	CHECK(port == DMR_UDP_PORT_TMS);
	CHECK(length == (int)(6 + (2 * strlen(text))));

	dmrDataTMS_t tms;
	CHECK(dmrDataDecodeTMS(payload, length, &tms));
	CHECK(!tms.isAck);
	CHECK(tms.seqByte == (0x80 | 5));
	CHECK(strcmp(tms.text, text) == 0);

	// A corrupted block must fail the packet CRC
	bursts[n - 2].payload[3] ^= 0x10;
	CHECK(feed(bursts, n) == DMR_DATA_RX_ERROR);
	bursts[n - 2].payload[3] ^= 0x10;

	// A corrupted header must fail the header CRC
	bursts[preambles].payload[4] ^= 0x01;
	CHECK(dmrDataRxBurst(&bursts[preambles]) == DMR_DATA_RX_ERROR);
	bursts[preambles].payload[4] ^= 0x01;

	dumpBursts(group ? "tms-group" : "tms-private", bursts, n);
}

static void testPadding(void)
{
	// Every user data length from 1 to a full packet must round trip, which exercises all the pad octet counts
	dmrBurst_t bursts[DMR_DATA_MAX_BURSTS];
	uint8_t data[400];

	for (int i = 0; i < (int)sizeof(data); i++)
	{
		data[i] = rand();
	}

	for (int len = 1; len <= (int)sizeof(data); len++)
	{
		int n = dmrDataBuildPacket(DMR_DPF_UNCONFIRMED, 0x09, false, 1, 2, data, len, 0, bursts, DMR_DATA_MAX_BURSTS);

		if (n == 0)
		{
			CHECK(((len + 4 + 11) / 12) >= DMR_DATA_MAX_BURSTS);
			break;
		}
		CHECK(feed(bursts, n) == DMR_DATA_RX_PACKET);
		CHECK(dmrDataRxPacket.length == len);
		CHECK(memcmp(dmrDataRxPacket.data, data, len) == 0);
	}
}

static void testTMSAck(void)
{
	dmrBurst_t bursts[DMR_DATA_MAX_BURSTS];
	int n = dmrDataBuildTMSAck(3141592, 235, 0x85, bursts, DMR_DATA_MAX_BURSTS);

	CHECK(feed(bursts, n) == DMR_DATA_RX_PACKET);

	uint16_t port;
	const uint8_t *payload;
	int length;
	dmrDataTMS_t tms;
	CHECK(dmrDataGetUDP(&dmrDataRxPacket, &port, &payload, &length));
	CHECK(dmrDataDecodeTMS(payload, length, &tms));
	CHECK(tms.isAck);
	CHECK(tms.seqByte == 0x85);
	dumpBursts("tms-ack", bursts, n);
}

static void testRadioDeskVectors(void)
{
	// TMS samples from RadioDesk's tests (ok-dmrlib over the air capture)
	const uint8_t ahoj[] = { 0x00, 0x0D, 0xE0, 0x01, 0x01, 0x95, 0x44, 0x61, 0x00, 0x68, 0x00, 0x6F, 0x00, 0x6A, 0x00 };
	const uint8_t ack[] = { 0x00, 0x04, 0x9F, 0x00, 0x95, 0x20 };
	dmrDataTMS_t tms;

	CHECK(dmrDataDecodeTMS(ahoj, sizeof(ahoj), &tms));
	CHECK(!tms.isAck && tms.ackRequested);
	CHECK(strcmp(tms.text, "ahoj") == 0);
	CHECK(tms.seqByte == 0x95);

	CHECK(dmrDataDecodeTMS(ack, sizeof(ack), &tms));
	CHECK(tms.isAck);
	CHECK(tms.seqByte == 0x95);

	// Compressed UDP/IP header, Motorola TMS port id 98
	static dmrDataPacket_t p;
	const uint8_t compressed[] = { 0x00, 0x01, 0x00, 98, 98 };
	p.sap = 0x03;
	memcpy(p.data, compressed, sizeof(compressed));
	memcpy(&p.data[sizeof(compressed)], ahoj, sizeof(ahoj));
	p.length = sizeof(compressed) + sizeof(ahoj);

	uint16_t port;
	const uint8_t *payload;
	int length;
	CHECK(dmrDataGetUDP(&p, &port, &payload, &length));
	CHECK(port == DMR_UDP_PORT_TMS);
	CHECK(length == sizeof(ahoj));
}

static void testCSBK(void)
{
	dmrBurst_t burst;
	const uint8_t csbk[10] = { 0x9F, 0x00, 0x00, 0x00, 0x00, 0xEB, 0x00, 0x2F, 0xEF, 0xD8 };// Call alert to 235 from 3141592

	CHECK(dmrDataBuildCSBK(csbk, &burst) == 1);
	CHECK(dmrDataRxBurst(&burst) == DMR_DATA_RX_CSBK);
	burst.payload[2] ^= 1;
	CHECK(dmrDataRxBurst(&burst) == DMR_DATA_RX_ERROR);
	burst.payload[2] ^= 1;
	dumpBursts("csbk", &burst, 1);
}

// Confirmed Rate 1/2 packet as another radio would send it: 10 data bytes per block after the DBSN / CRC-9
static void testConfirmedRx(void)
{
	uint8_t user[50];
	uint8_t packed[60];
	dmrBurst_t bursts[8];
	int blocks = 6;// 6 * 10 = 60 = 50 user + 6 pad + 4 CRC
	int pad = 6;

	for (int i = 0; i < (int)sizeof(user); i++)
	{
		user[i] = 0x30 + i;
	}
	memcpy(packed, user, sizeof(user));
	memset(&packed[50], 0, pad);
	uint32_t crc = dmrDataCRC32(packed, 56);
	packed[56] = crc; packed[57] = crc >> 8; packed[58] = crc >> 16; packed[59] = crc >> 24;

	uint8_t h[12] = { 0x40 | DMR_DPF_CONFIRMED, (DMR_SAP_IP << 4) | pad, 0x00, 0x00, 235, 0x2F, 0xEF, 0xD8, 0x80 | blocks, (5 << 4) | 0x08, 0, 0 };
	CRC_addCCITT162(h, 12);
	h[10] ^= 0xCC; h[11] ^= 0xCC;
	bursts[0].dataType = DT_DATA_HEADER; bursts[0].length = 12; memcpy(bursts[0].payload, h, 12);
	for (int i = 0; i < blocks; i++)
	{
		bursts[1 + i].dataType = DT_RATE_12_DATA;
		bursts[1 + i].length = 12;
		bursts[1 + i].payload[0] = i << 1;// DBSN, CRC-9 not checked (the CRC-32 covers the data)
		bursts[1 + i].payload[1] = 0;
		memcpy(&bursts[1 + i].payload[2], &packed[i * 10], 10);
	}

	CHECK(feed(bursts, 1 + blocks) == DMR_DATA_RX_PACKET);
	CHECK(dmrDataRxPacket.dpf == DMR_DPF_CONFIRMED);
	CHECK(dmrDataRxPacket.responseRequested);
	CHECK(dmrDataRxPacket.sendSeq == 5);
	CHECK(dmrDataRxPacket.length == sizeof(user));
	CHECK(memcmp(dmrDataRxPacket.data, user, sizeof(user)) == 0);

	dmrBurst_t ack;
	CHECK(dmrDataBuildResponseAck(DMR_SAP_IP, 3141592, 235, 5, &ack) == 1);
	CHECK(feed(&ack, 1) == DMR_DATA_RX_PACKET);
	CHECK(dmrDataRxPacket.dpf == DMR_DPF_RESPONSE);
	dumpBursts("response-ack", &ack, 1);
}

// Bursts to 33 byte MMDVM frames and back, the frames are printed for the external decoder
static void testFrames(void)
{
	dmrBurst_t bursts[DMR_DATA_MAX_BURSTS];
	int n = dmrDataBuildTMS(false, 235, 3141592, "frame test", 1, true, 1, bursts, DMR_DATA_MAX_BURSTS);
	uint8_t frame[33];
	dmrBurst_t back;

	printf("# frames cc=7\n");
	for (int i = 0; i < n; i++)
	{
		CHECK(dmrDataBurstToFrame(&bursts[i], 7, frame));
		CHECK(dmrDataFrameToBurst(bursts[i].dataType, frame, &back));
		CHECK(memcmp(back.payload, bursts[i].payload, 12) == 0);
		printf("F %d ", bursts[i].dataType);
		for (int j = 0; j < 33; j++)
		{
			printf("%02x", frame[j]);
		}
		printf("\n");
	}
}

int main(void)
{
	testCRC32();
	testTMSRoundTrip(false, 0);
	testTMSRoundTrip(true, 3);
	testPadding();
	testTMSAck();
	testRadioDeskVectors();
	testCSBK();
	testConfirmedRx();
	testFrames();

	printf("# %s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
	return failures ? 1 : 0;
}
