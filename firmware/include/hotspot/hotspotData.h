/*
 * Hotspot packet data from the host: MMDVM data frames collected into burst lists for the radio's data TX
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

#ifndef _OPENGD77_HOTSPOTDATA_H_
#define _OPENGD77_HOTSPOTDATA_H_

#include <stdbool.h>
#include <stdint.h>
#include "functions/dmrData.h"

// MMDVM NAK reasons
#define HOTSPOT_DATA_NAK_UNSUPPORTED	4U		// a burst type the radio can't send
#define HOTSPOT_DATA_NAK_FULL			5U		// no space left

// The bursts are collected in a list. A list goes out when it holds as many bursts as its data header (or else its
// preamble CSBKs or MBC) announced, or HOTSPOT_DATA_GAP_MS after its last frame. While it is on air the next list fills
// the rest of the array, so frames keep being taken (DMR_DATA_MAX_BURSTS in all).
#define HOTSPOT_DATA_GAP_MS			180U	// 3 slots without a frame
#define HOTSPOT_DATA_GIVE_UP_MS		2000U	// the channel never became free

// No hardware dependencies, so it can be unit tested on a host (see firmware/tests).
// storage: DMR_DATA_MAX_BURSTS bursts, e.g. the data service's, which isn't used in hotspot mode
void hotspotDataReset(dmrBurst_t *storage);

// A 33 byte frame with data sync from the host. 0 if it was taken, else the MMDVM NAK reason
uint8_t hotspotDataQueue(uint8_t dataType, const uint8_t *frame, uint32_t nowMs);

// Starts a list that is ready, when canStart (the radio is idle on receive), and frees the one that has gone out
void hotspotDataTick(uint32_t nowMs, bool canStart);

// Anything queued or on air, so the host must not retune
bool hotspotDataIsBusy(void);

// Bursts the host can still send
int hotspotDataSpace(void);

// Provided by the user of the module
bool hotspotDataTxStart(const dmrBurst_t *bursts, int count);	// false if the radio can't start now
bool hotspotDataTxDone(void);									// the transmission started is over, sent or failed

#endif
