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

#include <stdio.h>
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
#include "usb/usb_ncm.h"
#include "functions/ipGateway.h"
#include "functions/codeplug.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiUtilities.h"

#define MILLIS()				ticksGetMillis()
#define CHANNEL_IS_RX_ONLY()	(codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_RX_ONLY) != 0)

#define RX_BURST_QUEUE_SIZE		8
#define USB_BUFFER_SIZE			300
#define START_TIMEOUT_MS		10000	// a transmission still waiting for a clear channel after this is dropped
#define REPLY_TIMEOUT_MS		3000	// the same for an ACK or CSBK answer: later than this the sender has given up
#define REPLY_HOLDOFF_MS		180		// through a repeater: our slot quiet this long means the sender has finished
#define LBT_CLEAR_MS			120		// direct mode: nothing heard for this long before transmitting (listen before talk). A
												// radio sends a burst every 60 ms, so this is at least one frame with none
#define CALL_ALERT_REPEAT_MS	10000	// the caller retries until it hears the ack: ring once
#define CALL_ALERT_SHOW_MS		10000
// Motorola ARS: registration with the presence server (the "ARS radio ID"), as MOTOTRBO radios do it: 5-15 s after power
// on (spread by radio ID, so that radios switched on together don't collide), and again once the channel, our ID or the
// server has been unchanged for ARS_SETTLE_MS. A registration the server doesn't acknowledge is retried ARS_RETRIES times,
// ARS_RETRY_FIRST_MS apart and doubling (30 s to 8 min), then left until something changes: with no server on the
// channel, MOTOTRBO radios retry for ever
#define ARS_POWER_ON_MIN_MS		5000
#define ARS_POWER_ON_SPREAD_MS	10000
#define ARS_SETTLE_MS			5000
#define ARS_RETRY_FIRST_MS		30000
#define ARS_RETRIES				5
#if defined(HAS_SOFT_VOLUME)
#define CALL_ALERT_LOUD_GAIN	16		// HR-C6000 line out gain while the alert rings with BIT_CALL_ALERT_LOUD (knob: -31..31)
#endif

// USB 'D' sub commands
enum
{
	USB_DATA_CLEAR = 1,				// clear the byte buffer and the burst list
	USB_DATA_APPEND_BYTES,			// [len][bytes]
	USB_DATA_APPEND_BURST,			// [dataType][flags][len][payload]
	USB_DATA_SEND,					// [kind][flags][dst 3][port or dpf/sap 2]
	USB_DATA_STATUS,				// -> [tx status][rx bursts][inbox count]
	USB_DATA_POP_BURST,				// -> [1][dataType][flags][len][payload] or [0]
	USB_DATA_POP_MESSAGE,			// -> [1][src 3][dst 3][group][len][text] or [0]
	USB_DATA_NETWORK_MODE			// [0 off / 1 on / 0xFF query] -> [result][mode saved for the next boot][network link up]
};

enum { USB_SEND_BURSTS = 0, USB_SEND_TMS, USB_SEND_UDP, USB_SEND_PACKET };

#define USB_SEND_FLAG_GROUP		0x01
#define USB_SEND_FLAG_ACK		0x02
#define USB_SEND_RATE_SHIFT		2		// flag bits 2-3: the block rate of kinds 1-3, 0 Rate 1/2, 1 Rate 3/4, 2 Rate 1

DMR_DATA_BUFFER static dmrBurst_t txBursts[DMR_DATA_MAX_BURSTS];
static int txBurstCount = 0;
static volatile bool txPending = false;
static uint32_t txPendingSince;
static bool txIsReply;

static uint8_t smsSeq = 0;
// Replies to a received message: the ETSI response for confirmed data, then the TMS ACK in a transmission of its own,
// as MOTOTRBO radios send it (a repeater or hotspot takes one transmission for one data call)
static volatile bool ackPending = false;
static uint32_t ackDst;
static bool ackResponse;
static uint8_t ackSap;
static uint8_t ackSendSeq;
static bool ackTMS;
static uint8_t ackSeq;
static bool tmsAckPending = false;
static uint32_t tmsAckDst;
static uint8_t tmsAckSeq;
static volatile bool newMessage = false;

// Call alert ack or radio check answer, sent on its own
static volatile bool csbkAnswerPending = false;
static dmrBurst_t csbkAnswer;
static volatile uint32_t lastRxBurstTime;
static uint32_t channelBusyTime;
static volatile bool callAlertPending = false;
static uint32_t callAlertSrc;
static uint32_t lastCallAlertSrc = 0;
static uint32_t lastCallAlertTime;
#if defined(HAS_SOFT_VOLUME)
static bool callAlertGainRaised = false;
#endif

// Ring twice, like a phone
static const int16_t MELODY_CALL_ALERT[] = {
		1000, 50, 1300, 50, 1000, 50, 1300, 50, 1000, 50, 1300, 50, 1000, 50, 1300, 50, 1000, 50, 1300, 50, 0, 400,
		1000, 50, 1300, 50, 1000, 50, 1300, 50, 1000, 50, 1300, 50, 1000, 50, 1300, 50, 1000, 50, 1300, 50,
		-1, -1 };

#if defined(HAS_DMR_ARS)
static struct
{
	uint32_t seen;							// what to register (arsContext()) as last seen, and since when
	uint32_t seenSince;
	uint32_t settleMs;						// how long it must stay unchanged: the power on delay at first
	uint32_t context;						// what was registered, or is being: 0 nothing
	uint32_t server;						// the server registered with, deregistered when ARS is switched off
	uint32_t due;							// the next attempt
	uint8_t attempts;
	bool registered;						// the server acknowledged
	bool deregister;						// a deregistration to the old server is waiting to go
	uint8_t powerOff;						// ARS_POWER_OFF_xxx: deregistering because the radio is being switched off
} ars;
enum { ARS_POWER_OFF_NONE = 0, ARS_POWER_OFF_SENDING, ARS_POWER_OFF_DONE };
static volatile bool arsAckReceived = false;
static volatile bool arsQueryReceived = false;
#endif

DMR_DATA_BUFFER static dmrDataMessage_t inbox[DMR_DATA_INBOX_SIZE];
static volatile uint8_t inboxNewest = 0;
static volatile uint8_t inboxUsed = 0;

static dmrBurst_t rxBursts[RX_BURST_QUEUE_SIZE];
static volatile uint8_t rxBurstWriteIdx = 0;
static volatile uint8_t rxBurstReadIdx = 0;

// An IP packet received over the air (ICMP, UDP or SCTP, from layer 4 on), waiting for the main task to give it to the
// USB network gateway
static volatile bool airToHostPending = false;
DMR_DATA_BUFFER static struct
{
	bool group;
	uint32_t dst;
	uint32_t src;
	uint8_t protocol;
	int length;
	uint8_t l4[DMR_DATA_MAX_PACKET];
} airToHost;

// Every data burst received, for the USB network host (see IPGW_MONITOR_PORT)
#define MONITOR_RECORD_HEADER	6
static uint8_t monitorRecords[RX_BURST_QUEUE_SIZE][MONITOR_RECORD_HEADER + DMR_BURST_PAYLOAD_MAX];
static volatile uint8_t monitorWriteIdx = 0;
static volatile uint8_t monitorReadIdx = 0;

DMR_DATA_BUFFER static uint8_t usbBuffer[USB_BUFFER_SIZE];
static int usbBufferLength = 0;

static bool canTransmit(void)
{
	return (settingsUsbMode != USB_MODE_HOTSPOT) && (trxGetMode() == RADIO_MODE_DIGITAL) &&
			!CHANNEL_IS_RX_ONLY() &&
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
	txPendingSince = MILLIS();
	channelBusyTime = txPendingSince;// listen for LBT_CLEAR_MS at least
	txIsReply = false;
	txPending = true;
	return true;
}

// ACKs and CSBK answers: sent in the repeater's hang time, and only worth sending for REPLY_TIMEOUT_MS
static void queueReply(int count)
{
	if (queueTx(count))
	{
		txIsReply = true;
	}
}

static bool sendSMS(bool group, uint32_t dst, const char *text, bool ackRequested, uint8_t blockType)
{
	if (dmrDataServiceIsBusy() || !canTransmit())
	{
		return false;
	}

	smsSeq = (smsSeq + 1) & 0x1F;
	return queueTx(dmrDataBuildTMS(group, dst, trxDMRID, text, smsSeq, ackRequested, blockType, DMR_DATA_SMS_PREAMBLES, txBursts, DMR_DATA_MAX_BURSTS));
}

static bool sendUDP(bool group, uint32_t dst, uint16_t srcPort, uint16_t dstPort, const uint8_t *payload, int length, uint8_t blockType)
{
	if (dmrDataServiceIsBusy() || !canTransmit())
	{
		return false;
	}

	return queueTx(dmrDataBuildUDP(group, dst, trxDMRID, srcPort, dstPort, payload, length, blockType, DMR_DATA_SMS_PREAMBLES, txBursts, DMR_DATA_MAX_BURSTS));
}

bool dmrDataServiceSendSMS(bool group, uint32_t dst, const char *text, bool ackRequested)
{
	return sendSMS(group, dst, text, ackRequested, DT_RATE_12_DATA);
}

bool dmrDataServiceSendUDP(bool group, uint32_t dst, uint16_t srcPort, uint16_t dstPort, const uint8_t *payload, int length)
{
	return sendUDP(group, dst, srcPort, dstPort, payload, length, DT_RATE_12_DATA);
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

static void showCallAlert(uint32_t src)
{
	char name[MAX_DMR_ID_CONTACT_TEXT_LENGTH];
	char message[NOTIFICATION_MESSAGE_LEN_MAX];

	if (!contactIDLookup(src, CONTACT_CALLTYPE_PC, name))
	{
		dmrIdDataStruct_t record;

		dmrIDLookup(src, &record);// "ID:n" when it isn't in the database
		snprintf(name, sizeof(name), "%s", record.text);
	}
	snprintf(message, sizeof(message), "Call alert\n%s", name);

	displayLightTrigger(true);
	uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, CALL_ALERT_SHOW_MS, message, true);

#if defined(HAS_SOFT_VOLUME)
	// The volume knob only sets the HR-C6000 line out gain, which the beeps go through as well
	if (settingsIsOptionBitSet(BIT_CALL_ALERT_LOUD) && (lastVolume < CALL_ALERT_LOUD_GAIN))
	{
		HRC6000SetDmrRxGain(CALL_ALERT_LOUD_GAIN);
		callAlertGainRaised = true;
	}
#endif
	soundSetMelody(MELODY_CALL_ALERT);
}

// Listen before talk, for everything the service transmits. Direct mode: no voice call (its terminator clears the ID),
// and for LBT_CLEAR_MS no data burst and no carrier, decodable or not. The HR-C6000 slot state isn't used: without a
// terminator (data, CSBKs) it only goes idle on the voice fade timeout, 200-400 ms after the last burst. A repeater's
// outbound carrier stays up through its hang time, so there: our slot has had no data bursts for REPLY_HOLDOFF_MS, and
// unless this is a reply (expected in the hang time), no call is going on on our slot. The caller's ID is only cleared
// when the repeater stops, so new traffic waits for the hang time to end
static bool channelIsClear(bool isReply)
{
#if defined(STM32F405xx)
	if (currentRadioDevice->trxDMRModeRx == DMR_MODE_RMO)
#else
	if (trxDMRModeRx == DMR_MODE_RMO)
#endif
	{
		return ((MILLIS() - lastRxBurstTime) > REPLY_HOLDOFF_MS) && (isReply || (HRC6000GetReceivedSrcId() == 0));
	}

	if ((HRC6000GetReceivedSrcId() != 0) || ((MILLIS() - lastRxBurstTime) < LBT_CLEAR_MS) ||
#if defined(STM32F405xx)
			trxCarrierDetected(RADIO_DEVICE_PRIMARY))
#else
			trxCarrierDetected())
#endif
	{
		channelBusyTime = MILLIS();
		return false;
	}

	return ((MILLIS() - channelBusyTime) > LBT_CLEAR_MS);
}

#if defined(HAS_DMR_ARS)
// The ARS radio ID, 0 when ARS is off. Saved settings only: while an options menu is open its changes aren't, and
// leaving it with Red puts the saved ones back (originalNonVolatileSettings holds them until then)
static uint32_t arsServer(void)
{
	settingsStruct_t *saved = (originalNonVolatileSettings.magicNumber != 0xDEADBEEF) ? &originalNonVolatileSettings : &nonVolatileSettings;

	if (!settingsIsOptionBitSetFromSettings(saved, BIT_DMR_ARS))
	{
		return 0;
	}

	return (saved->dmrArsId[0] << 16) | (saved->dmrArsId[1] << 8) | saved->dmrArsId[2];
}

// What a registration is for: the server, our ID and the channel (frequency, timeslot, colour code). 0 when there is
// nothing to register (ARS off, or a channel we can't send data on)
static uint32_t arsContext(uint32_t server)
{
	if ((server == 0) || !canTransmit())
	{
		return 0;
	}

	uint32_t h = 2166136261u;// FNV-1a
	uint32_t values[] = { server, trxDMRID, currentChannelData->txFreq, trxGetDMRTimeSlot(), trxGetDMRColourCode() };

	for (unsigned int i = 0; i < (sizeof(values) / sizeof(values[0])); i++)
	{
		h = (h ^ values[i]) * 16777619u;
	}

	return (h != 0) ? h : 1;
}

static bool arsSend(uint32_t server, bool registration)
{
	uint8_t pdu[DMR_ARS_MAX_PDU];
	int length = registration ? dmrDataBuildARSRegistration(trxDMRID, pdu) : dmrDataBuildARSDeregistration(pdu);

	return queueTx(dmrDataBuildUDP(false, server, trxDMRID, DMR_UDP_PORT_ARS, DMR_UDP_PORT_ARS, pdu, length,
									DT_RATE_12_DATA, DMR_DATA_SMS_PREAMBLES, txBursts, DMR_DATA_MAX_BURSTS));
}

static void arsTick(void)
{
	if (ars.powerOff != ARS_POWER_OFF_NONE)
	{
		return;
	}

	uint32_t now = MILLIS();
	uint32_t server = arsServer();
	uint32_t context = arsContext(server);

	if (ars.settleMs == 0)
	{
		ars.settleMs = ARS_POWER_ON_MIN_MS + (((trxDMRID * 2654435761u) ^ now) % ARS_POWER_ON_SPREAD_MS);
		ars.seen = context;
		ars.seenSince = now;
	}

	if (arsAckReceived)
	{
		arsAckReceived = false;
		ars.registered = (ars.context != 0);
	}

	if (arsQueryReceived)
	{
		arsQueryReceived = false;// the server asks us to register again
		ars.registered = false;
		ars.attempts = 0;
		ars.due = now;
	}

	if (context != ars.seen)
	{
		ars.seen = context;
		ars.seenSince = now;
	}

	if ((now - ars.seenSince) >= ars.settleMs)
	{
		ars.settleMs = ARS_SETTLE_MS;

		if (context != ars.context)
		{
			// Switched off, or to another server: say goodbye to the one we registered with (or tried to: a server
			// that doesn't acknowledge may still be listening). A channel we can't send on (analog, RX only, hotspot)
			// isn't a reason to, the registration still holds when we come back
			if ((ars.context != 0) && (server != ars.server) && (ars.registered || (ars.attempts > 0)))
			{
				ars.deregister = true;
			}

			if ((context != 0) || (server != ars.server))
			{
				ars.context = context;
				ars.registered = false;
				ars.attempts = 0;
				ars.due = now;
			}
		}
	}

	if (dmrDataServiceIsBusy() || !canTransmit())
	{
		return;
	}

	if (ars.deregister)
	{
		ars.deregister = false;
		arsSend(ars.server, false);
		return;
	}

	if ((ars.context != 0) && (ars.context == context) && !ars.registered && (ars.attempts <= ARS_RETRIES) &&
			((int32_t)(now - ars.due) >= 0) && arsSend(server, true))
	{
		ars.server = server;
		ars.due = now + (ARS_RETRY_FIRST_MS << ars.attempts);
		ars.attempts++;
	}
}
#endif

// The radio is being switched off (the power off screen, main task): deregister from the ARS server if registered, or
// tried to. True while the deregistration still has to go out; the caller gives up on it after a while
bool dmrDataServicePowerOff(void)
{
#if defined(HAS_DMR_ARS)
	if (ars.powerOff == ARS_POWER_OFF_NONE)
	{
		if (!(ars.deregister || ((ars.context != 0) && (ars.registered || (ars.attempts > 0)))) || !canTransmit())
		{
			ars.powerOff = ARS_POWER_OFF_DONE;
			return false;
		}

		if (dmrDataServiceIsBusy())
		{
			return true;// after what's already on its way
		}

		ars.deregister = false;
		ars.powerOff = arsSend(ars.server, false) ? ARS_POWER_OFF_SENDING : ARS_POWER_OFF_DONE;
	}

	return (ars.powerOff == ARS_POWER_OFF_SENDING) && dmrDataServiceIsBusy();
#else
	return false;
#endif
}

// Switched back on before the power off finished: register again if we deregistered
void dmrDataServicePowerOffCancelled(void)
{
#if defined(HAS_DMR_ARS)
	if (ars.powerOff == ARS_POWER_OFF_SENDING)
	{
		ars.context = 0;
	}
	ars.powerOff = ARS_POWER_OFF_NONE;
#endif
}

void dmrDataServiceTick(void)
{
	if (csbkAnswerPending && !dmrDataServiceIsBusy() && canTransmit())
	{
		txBursts[0] = csbkAnswer;
		csbkAnswerPending = false;
		queueReply(1);
	}

	if (callAlertPending)
	{
		callAlertPending = false;
		if ((callAlertSrc != lastCallAlertSrc) || ((MILLIS() - lastCallAlertTime) > CALL_ALERT_REPEAT_MS))
		{
			showCallAlert(callAlertSrc);
		}
		lastCallAlertSrc = callAlertSrc;
		lastCallAlertTime = MILLIS();
	}

#if defined(HAS_SOFT_VOLUME)
	if (callAlertGainRaised && !soundMelodyIsPlaying())
	{
		callAlertGainRaised = false;
		HRC6000SetDmrRxGain(lastVolume);
	}
#endif

	if (ackPending && !dmrDataServiceIsBusy() && canTransmit())
	{
		ackPending = false;
		if (ackTMS)
		{
			tmsAckDst = ackDst;
			tmsAckSeq = ackSeq;
			tmsAckPending = true;
		}
		if (ackResponse)
		{
			queueReply(dmrDataBuildResponseAck(ackSap, ackDst, trxDMRID, ackSendSeq, txBursts));
		}
	}

	// After the response has gone (the service is busy until then)
	if (tmsAckPending && !dmrDataServiceIsBusy() && canTransmit())
	{
		tmsAckPending = false;
		queueReply(dmrDataBuildTMSAck(tmsAckDst, trxDMRID, tmsAckSeq, DMR_DATA_SMS_PREAMBLES, txBursts, DMR_DATA_MAX_BURSTS));
	}

#if defined(HAS_DMR_ARS)
	arsTick();
#endif

	if (txPending)
	{
		rxPowerSavingSetState(ECOPHASE_POWERSAVE_INACTIVE);

		if (channelIsClear(txIsReply) && HRC6000DataTxStart(txBursts, txBurstCount))
		{
			txPending = false;
		}
		else if ((MILLIS() - txPendingSince) > (txIsReply ? REPLY_TIMEOUT_MS : START_TIMEOUT_MS))
		{
			txPending = false;
		}
	}

	if (newMessage)
	{
		newMessage = false;
		soundSetMelody(MELODY_PRIVATE_CALL);
		dmrDataServiceMessageReceived(dmrDataServiceMessage(0));
	}

	while (monitorReadIdx != monitorWriteIdx)
	{
		uint8_t *record = monitorRecords[monitorReadIdx];

		if (usbNcmIsUp() && !ipGatewayDeliverMonitor(record, MONITOR_RECORD_HEADER + record[5]))
		{
			break;// USB IN busy, next tick
		}
		monitorReadIdx = (monitorReadIdx + 1) % RX_BURST_QUEUE_SIZE;
	}

	if (airToHostPending)
	{
		// Retried on the next tick if the USB IN endpoint is still busy, dropped if the link went down
		if (!usbNcmIsUp() || ipGatewayDeliverIP(airToHost.group, airToHost.dst, airToHost.src, airToHost.protocol,
													airToHost.l4, airToHost.length))
		{
			airToHostPending = false;
		}
	}
}

// The radio's own DMR ID from the codeplug, not trxDMRID: per channel IDs, manual overrides and the hotspot (which uses
// the caller's ID) change that, and the host's address must stay put
uint32_t ipGatewayRadioId(void)
{
	return (uiDataGlobal.userDMRId != 0) ? uiDataGlobal.userDMRId : trxDMRID;
}

// USB network gateway: ICMP, UDP or SCTP from the host to a radio ID or talkgroup
bool ipGatewayIPToAir(bool group, uint32_t dst, uint8_t protocol, const uint8_t *l4, int length)
{
	if (dmrDataServiceIsBusy() || !canTransmit())
	{
		return false;
	}

	return queueTx(dmrDataBuildIP(group, dst, trxDMRID, protocol, l4, length, DT_RATE_12_DATA, DMR_DATA_SMS_PREAMBLES,
									txBursts, DMR_DATA_MAX_BURSTS));
}

// USB network gateway: the payload of a datagram to IPGW_RAW_PORT, as the user data of a short data packet
bool ipGatewayRawToAir(bool group, uint32_t dst, const uint8_t *data, int length)
{
	if (dmrDataServiceIsBusy() || !canTransmit())
	{
		return false;
	}

	return queueTx(dmrDataBuildPacket(DMR_DPF_UNCONFIRMED, DMR_SAP_SHORT_DATA, group, dst, trxDMRID, data, length,
										DT_RATE_12_DATA, DMR_DATA_SMS_PREAMBLES, txBursts, DMR_DATA_MAX_BURSTS));
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
	dmrDataUDP_t udp;
	dmrDataTMS_t tms;
	bool isUDP = dmrDataGetUDP(packet, &udp);

	// ICMP, UDP and SCTP go to the USB network host, also between other radios so that a capture shows them. UDP is
	// rebuilt from its ports and payload (it may have come with a compressed header, SAP 3). Data that isn't IP based
	// goes as the payload of a datagram to IPGW_RAW_PORT
	if ((packet->src != trxDMRID) && usbNcmIsUp() && !airToHostPending)
	{
		dmrDataIP_t ip;

		if (isUDP && ((8 + udp.length) <= (int)sizeof(airToHost.l4)))
		{
			airToHost.protocol = DMR_IP_PROTO_UDP;
			airToHost.l4[0] = udp.srcPort >> 8;
			airToHost.l4[1] = udp.srcPort & 0xFF;
			airToHost.l4[2] = udp.dstPort >> 8;
			airToHost.l4[3] = udp.dstPort & 0xFF;
			memset(&airToHost.l4[4], 0, 4);// length and checksum, worked out by the gateway
			memcpy(&airToHost.l4[8], udp.payload, udp.length);
			airToHost.length = 8 + udp.length;
			airToHostPending = true;
		}
		else if (!isUDP && dmrDataGetIP(packet, &ip) && ((ip.protocol == DMR_IP_PROTO_ICMP) || (ip.protocol == DMR_IP_PROTO_SCTP)) &&
					(ip.length > 0) && (ip.length <= (int)sizeof(airToHost.l4)))
		{
			airToHost.protocol = ip.protocol;
			memcpy(airToHost.l4, ip.payload, ip.length);
			airToHost.length = ip.length;
			airToHostPending = true;
		}
		else if (((packet->dpf == DMR_DPF_UNCONFIRMED) || (packet->dpf == DMR_DPF_CONFIRMED)) &&
					(packet->sap != DMR_SAP_IP) && (packet->sap != DMR_SAP_UDPIP_COMPRESSION) &&
					(packet->length > 0) && ((8 + packet->length) <= (int)sizeof(airToHost.l4)))
		{
			airToHost.protocol = DMR_IP_PROTO_UDP;
			airToHost.l4[0] = IPGW_RAW_PORT >> 8;
			airToHost.l4[1] = IPGW_RAW_PORT & 0xFF;
			airToHost.l4[2] = IPGW_RAW_PORT >> 8;
			airToHost.l4[3] = IPGW_RAW_PORT & 0xFF;
			memset(&airToHost.l4[4], 0, 4);
			memcpy(&airToHost.l4[8], packet->data, packet->length);
			airToHost.length = 8 + packet->length;
			airToHostPending = true;
		}

		if (airToHostPending)
		{
			airToHost.group = packet->group;
			airToHost.dst = packet->dst;
			airToHost.src = packet->src;
		}
	}

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

#if defined(HAS_DMR_ARS)
	// The presence server's answer to our registration, or its request for one
	if (isUDP && (udp.appPort == DMR_UDP_PORT_ARS) && !packet->group && (packet->src == ars.server))
	{
		switch (dmrDataDecodeARS(udp.payload, udp.length))
		{
			case DMR_ARS_RESPONSE:
				arsAckReceived = true;
				break;
			case DMR_ARS_QUERY:
				arsQueryReceived = true;
				break;
			default:
				break;
		}
	}
#endif

	bool isTMS = isUDP && (udp.appPort == DMR_UDP_PORT_TMS) && dmrDataDecodeTMS(udp.payload, udp.length, &tms) && !tms.isAck;

	if (isTMS && tms.ackRequested && !packet->group)
	{
		ackTMS = true;
		ackSeq = tms.seq;
	}

	if (ackResponse || ackTMS)
	{
		ackPending = true;
	}

	if (!isTMS)
	{
		return;
	}

	uint8_t next = (inboxUsed == 0) ? 0 : ((inboxNewest + 1) % DMR_DATA_INBOX_SIZE);// replaces the oldest when full
	dmrDataMessage_t *message = &inbox[next];
	message->src = packet->src;
	message->dst = packet->dst;
	message->group = packet->group;
	message->receivedAt = MILLIS();
	message->hostPending = true;
	message->unread = true;
	memcpy(message->text, tms.text, sizeof(message->text));
	inboxNewest = next;
	if (inboxUsed < DMR_DATA_INBOX_SIZE)
	{
		inboxUsed++;
	}
	newMessage = true;
}

// Main task context: a text message to us has arrived. The platform's UI shows it (see uiMessages.c)
__attribute__((weak)) void dmrDataServiceMessageReceived(const dmrDataMessage_t *message)
{
	(void)message;
}

// Call alert and radio check to us: answered, and a call alert rings
static void handleCSBK(const uint8_t *csbk)
{
	if (csbkAnswerPending || (dmrDataBuildCSBKAnswer(csbk, trxDMRID, &csbkAnswer) == 0))
	{
		return;
	}

	csbkAnswerPending = true;

	if (((csbk[0] & 0x3F) == DMR_CSBKO_CALL_ALERT) && !callAlertPending)
	{
		callAlertSrc = (csbk[7] << 16) | (csbk[8] << 8) | csbk[9];
		callAlertPending = true;
	}
}

// HR-C6000 task context
void dmrDataServiceRxBurst(const dmrBurst_t *burst)
{
	lastRxBurstTime = MILLIS();// through a repeater, holds our transmissions back until the sender has finished

	uint8_t next = (rxBurstWriteIdx + 1) % RX_BURST_QUEUE_SIZE;

	if (next == rxBurstReadIdx)
	{
		rxBurstReadIdx = (rxBurstReadIdx + 1) % RX_BURST_QUEUE_SIZE;// drop the oldest
	}
	rxBursts[rxBurstWriteIdx] = *burst;
	rxBurstWriteIdx = next;

	next = (monitorWriteIdx + 1) % RX_BURST_QUEUE_SIZE;
	if (usbNcmIsUp() && (next != monitorReadIdx))
	{
		uint8_t *record = monitorRecords[monitorWriteIdx];

		record[0] = 1;// version
		record[1] = trxGetDMRTimeSlot() + 1;
		record[2] = trxGetDMRColourCode();
		record[3] = burst->dataType;
		record[4] = burst->flags;
		record[5] = burst->length;
		memcpy(&record[MONITOR_RECORD_HEADER], burst->payload, burst->length);
		monitorWriteIdx = next;
	}

	// In hotspot mode MMDVMHost gets the bursts (see hotspotDataTick), the radio itself is not the recipient
	if (settingsUsbMode != USB_MODE_HOTSPOT)
	{
		switch (dmrDataRxBurst(burst))
		{
			case DMR_DATA_RX_PACKET:
				handlePacket(&dmrDataRxPacket);
				break;
			case DMR_DATA_RX_CSBK:
				handleCSBK(burst->payload);
				break;
			default:
				break;
		}
	}
}

int dmrDataServiceMessageCount(void)
{
	return inboxUsed;
}

dmrDataMessage_t *dmrDataServiceMessage(int index)
{
	if ((index < 0) || (index >= inboxUsed))
	{
		return NULL;
	}

	return &inbox[(inboxNewest + DMR_DATA_INBOX_SIZE - index) % DMR_DATA_INBOX_SIZE];
}

int dmrDataServiceInboxCount(void)
{
	int count = 0;

	for (int i = 0; i < inboxUsed; i++)
	{
		if (dmrDataServiceMessage(i)->hostPending)
		{
			count++;
		}
	}

	return count;
}

// The oldest message the host hasn't had
bool dmrDataServiceInboxPop(dmrDataMessage_t *message)
{
	for (int i = inboxUsed - 1; i >= 0; i--)
	{
		dmrDataMessage_t *m = dmrDataServiceMessage(i);

		if (m->hostPending)
		{
			m->hostPending = false;
			*message = *m;
			return true;
		}
	}

	return false;
}

dmrBurst_t *dmrDataServiceTxBursts(void)
{
	return txBursts;
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
	static const uint8_t RATES[4] = { DT_RATE_12_DATA, DT_RATE_34_DATA, DT_RATE_1_DATA, 0 };
	uint8_t blockType = RATES[(r[1] >> USB_SEND_RATE_SHIFT) & 0x03];

	if ((r[0] != USB_SEND_BURSTS) && (blockType == 0))
	{
		return false;
	}

	switch (r[0])
	{
		case USB_SEND_BURSTS:
			return dmrDataServiceSendBursts(txBursts, txBurstCount);
		case USB_SEND_TMS:
			usbBuffer[(usbBufferLength < USB_BUFFER_SIZE) ? usbBufferLength : (USB_BUFFER_SIZE - 1)] = 0;
			return sendSMS(group, dst, (const char *)usbBuffer, (r[1] & USB_SEND_FLAG_ACK) != 0, blockType);
		case USB_SEND_UDP:
			return sendUDP(group, dst, port, port, usbBuffer, usbBufferLength, blockType);
		case USB_SEND_PACKET:
			if (dmrDataServiceIsBusy() || !canTransmit())
			{
				return false;
			}
			return queueTx(dmrDataBuildPacket(port >> 8, port & 0x0F, group, dst, trxDMRID, usbBuffer, usbBufferLength,
									blockType, DMR_DATA_SMS_PREAMBLES, txBursts, DMR_DATA_MAX_BURSTS));
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

		case USB_DATA_NETWORK_MODE:
			if (args[0] != 0xFF)
			{
				settingsSetOptionBit(BIT_USB_NETWORK, (args[0] != 0));
				settingsSetDirty();// saved with the other settings, e.g. by the CPS save and reboot command
			}
			reply[3] = settingsIsOptionBitSet(BIT_USB_NETWORK) ? 1 : 0;
			reply[4] = usbNcmIsUp() ? 1 : 0;
			len = 5;
			break;

		default:
			reply[2] = 0;
			break;
	}

	return len;
}
