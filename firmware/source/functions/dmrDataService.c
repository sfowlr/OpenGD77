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

#include <string.h>
#include "functions/dmrDataService.h"
#include "hardware/HR-C6000.h"
#include "functions/trx.h"
#include "functions/settings.h"
#include "functions/sound.h"
#include "functions/ticks.h"
#include "functions/rxPowerSaving.h"
#include "user_interface/uiGlobals.h"
#include "usb/usb_com.h"

#define RX_BURST_QUEUE_SIZE		8
#define USB_BUFFER_SIZE			300
#define START_TIMEOUT_MS		5000

// USB 'D' sub commands
enum
{
	USB_DATA_CLEAR = 1,				// clear the byte buffer and the burst list
	USB_DATA_APPEND_BYTES,			// [len][bytes]
	USB_DATA_APPEND_BURST,			// [dataType][flags][len][payload]
	USB_DATA_SEND,					// [kind][flags][dst 3][port or dpf/sap 2]
	USB_DATA_STATUS,				// -> [tx status][rx bursts][inbox count]
	USB_DATA_POP_BURST,				// -> [1][dataType][flags][len][payload] or [0]
	USB_DATA_POP_MESSAGE			// -> [1][src 3][dst 3][group][len][text] or [0]
};

enum { USB_SEND_BURSTS = 0, USB_SEND_TMS, USB_SEND_UDP, USB_SEND_PACKET };

#define USB_SEND_FLAG_GROUP		0x01
#define USB_SEND_FLAG_ACK		0x02

static dmrBurst_t txBursts[DMR_DATA_MAX_BURSTS];
static int txBurstCount = 0;
static volatile bool txPending = false;
static uint32_t txPendingSince;

static uint8_t smsSeq = 0;
// Replies to a received message: the ETSI response for confirmed data and / or the TMS ACK, sent in one transmission
static volatile bool ackPending = false;
static uint32_t ackDst;
static bool ackResponse;
static uint8_t ackSap;
static uint8_t ackSendSeq;
static bool ackTMS;
static uint8_t ackSeqByte;
static volatile bool newMessage = false;

static dmrDataMessage_t inbox[DMR_DATA_INBOX_SIZE];
static volatile uint8_t inboxWriteIdx = 0;
static volatile uint8_t inboxReadIdx = 0;

static dmrBurst_t rxBursts[RX_BURST_QUEUE_SIZE];
static volatile uint8_t rxBurstWriteIdx = 0;
static volatile uint8_t rxBurstReadIdx = 0;

static uint8_t usbBuffer[USB_BUFFER_SIZE];
static int usbBufferLength = 0;

static bool canTransmit(void)
{
	return (settingsUsbMode != USB_MODE_HOTSPOT) && (trxGetMode() == RADIO_MODE_DIGITAL) &&
			((currentChannelData->flag4 & 0x04) == 0x00) &&
			((nonVolatileSettings.txFreqLimited == BAND_LIMITS_NONE) || trxCheckFrequencyInAmateurBand(currentChannelData->txFreq));
}

bool dmrDataServiceIsBusy(void)
{
	return txPending || (HRC6000DataTxGetStatus() == DMR_DATA_TX_RUNNING);
}

static bool queueTx(int count)
{
	if (count <= 0)
	{
		return false;
	}

	txBurstCount = count;
	txPendingSince = fw_millis();
	txPending = true;
	return true;
}

bool dmrDataServiceSendSMS(bool group, uint32_t dst, const char *text, bool ackRequested)
{
	if (dmrDataServiceIsBusy() || !canTransmit())
	{
		return false;
	}

	smsSeq = (smsSeq + 1) & 0x1F;
	return queueTx(dmrDataBuildTMS(group, dst, trxDMRID, text, smsSeq, ackRequested, DMR_DATA_SMS_PREAMBLES, txBursts, DMR_DATA_MAX_BURSTS));
}

bool dmrDataServiceSendUDP(bool group, uint32_t dst, uint16_t port, const uint8_t *payload, int length)
{
	if (dmrDataServiceIsBusy() || !canTransmit())
	{
		return false;
	}

	return queueTx(dmrDataBuildUDP(group, dst, trxDMRID, port, payload, length, DMR_DATA_SMS_PREAMBLES, txBursts, DMR_DATA_MAX_BURSTS));
}

bool dmrDataServiceSendBursts(const dmrBurst_t *bursts, int count)
{
	if (dmrDataServiceIsBusy() || !canTransmit() || (count > DMR_DATA_MAX_BURSTS))
	{
		return false;
	}

	memmove(txBursts, bursts, count * sizeof(dmrBurst_t));
	return queueTx(count);
}

void dmrDataServiceTick(void)
{
	if (ackPending && !dmrDataServiceIsBusy() && canTransmit())
	{
		int n = 0;

		ackPending = false;
		if (ackResponse)
		{
			n = dmrDataBuildResponseAck(ackSap, ackDst, trxDMRID, ackSendSeq, txBursts);
		}
		if (ackTMS)
		{
			n += dmrDataBuildTMSAck(ackDst, trxDMRID, ackSeqByte, &txBursts[n], DMR_DATA_MAX_BURSTS - n);
		}
		queueTx(n);
	}

	if (txPending)
	{
		rxPowerSavingSetState(ECOPHASE_POWERSAVE_INACTIVE);

		if (HRC6000DataTxStart(txBursts, txBurstCount))
		{
			txPending = false;
		}
		else if ((fw_millis() - txPendingSince) > START_TIMEOUT_MS)
		{
			txPending = false;
		}
	}

	if (newMessage)
	{
		newMessage = false;
		soundSetMelody(MELODY_PRIVATE_CALL);
	}
}

static bool isForUs(const dmrDataPacket_t *packet)
{
	if (packet->src == trxDMRID)
	{
		return false;// our own transmission, repeated back
	}

	if (!packet->group)
	{
		return (packet->dst == trxDMRID);
	}

	if (packet->dst == (trxTalkGroupOrPcId & 0x00FFFFFF))
	{
		return true;
	}

	for (int i = 0; i < currentRxGroupData.NOT_IN_CODEPLUG_numTGsInGroup; i++)
	{
		if (currentRxGroupData.NOT_IN_CODEPLUG_contactsTG[i] == packet->dst)
		{
			return true;
		}
	}

	return false;
}

static void handlePacket(const dmrDataPacket_t *packet)
{
	uint16_t port;
	const uint8_t *payload;
	int length;
	dmrDataTMS_t tms;

	if (!isForUs(packet))
	{
		return;
	}

	// Confirmed data to us must be acknowledged, whatever it carries, or the sender keeps retrying
	ackResponse = (packet->dpf == DMR_DPF_CONFIRMED) && packet->responseRequested && !packet->group;
	ackTMS = false;
	ackDst = packet->src;
	ackSap = packet->sap;
	ackSendSeq = packet->sendSeq;

	bool isTMS = dmrDataGetUDP(packet, &port, &payload, &length) && (port == DMR_UDP_PORT_TMS) &&
			dmrDataDecodeTMS(payload, length, &tms) && !tms.isAck;

	if (isTMS && tms.ackRequested && !packet->group)
	{
		ackTMS = true;
		ackSeqByte = tms.seqByte;
	}

	if (ackResponse || ackTMS)
	{
		ackPending = true;
	}

	if (!isTMS)
	{
		return;
	}

	uint8_t next = (inboxWriteIdx + 1) % DMR_DATA_INBOX_SIZE;
	if (next == inboxReadIdx)
	{
		inboxReadIdx = (inboxReadIdx + 1) % DMR_DATA_INBOX_SIZE;// drop the oldest
	}

	dmrDataMessage_t *message = &inbox[inboxWriteIdx];
	message->src = packet->src;
	message->dst = packet->dst;
	message->group = packet->group;
	memcpy(message->text, tms.text, sizeof(message->text));
	inboxWriteIdx = next;
	newMessage = true;
}

// HR-C6000 task context
void dmrDataServiceRxBurst(const dmrBurst_t *burst)
{
	uint8_t next = (rxBurstWriteIdx + 1) % RX_BURST_QUEUE_SIZE;

	if (next == rxBurstReadIdx)
	{
		rxBurstReadIdx = (rxBurstReadIdx + 1) % RX_BURST_QUEUE_SIZE;// drop the oldest
	}
	rxBursts[rxBurstWriteIdx] = *burst;
	rxBurstWriteIdx = next;

	// In hotspot mode MMDVMHost gets the bursts (see hotspotDataTick), the radio itself is not the recipient
	if ((settingsUsbMode != USB_MODE_HOTSPOT) && (dmrDataRxBurst(burst) == DMR_DATA_RX_PACKET))
	{
		handlePacket(&dmrDataRxPacket);
	}
}

int dmrDataServiceInboxCount(void)
{
	return (inboxWriteIdx + DMR_DATA_INBOX_SIZE - inboxReadIdx) % DMR_DATA_INBOX_SIZE;
}

bool dmrDataServiceInboxPop(dmrDataMessage_t *message)
{
	if (inboxReadIdx == inboxWriteIdx)
	{
		return false;
	}

	*message = inbox[inboxReadIdx];
	inboxReadIdx = (inboxReadIdx + 1) % DMR_DATA_INBOX_SIZE;
	return true;
}

int dmrDataServiceRxBurstCount(void)
{
	return (rxBurstWriteIdx + RX_BURST_QUEUE_SIZE - rxBurstReadIdx) % RX_BURST_QUEUE_SIZE;
}

bool dmrDataServiceRxBurstPop(dmrBurst_t *burst)
{
	if (rxBurstReadIdx == rxBurstWriteIdx)
	{
		return false;
	}

	*burst = rxBursts[rxBurstReadIdx];
	rxBurstReadIdx = (rxBurstReadIdx + 1) % RX_BURST_QUEUE_SIZE;
	return true;
}

static bool usbSend(const uint8_t *r)
{
	bool group = (r[1] & USB_SEND_FLAG_GROUP) != 0;
	uint32_t dst = (r[2] << 16) | (r[3] << 8) | r[4];
	uint16_t port = (r[5] << 8) | r[6];

	switch (r[0])
	{
		case USB_SEND_BURSTS:
			return dmrDataServiceSendBursts(txBursts, txBurstCount);
		case USB_SEND_TMS:
			usbBuffer[(usbBufferLength < USB_BUFFER_SIZE) ? usbBufferLength : (USB_BUFFER_SIZE - 1)] = 0;
			return dmrDataServiceSendSMS(group, dst, (const char *)usbBuffer, (r[1] & USB_SEND_FLAG_ACK) != 0);
		case USB_SEND_UDP:
			return dmrDataServiceSendUDP(group, dst, port, usbBuffer, usbBufferLength);
		case USB_SEND_PACKET:
			if (dmrDataServiceIsBusy() || !canTransmit())
			{
				return false;
			}
			return queueTx(dmrDataBuildPacket(port >> 8, port & 0x0F, group, dst, trxDMRID, usbBuffer, usbBufferLength,
									DMR_DATA_SMS_PREAMBLES, txBursts, DMR_DATA_MAX_BURSTS));
		default:
			return false;
	}
}

// request: 'D', sub command, arguments. Returns the reply length
int dmrDataServiceHandleUSB(const uint8_t *request, uint8_t *reply)
{
	const uint8_t *args = &request[2];
	int len = 3;

	reply[0] = 'D';
	reply[1] = request[1];
	reply[2] = 1;// result, 1 = OK

	switch (request[1])
	{
		case USB_DATA_CLEAR:
			if (dmrDataServiceIsBusy())
			{
				reply[2] = 0;
				break;
			}
			usbBufferLength = 0;
			txBurstCount = 0;
			break;

		case USB_DATA_APPEND_BYTES:
			if ((args[0] > (COM_REQUESTBUFFER_SIZE - 3)) || ((usbBufferLength + args[0]) > USB_BUFFER_SIZE))
			{
				reply[2] = 0;
				break;
			}
			memcpy(&usbBuffer[usbBufferLength], &args[1], args[0]);
			usbBufferLength += args[0];
			break;

		case USB_DATA_APPEND_BURST:
			if (dmrDataServiceIsBusy() || (txBurstCount >= DMR_DATA_MAX_BURSTS) || (args[2] > DMR_BURST_PAYLOAD_MAX))
			{
				reply[2] = 0;
				break;
			}
			txBursts[txBurstCount].dataType = args[0];
			txBursts[txBurstCount].flags = args[1];
			txBursts[txBurstCount].length = args[2];
			memcpy(txBursts[txBurstCount].payload, &args[3], args[2]);
			txBurstCount++;
			break;

		case USB_DATA_SEND:
			reply[2] = usbSend(args) ? 1 : 0;
			break;

		case USB_DATA_STATUS:
			reply[2] = txPending ? DMR_DATA_TX_RUNNING : HRC6000DataTxGetStatus();
			reply[3] = dmrDataServiceRxBurstCount();
			reply[4] = dmrDataServiceInboxCount();
			len = 5;
			break;

		case USB_DATA_POP_BURST:
			{
				dmrBurst_t burst;
				reply[2] = dmrDataServiceRxBurstPop(&burst) ? 1 : 0;
				if (reply[2])
				{
					reply[3] = burst.dataType;
					reply[4] = burst.flags;
					reply[5] = burst.length;
					memcpy(&reply[6], burst.payload, burst.length);
					len = 6 + burst.length;
				}
			}
			break;

		case USB_DATA_POP_MESSAGE:
			{
				dmrDataMessage_t message;
				reply[2] = dmrDataServiceInboxPop(&message) ? 1 : 0;
				if (reply[2])
				{
					int textLen = strlen(message.text);
					reply[3] = message.src >> 16; reply[4] = message.src >> 8; reply[5] = message.src;
					reply[6] = message.dst >> 16; reply[7] = message.dst >> 8; reply[8] = message.dst;
					reply[9] = message.group;
					reply[10] = textLen;
					memcpy(&reply[11], message.text, textLen);
					len = 11 + textLen;
				}
			}
			break;

		default:
			reply[2] = 0;
			break;
	}

	return len;
}
