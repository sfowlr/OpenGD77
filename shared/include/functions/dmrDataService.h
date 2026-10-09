/*
 * DMR data service for OpenGD77: SMS (Motorola TMS) inbox, ACKs and the host interface
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

#ifndef _OPENGD77_DMRDATASERVICE_H_
#define _OPENGD77_DMRDATASERVICE_H_

#include "functions/dmrData.h"

#define DMR_DATA_INBOX_SIZE			4
#define DMR_DATA_SMS_PREAMBLES		2		// preamble CSBKs before an SMS, gives receivers time to sync

typedef struct
{
	uint32_t src;
	uint32_t dst;
	uint32_t receivedAt;					// ticksGetMillis()
	bool     group;
	bool     hostPending;					// not popped by the USB host yet
	bool     unread;						// not shown on the radio yet
	char     text[sizeof(((dmrDataTMS_t *)0)->text)];
} dmrDataMessage_t;

// Requests are queued and started from the main task by dmrDataServiceTick()
bool dmrDataServiceSendSMS(bool group, uint32_t dst, const char *text, bool ackRequested);
bool dmrDataServiceSendUDP(bool group, uint32_t dst, uint16_t srcPort, uint16_t dstPort, const uint8_t *payload, int length);
bool dmrDataServiceSendBursts(const dmrBurst_t *bursts, int count);
bool dmrDataServiceIsBusy(void);
void dmrDataServiceTick(void);
// Power off (main task): true while the ARS deregistration is still going out, call again until it's false
bool dmrDataServicePowerOff(void);
void dmrDataServicePowerOffCancelled(void);

// Main task context, for each text message received (a weak no-op unless the platform's UI has one)
void dmrDataServiceMessageReceived(const dmrDataMessage_t *message);

// The inbox: the last DMR_DATA_INBOX_SIZE messages, shared by the USB host (which pops the oldest it hasn't had yet)
// and the radio's UI (which reads them all, newest first, and marks them read). A new message replaces the oldest
int dmrDataServiceInboxCount(void);// messages the USB host hasn't popped
bool dmrDataServiceInboxPop(dmrDataMessage_t *message);
int dmrDataServiceMessageCount(void);
dmrDataMessage_t *dmrDataServiceMessage(int index);// 0: the newest

// Raw received bursts kept for the host (USB 'D' commands)
// The TX burst list (DMR_DATA_MAX_BURSTS), lent to the hotspot: the service never transmits in hotspot mode
dmrBurst_t *dmrDataServiceTxBursts(void);

int dmrDataServiceRxBurstCount(void);
bool dmrDataServiceRxBurstPop(dmrBurst_t *burst);

// USB 'D' command handler, see usb_com.c
int dmrDataServiceHandleUSB(const uint8_t *request, uint8_t *reply);

#endif
