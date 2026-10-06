/*
 * DMR packet data (ETSI TS 102 361-1 clause 8 / 9) for OpenGD77
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _OPENGD77_DMRDATA_H_
#define _OPENGD77_DMRDATA_H_

#include <stdbool.h>
#include <stdint.h>

// This module has no hardware dependencies, so it can be unit tested on a host (see firmware/tests)

// Largest burst info field: Rate 1 data is 24 bytes, Rate 3/4 is 18, everything else is 12
#define DMR_BURST_PAYLOAD_MAX		24
#define DMR_DATA_MAX_BURSTS			40
#define DMR_DATA_MAX_PACKET			480			// 40 Rate 1/2 blocks

// Burst flags
#define DMR_BURST_FLAG_MCU_CRC		0x01		// TX: payload already holds its CRC / mask, the HR-C6000 must send it verbatim
#define DMR_BURST_FLAG_RX_CRC_ERROR	0x02		// RX: the HR-C6000 reported a CRC error for this burst

typedef struct
{
	uint8_t dataType;							// DT_xxx from dmrDefines.h
	uint8_t length;
	uint8_t flags;
	uint8_t payload[DMR_BURST_PAYLOAD_MAX];
} dmrBurst_t;

// Data packet formats (DPF) and service access points (SAP)
#define DMR_DPF_UDT					0x00
#define DMR_DPF_RESPONSE			0x01
#define DMR_DPF_UNCONFIRMED			0x02
#define DMR_DPF_CONFIRMED			0x03
#define DMR_DPF_DEFINED_SHORT		0x0D
#define DMR_DPF_RAW_SHORT			0x0E
#define DMR_DPF_PROPRIETARY			0x0F

#define DMR_SAP_UDT					0x00
#define DMR_SAP_IP					0x04
#define DMR_SAP_PROPRIETARY			0x09
#define DMR_SAP_SHORT_DATA			0x0A

#define DMR_CSBKO_PREAMBLE			0x3D

#define DMR_UDP_PORT_LRRP			4001
#define DMR_UDP_PORT_ARS			4005
#define DMR_UDP_PORT_TMS			4007

typedef struct
{
	uint8_t  dpf;
	uint8_t  sap;
	bool     group;
	bool     responseRequested;
	uint32_t dst;
	uint32_t src;
	uint8_t  blocksToFollow;
	uint8_t  padOctets;
	uint8_t  sendSeq;						// N(S), confirmed only
	uint8_t  dataType;						// DT of the blocks
	uint16_t length;						// user data length
	uint8_t  data[DMR_DATA_MAX_PACKET];
} dmrDataPacket_t;

typedef enum
{
	DMR_DATA_RX_NONE,							// burst consumed, nothing complete yet
	DMR_DATA_RX_PACKET,							// a complete packet is in dmrDataRxPacket
	DMR_DATA_RX_CSBK,							// a non preamble CSBK is in the burst passed in
	DMR_DATA_RX_ERROR							// header or packet CRC failed, reassembly reset
} dmrDataRxResult_t;

extern dmrDataPacket_t dmrDataRxPacket;

uint32_t dmrDataCRC32(const uint8_t *data, int length);

// TX builders. Each returns the number of bursts written to out, or 0 if the data does not fit
int dmrDataBuildCSBK(const uint8_t csbk[10], dmrBurst_t *out);
int dmrDataBuildPacket(uint8_t dpf, uint8_t sap, bool group, uint32_t dst, uint32_t src,
						const uint8_t *data, int length, int preambles, dmrBurst_t *out, int maxBursts);
int dmrDataBuildUDP(bool group, uint32_t dst, uint32_t src, uint16_t srcPort, uint16_t dstPort,
						const uint8_t *payload, int length, int preambles, dmrBurst_t *out, int maxBursts);
int dmrDataBuildTMS(bool group, uint32_t dst, uint32_t src, const char *text, uint8_t seq, bool ackRequested,
						int preambles, dmrBurst_t *out, int maxBursts);
int dmrDataBuildResponseAck(uint8_t sap, uint32_t dst, uint32_t src, uint8_t sendSeq, dmrBurst_t *out);
int dmrDataBuildTMSAck(uint32_t dst, uint32_t src, uint8_t seqByte, dmrBurst_t *out, int maxBursts);

// RX
void dmrDataRxReset(void);
dmrDataRxResult_t dmrDataRxBurst(const dmrBurst_t *burst);
typedef struct
{
	uint16_t srcPort;
	uint16_t dstPort;
	uint16_t appPort;					// the well known one of the two (LRRP, ARS, TMS), else the destination port
	const uint8_t *payload;
	int length;
} dmrDataUDP_t;

bool dmrDataGetUDP(const dmrDataPacket_t *packet, dmrDataUDP_t *udp);

// Decoded TMS text message, text is converted from UTF-16LE to Latin-1 ('?' for anything else)
typedef struct
{
	bool    isAck;
	bool    ackRequested;
	uint8_t seqByte;
	char    text[160];
} dmrDataTMS_t;

bool dmrDataDecodeTMS(const uint8_t *payload, int length, dmrDataTMS_t *tms);

#endif
