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

#ifndef _OPENGD77_DMRDATAFRAME_H_
#define _OPENGD77_DMRDATAFRAME_H_

#include "functions/dmrData.h"

bool dmrDataFrameToBurst(uint8_t dataType, const uint8_t *frame, dmrBurst_t *burst);
bool dmrDataBurstToFrame(const dmrBurst_t *burst, uint8_t colourCode, uint8_t *frame);

#endif
