/*
 * DMR voice privacy (static keystream "basic privacy" schemes) for OpenGD77
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

#ifndef _OPENGD77_DMRPRIVACY_H_
#define _OPENGD77_DMRPRIVACY_H_

#include <stdbool.h>
#include <stdint.h>

/*
 * The keystream is XORed onto the 49 AMBE+2 parameter bits of each vocoder frame (ambe_d[] order, as in mbelib and
 * DSD-FME), between the FEC decode and the vocoder. A voice burst holds 3 frames, a superframe (bursts A-F) 18, and the
 * keystream position restarts at each burst A. The burst is handled in its on-air form (27 bytes, 3 x 72 FEC coded bits),
 * so it is applied where the burst sequence is known: when the burst is handed to the HR-C6000 for TX, and before the
 * vocoder on RX.
 */

typedef enum
{
	DMR_PRIVACY_OFF = 0,
	DMR_PRIVACY_MOTOROLA_BP,  // Motorola Basic Privacy, key 1-255
	DMR_PRIVACY_ANYTONE_BP,   // Anytone 16-bit Basic Privacy, key 0x0000-0xFFFF
	DMR_PRIVACY_NUM_TYPES
} dmrPrivacyType_t;

#define DMR_PRIVACY_FID                 0x10 // FID sent in the voice LC of a private call (Motorola)
#define DMR_PRIVACY_SERVICE_OPTION      0x40 // Privacy bit of the Service Options (ETSI TS 102 361-2 7.2.1)

#define DMR_PRIVACY_AMBE_FRAME_BITS     49
#define DMR_PRIVACY_AMBE_FRAME_BYTES    9    // 72 FEC coded bits
#define DMR_PRIVACY_AMBE_BURST_FRAMES   3

bool dmrPrivacyKeyIsValid(dmrPrivacyType_t type, uint16_t key);
uint16_t dmrPrivacyKeyMax(dmrPrivacyType_t type);
const char *dmrPrivacyTypeName(dmrPrivacyType_t type);

// The Service Options byte of a voice LC with the privacy bit set when a scheme is in use
uint8_t dmrPrivacyServiceOptions(dmrPrivacyType_t type, uint8_t serviceOptions);
uint8_t dmrPrivacyFID(dmrPrivacyType_t type, uint8_t fid);
// Whether a received voice LC marks the call as private
bool dmrPrivacyLCIsPrivate(const uint8_t *lc);

// Encrypt (TX) or decrypt (RX) the 3 frames of a voice burst in place. voiceSequence is the burst, 0 (A) to 5 (F).
// correctErrors runs the frames through the FEC decoder first (RX); TX frames come straight from the encoder.
void dmrPrivacyApplyBurst(dmrPrivacyType_t type, uint16_t key, uint8_t *ambe, uint8_t voiceSequence, bool correctErrors);

// The AMBE+2 3600x2450 FEC of one vocoder frame: 49 parameter bits (one per byte) <-> 9 interleaved bytes on air
void dmrPrivacyAmbeEncode(const uint8_t *bits, uint8_t *frame);
void dmrPrivacyAmbeExtract(const uint8_t *frame, uint8_t *bits); // no error correction

// Provided by the codec (dmr_codec/codec.c): de-interleave and FEC decode with error correction
void initFrame(uint8_t *indata, uint16_t bitbufferDecode[49]);

#endif
