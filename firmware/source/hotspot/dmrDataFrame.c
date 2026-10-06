/*
 * Conversion between DMR data bursts (info bits) and 33 byte DMR frames, as used by MMDVM
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

#include "hotspot/dmrDataFrame.h"
#include "hotspot/BPTC19696.h"
#include "hotspot/DMRSlotType.h"

// ETSI TS 102 361-1 9.1.1, the 48 sync bits sit between bit 4 of octet 13 and bit 3 of octet 19
static const uint8_t SYNC_MASK_BITS[7] = { 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0 };
static const uint8_t MS_DATA_SYNC[7] = { 0x0D, 0x5D, 0x7F, 0x77, 0xFD, 0x75, 0x70 };

// Only the BPTC(196,96) coded types for now. Rate 3/4 needs Trellis coding, Rate 1 a raw extraction
bool dmrDataFrameToBurst(uint8_t dataType, const uint8_t *frame, dmrBurst_t *burst)
{
	if ((dataType == DT_RATE_34_DATA) || (dataType == DT_RATE_1_DATA))
	{
		return false;
	}

	burst->dataType = dataType;
	burst->length = 12;
	burst->flags = DMR_BURST_FLAG_MCU_CRC;// the CRC and mask are already in the frame
	BPTC19696_init();
	BPTC19696_decode(frame, burst->payload);
	return true;
}

// MS sourced, as the hotspot is on the mobile side of the air interface
bool dmrDataBurstToFrame(const dmrBurst_t *burst, uint8_t colourCode, uint8_t *frame)
{
	if (burst->length != 12)
	{
		return false;
	}

	BPTC19696_init();
	BPTC19696_encode(burst->payload, frame);
	DMRSlotType_encode(colourCode, burst->dataType, frame);
	for (int i = 0; i < 7; i++)
	{
		frame[i + 13] = (frame[i + 13] & ~SYNC_MASK_BITS[i]) | MS_DATA_SYNC[i];
	}
	return true;
}
