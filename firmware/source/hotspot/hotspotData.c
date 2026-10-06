/*
 * Hotspot packet data from the host: MMDVM data frames collected into burst lists for the radio's data TX
 *
 * CSBKs, MBC, data headers and Rate 1/2 blocks are sent with their 96 info bits unchanged (taken from the frame by
 * BPTC decoding, so the host's CRCs are kept).
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

#include <string.h>
#include "hotspot/hotspotData.h"
#include "hotspot/dmrDataFrame.h"

static dmrBurst_t *bursts;			// the list on air first, then the list being filled
static int onAir = 0;				// bursts of the list on air, 0 if none
static struct
{
	int count;
	int expected;					// bursts this list should hold, from what it has so far
	bool hasHeader;					// a data header has set expected, the preambles no longer count
	uint32_t lastFrameMs;
} list;

static void clearList(void)
{
	list.count = 0;
	list.expected = 0;
	list.hasHeader = false;
}

void hotspotDataReset(dmrBurst_t *storage)
{
	bursts = storage;
	onAir = 0;
	clearList();
}

// Blocks announced by a data header (ETSI TS 102 361-1 9.3)
static int headerBlocks(const uint8_t *h)
{
	switch (h[0] & 0x0F)
	{
		case DMR_DPF_UDT:
			return (h[8] & 0x03) + 1;
		case DMR_DPF_DEFINED_SHORT:
		case DMR_DPF_RAW_SHORT:
			return (((h[0] >> 4) & 0x03) << 4) | (h[1] & 0x0F);
		default:// response, unconfirmed, confirmed
			return h[8] & 0x7F;
	}
}

uint8_t hotspotDataQueue(uint8_t dataType, const uint8_t *frame, uint32_t nowMs)
{
	if ((dataType < DT_CSBK) || (dataType > DT_RATE_12_DATA))
	{
		return HOTSPOT_DATA_NAK_UNSUPPORTED;// PI header, Idle, USBD; Rate 3/4 needs Trellis coding, Rate 1 a raw extraction
	}

	if ((onAir + list.count) >= DMR_DATA_MAX_BURSTS)
	{
		return HOTSPOT_DATA_NAK_FULL;
	}

	dmrBurst_t *burst = &bursts[onAir + list.count++];
	dmrDataFrameToBurst(dataType, frame, burst);
	const uint8_t *p = burst->payload;

	if (dataType == DT_DATA_HEADER)
	{
		// A proprietary header after a data header is one of the blocks it announced
		if (!(list.hasHeader && ((p[0] & 0x0F) == DMR_DPF_PROPRIETARY)))
		{
			list.expected = list.count + headerBlocks(p);
			list.hasHeader = true;
		}
	}
	else if ((dataType == DT_CSBK) && ((p[0] & 0x3F) == DMR_CSBKO_PREAMBLE) && !list.hasHeader)
	{
		list.expected = list.count + p[3];// blocks to follow
	}
	else if (((dataType == DT_MBC_HEADER) || (dataType == DT_MBC_CONTINUATION)) && ((p[0] & 0x80) == 0))
	{
		list.expected = list.count + 1;// not the last block
	}

	if (list.expected < list.count)
	{
		list.expected = list.count;// a lone CSBK, or more blocks than announced
	}

	list.lastFrameMs = nowMs;
	return 0;
}

void hotspotDataTick(uint32_t nowMs, bool canStart)
{
	if ((onAir > 0) && hotspotDataTxDone())
	{
		memmove(bursts, &bursts[onAir], list.count * sizeof(dmrBurst_t));// the next list to the front
		onAir = 0;
	}

	if ((onAir > 0) || (list.count == 0))
	{
		return;
	}

	uint32_t idle = nowMs - list.lastFrameMs;

	if ((list.count < list.expected) && (idle <= HOTSPOT_DATA_GAP_MS))
	{
		return;// more to come
	}

	if (canStart && hotspotDataTxStart(bursts, list.count))
	{
		onAir = list.count;
		clearList();
	}
	else if (idle > HOTSPOT_DATA_GIVE_UP_MS)
	{
		clearList();
	}
}

bool hotspotDataIsBusy(void)
{
	return (onAir > 0) || (list.count > 0);
}

int hotspotDataSpace(void)
{
	return DMR_DATA_MAX_BURSTS - onAir - list.count;
}
