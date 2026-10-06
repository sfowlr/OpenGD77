/*
 * USB CDC-NCM (Network Control Model) function for the NXP KSDK device stack
 *
 * NTB16 only. Frames from the host are reassembled from 64 byte packets using the NTB block length,
 * so the transfer does not depend on the host sending a zero length packet. A complete NTB is handed to
 * the main task, and the OUT endpoint is only re-armed once the task has consumed it, which flow
 * controls the host. Frames to the host are sent one per NTB.
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
#include "usb_device_config.h"
#include "usb/usb_ncm.h"
#include "usb/usb_device_descriptor.h"
#include "functions/ipGateway.h"
#include "functions/ticks.h"

// Class specific requests (CDC NCM 1.0 table 6-2, CDC ECM 1.2 table 6)
#define NCM_SET_ETHERNET_PACKET_FILTER	0x43
#define NCM_GET_NTB_PARAMETERS			0x80
#define NCM_GET_NET_ADDRESS				0x81
#define NCM_GET_NTB_FORMAT				0x83
#define NCM_SET_NTB_FORMAT				0x84
#define NCM_GET_NTB_INPUT_SIZE			0x85
#define NCM_SET_NTB_INPUT_SIZE			0x86
#define NCM_GET_MAX_DATAGRAM_SIZE		0x87
#define NCM_SET_MAX_DATAGRAM_SIZE		0x88
#define NCM_GET_CRC_MODE				0x89
#define NCM_SET_CRC_MODE				0x8A

#define NCM_NOTIFY_NETWORK_CONNECTION	0x00
#define NCM_NOTIFY_SPEED_CHANGE			0x2A

#define NTB_OUT_SIZE					2048	// what we accept from the host (Linux needs at least one full Ethernet frame + headers)
#define NTB_IN_SIZE						(28 + IPGW_MAX_FRAME + 4)
#define NTH16_LENGTH					12
#define NDP16_LENGTH					16		// header + one datagram + the terminating null entry
#define NDP_MAX_HOPS					8		// NDPs in one NTB, against chains that loop (the datagrams in each are not limited)
#define REPORTED_SPEED					12000000UL

static const uint8_t NTB_PARAMETERS[28] = {
	28, 0,							// wLength
	0x01, 0x00,						// bmNtbFormatsSupported: NTB16
	0x00, 0x08, 0x00, 0x00,			// dwNtbInMaxSize 2048 (the hosts' minimum, our NTBs are much smaller)
	0x04, 0x00,						// wNdpInDivisor
	0x00, 0x00,						// wNdpInPayloadRemainder
	0x04, 0x00,						// wNdpInAlignment
	0x00, 0x00,						// reserved
	NTB_OUT_SIZE & 0xFF, NTB_OUT_SIZE >> 8, 0x00, 0x00,	// dwNtbOutMaxSize
	0x04, 0x00,						// wNdpOutDivisor
	0x00, 0x00,						// wNdpOutPayloadRemainder
	0x04, 0x00,						// wNdpOutAlignment
	0x00, 0x00						// wNtbOutMaxDatagrams: no limit
};

typedef struct
{
	usb_device_handle device;
	uint8_t configuration;
	uint8_t dataAlternate;
	volatile bool inBusy;
	volatile bool outReady;
	volatile uint16_t outLength;
	uint16_t parseNdp;				// main task NTB parsing cursor
	uint16_t parseEntry;
	uint8_t ndpHops;			// NDPs followed in this NTB, against chains that loop
	uint16_t inSequence;
	uint32_t ntbInputSize;
	volatile uint8_t notifyPending;	// notifications still to send after the link comes up
	volatile bool notifyBusy;		// a notification is on the interrupt endpoint
	volatile uint32_t notifySentMs;
} usbNcm_t;

#define NOTIFY_REPEAT_MS	1000	// until the host selects the data alternate setting

static usbNcm_t ncm;

USB_DMA_NONINIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE) static uint8_t ntbOut[NTB_OUT_SIZE];
USB_DMA_NONINIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE) static uint8_t ntbIn[NTB_IN_SIZE];
USB_DMA_NONINIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE) static uint8_t notification[16];
USB_DMA_NONINIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE) static uint8_t controlBuffer[32];

static uint16_t get16(const uint8_t *p)
{
	return p[0] | (p[1] << 8);
}

static void put16(uint8_t *p, uint16_t v)
{
	p[0] = v & 0xFF;
	p[1] = v >> 8;
}

static void put32(uint8_t *p, uint32_t v)
{
	put16(p, v & 0xFFFF);
	put16(&p[2], v >> 16);
}

bool usbNcmIsUp(void)
{
	return (ncm.configuration != 0) && (ncm.dataAlternate == 1);
}

uint8_t usbNcmGetDataAlternate(void)
{
	return ncm.dataAlternate;
}

static void armOut(void)
{
	USB_DeviceRecvRequest(ncm.device, USB_NCM_BULK_ENDPOINT, &ntbOut[ncm.outLength], FS_NCM_BULK_PACKET_SIZE);
}

static void sendNotification(uint8_t code)
{
	notification[0] = 0xA1;// class, interface, device to host
	notification[1] = code;
	put16(&notification[2], (code == NCM_NOTIFY_NETWORK_CONNECTION) ? 1 : 0);// connected
	put16(&notification[4], USB_NCM_COMM_INTERFACE_INDEX);
	put16(&notification[6], (code == NCM_NOTIFY_SPEED_CHANGE) ? 8 : 0);
	put32(&notification[8], REPORTED_SPEED);// downlink
	put32(&notification[12], REPORTED_SPEED);// uplink
	ncm.notifyBusy = true;
	ncm.notifySentMs = fw_millis();
	USB_DeviceSendRequest(ncm.device, USB_NCM_INTERRUPT_IN_ENDPOINT | (USB_IN << 7U), notification,
							(code == NCM_NOTIFY_SPEED_CHANGE) ? 16 : 8);
}

// ISR: the speed is announced first, then the connection
static usb_status_t interruptInCallback(usb_device_handle handle, usb_device_endpoint_callback_message_struct_t *message, void *param)
{
	ncm.notifyBusy = false;
	if (ncm.notifyPending && (ncm.configuration != 0))
	{
		ncm.notifyPending = 0;
		sendNotification(NCM_NOTIFY_NETWORK_CONNECTION);
	}
	return kStatus_USB_Success;
}

static usb_status_t bulkInCallback(usb_device_handle handle, usb_device_endpoint_callback_message_struct_t *message, void *param)
{
	ncm.inBusy = false;
	return kStatus_USB_Success;
}

// ISR: collect the packets of one NTB
static usb_status_t bulkOutCallback(usb_device_handle handle, usb_device_endpoint_callback_message_struct_t *message, void *param)
{
	if ((message->length == USB_UNINITIALIZED_VAL_32) || !usbNcmIsUp())
	{
		return kStatus_USB_Success;// cancelled
	}

	bool shortPacket = (message->length < FS_NCM_BULK_PACKET_SIZE);
	ncm.outLength += message->length;

	if (ncm.outLength >= NTH16_LENGTH)
	{
		uint16_t blockLength = get16(&ntbOut[8]);

		if ((memcmp(ntbOut, "NCMH", 4) != 0) || (blockLength < NTH16_LENGTH) || (blockLength > NTB_OUT_SIZE))
		{
			ncm.outLength = 0;// not the start of an NTB, wait for the next transfer
		}
		else if (ncm.outLength >= blockLength)
		{
			ncm.parseNdp = get16(&ntbOut[10]);
			ncm.ndpHops = 0;
			ncm.parseEntry = 8;
			ncm.outReady = true;// usbNcmTick re-arms the endpoint
			return kStatus_USB_Success;
		}
	}

	if (shortPacket || ((ncm.outLength + FS_NCM_BULK_PACKET_SIZE) > NTB_OUT_SIZE))
	{
		ncm.outLength = 0;// truncated NTB, or a zero length packet
	}

	armOut();
	return kStatus_USB_Success;
}

static void initEndpoint(uint8_t address, uint8_t type, uint16_t maxPacketSize, uint8_t interval, usb_device_endpoint_callback_t callback)
{
	usb_device_endpoint_init_struct_t epInit = { maxPacketSize, address, type, 0, interval };
	usb_device_endpoint_callback_struct_t epCallback = { callback, NULL, 0 };

	USB_DeviceInitEndpoint(ncm.device, &epInit, &epCallback);
}

static void setDataAlternate(uint8_t alternate)
{
	if (ncm.dataAlternate == 1)
	{
		USB_DeviceDeinitEndpoint(ncm.device, USB_NCM_BULK_ENDPOINT | (USB_IN << 7U));
		USB_DeviceDeinitEndpoint(ncm.device, USB_NCM_BULK_ENDPOINT | (USB_OUT << 7U));
	}

	ncm.dataAlternate = alternate;
	ncm.inBusy = false;
	ncm.outReady = false;
	ncm.outLength = 0;

	if (alternate == 1)
	{
		initEndpoint(USB_NCM_BULK_ENDPOINT | (USB_IN << 7U), USB_ENDPOINT_BULK, FS_NCM_BULK_PACKET_SIZE, 0, bulkInCallback);
		initEndpoint(USB_NCM_BULK_ENDPOINT | (USB_OUT << 7U), USB_ENDPOINT_BULK, FS_NCM_BULK_PACKET_SIZE, 0, bulkOutCallback);
		armOut();
		ncm.notifyPending = 1;
		sendNotification(NCM_NOTIFY_SPEED_CHANGE);
	}
}

static usb_status_t classRequest(usb_device_control_request_struct_t *request)
{
	uint16_t value = request->setup->wValue;

	switch (request->setup->bRequest)
	{
		case NCM_GET_NTB_PARAMETERS:
			memcpy(controlBuffer, NTB_PARAMETERS, sizeof(NTB_PARAMETERS));
			request->length = sizeof(NTB_PARAMETERS);
			break;
		case NCM_GET_NTB_FORMAT:
		case NCM_GET_CRC_MODE:
			put16(controlBuffer, 0);// NTB16, no CRC
			request->length = 2;
			break;
		case NCM_GET_NTB_INPUT_SIZE:
			put32(controlBuffer, ncm.ntbInputSize);
			request->length = 4;
			break;
		case NCM_GET_MAX_DATAGRAM_SIZE:
			put16(controlBuffer, 1514);
			request->length = 2;
			break;
		case NCM_GET_NET_ADDRESS:
			memset(controlBuffer, 0, 6);// the host uses the address in the iMACAddress string
			request->length = 6;
			break;
		case NCM_SET_NTB_INPUT_SIZE:
			if (!request->isSetup)
			{
				ncm.ntbInputSize = controlBuffer[0] | (controlBuffer[1] << 8) | (controlBuffer[2] << 16) | ((uint32_t)controlBuffer[3] << 24);
				return kStatus_USB_Success;
			}
			if (request->length > sizeof(controlBuffer))
			{
				return kStatus_USB_InvalidRequest;
			}
			break;
		case NCM_SET_NTB_FORMAT:
		case NCM_SET_CRC_MODE:
			return (value == 0) ? kStatus_USB_Success : kStatus_USB_InvalidRequest;
		case NCM_SET_ETHERNET_PACKET_FILTER:
			return kStatus_USB_Success;
		case NCM_SET_MAX_DATAGRAM_SIZE:
			if (request->isSetup && (request->length > sizeof(controlBuffer)))
			{
				return kStatus_USB_InvalidRequest;
			}
			if (request->isSetup)
			{
				break;
			}
			return kStatus_USB_Success;
		default:
			return kStatus_USB_InvalidRequest;
	}

	request->buffer = controlBuffer;
	return kStatus_USB_Success;
}

usb_status_t USB_DeviceNcmInit(uint8_t controllerId, usb_device_class_config_struct_t *config, class_handle_t *handle)
{
	memset(&ncm, 0, sizeof(ncm));
	ncm.ntbInputSize = 2048;

	usb_status_t status = USB_DeviceClassGetDeviceHandle(controllerId, &ncm.device);
	*handle = (class_handle_t)&ncm;
	return status;
}

usb_status_t USB_DeviceNcmDeinit(class_handle_t handle)
{
	return kStatus_USB_Success;
}

usb_status_t USB_DeviceNcmEvent(void *handle, uint32_t event, void *param)
{
	switch (event)
	{
		case kUSB_DeviceClassEventDeviceReset:
			ncm.configuration = 0;
			ncm.dataAlternate = 0;
			ncm.inBusy = false;
			ncm.outReady = false;
			ncm.outLength = 0;
			return kStatus_USB_Success;

		case kUSB_DeviceClassEventSetConfiguration:
			{
				uint8_t configuration = *((uint8_t *)param);

				if (configuration == ncm.configuration)
				{
					return kStatus_USB_Success;
				}
				if (ncm.configuration != 0)
				{
					setDataAlternate(0);
					USB_DeviceDeinitEndpoint(ncm.device, USB_NCM_INTERRUPT_IN_ENDPOINT | (USB_IN << 7U));
				}
				ncm.configuration = configuration;
				if (configuration != 0)
				{
					initEndpoint(USB_NCM_INTERRUPT_IN_ENDPOINT | (USB_IN << 7U), USB_ENDPOINT_INTERRUPT,
									FS_NCM_INTERRUPT_IN_PACKET_SIZE, FS_NCM_INTERRUPT_IN_INTERVAL, interruptInCallback);

					// macOS only selects the data alternate setting once the link is reported up, Linux and Windows
					// select it first. So the speed and the connection are announced now, and again for alternate 1.
					ncm.notifyPending = 1;
					sendNotification(NCM_NOTIFY_SPEED_CHANGE);
				}
			}
			return kStatus_USB_Success;

		case kUSB_DeviceClassEventSetInterface:
			{
				uint16_t interfaceAlternate = *((uint16_t *)param);

				if ((interfaceAlternate >> 8) != USB_NCM_DATA_INTERFACE_INDEX)
				{
					return kStatus_USB_Error;// not ours
				}
				if ((interfaceAlternate & 0xFF) > 1)
				{
					return kStatus_USB_InvalidRequest;
				}
				if ((interfaceAlternate & 0xFF) != ncm.dataAlternate)
				{
					setDataAlternate(interfaceAlternate & 0xFF);
				}
			}
			return kStatus_USB_Success;

		case kUSB_DeviceClassEventClassRequest:
			{
				usb_device_control_request_struct_t *request = (usb_device_control_request_struct_t *)param;

				if ((request->setup->wIndex & 0xFF) != USB_NCM_COMM_INTERFACE_INDEX)
				{
					return kStatus_USB_Error;// not ours
				}
				return classRequest(request);
			}

		default:
			return kStatus_USB_Error;
	}
}

// One datagram per NTB16: NTH16 at 0, NDP16 at 12, the frame at 28 (4 byte aligned)
bool usbNcmSendFrame(const uint8_t *frame, int length)
{
	if (!usbNcmIsUp() || ncm.inBusy || (length <= 0) || ((NTH16_LENGTH + NDP16_LENGTH + length + 1) > NTB_IN_SIZE))
	{
		return false;
	}

	ncm.inBusy = true;

	int total = NTH16_LENGTH + NDP16_LENGTH + length;
	if ((total % FS_NCM_BULK_PACKET_SIZE) == 0)
	{
		ntbIn[total++] = 0;// pad, so that the transfer ends with a short packet rather than needing a zero length one
	}

	memcpy(ntbIn, "NCMH", 4);
	put16(&ntbIn[4], NTH16_LENGTH);
	put16(&ntbIn[6], ncm.inSequence++);
	put16(&ntbIn[8], total);
	put16(&ntbIn[10], NTH16_LENGTH);
	memcpy(&ntbIn[12], "NCM0", 4);
	put16(&ntbIn[16], NDP16_LENGTH);
	put16(&ntbIn[18], 0);// no next NDP
	put16(&ntbIn[20], NTH16_LENGTH + NDP16_LENGTH);
	put16(&ntbIn[22], length);
	put16(&ntbIn[24], 0);
	put16(&ntbIn[26], 0);
	memcpy(&ntbIn[NTH16_LENGTH + NDP16_LENGTH], frame, length);

	if (USB_DeviceSendRequest(ncm.device, USB_NCM_BULK_ENDPOINT | (USB_IN << 7U), ntbIn, total) != kStatus_USB_Success)
	{
		ncm.inBusy = false;
		return false;
	}
	return true;
}

// Hands the datagrams of the received NTB to the gateway, one at a time while the IN endpoint is free
// (each one may need a reply), then re-arms the OUT endpoint
// The announcement at configuration can be lost (seen at power on with macOS on the STM32 radios, after which macOS
// waits for it for ever), so it is repeated until the host selects the data alternate setting. Linux and Windows
// select it first.
static void repeatNotification(void)
{
	if ((ncm.configuration == 0) || (ncm.dataAlternate != 0) || ((fw_millis() - ncm.notifySentMs) < NOTIFY_REPEAT_MS))
	{
		return;
	}

	NVIC_DisableIRQ(USB0_IRQn);
	if ((ncm.configuration != 0) && (ncm.dataAlternate == 0))
	{
		if (ncm.notifyBusy)
		{
			USB_DeviceCancel(ncm.device, USB_NCM_INTERRUPT_IN_ENDPOINT | (USB_IN << 7U));// never collected
		}
		ncm.notifyPending = 1;
		sendNotification(NCM_NOTIFY_SPEED_CHANGE);
	}
	NVIC_EnableIRQ(USB0_IRQn);
}

void usbNcmTick(void)
{
	repeatNotification();

	if (!ncm.outReady)
	{
		return;
	}

	uint16_t blockLength = get16(&ntbOut[8]);
	while (!ncm.inBusy && (ncm.parseNdp != 0) && (ncm.ndpHops < NDP_MAX_HOPS))
	{
		const uint8_t *ndp = &ntbOut[ncm.parseNdp];
		uint16_t ndpLength;

		if (((ncm.parseNdp + 8) > blockLength) || (memcmp(ndp, "NCM0", 4) != 0) ||
				((ndpLength = get16(&ndp[4])) < 16) || ((ncm.parseNdp + ndpLength) > blockLength))
		{
			break;
		}

		if ((ncm.parseEntry + 4) > ndpLength)
		{
			ncm.parseNdp = get16(&ndp[6]);// next NDP
			ncm.ndpHops++;
			ncm.parseEntry = 8;
			continue;
		}

		uint16_t index = get16(&ndp[ncm.parseEntry]);
		uint16_t length = get16(&ndp[ncm.parseEntry + 2]);
		ncm.parseEntry += 4;

		if ((index == 0) || (length == 0))
		{
			ncm.parseNdp = get16(&ndp[6]);// end of this NDP's datagrams
			ncm.ndpHops++;
			ncm.parseEntry = 8;
			continue;
		}

		if (((uint32_t)index + length) <= blockLength)
		{
			ipGatewayEthernetIn(&ntbOut[index], length);
		}
	}

	if (ncm.inBusy && (ncm.parseNdp != 0))
	{
		return;// carry on once the reply has gone
	}

	// Done with this NTB
	ncm.outLength = 0;
	ncm.outReady = false;
	if (usbNcmIsUp())
	{
		armOut();
	}
}

bool ipGatewaySendFrame(const uint8_t *frame, int length)
{
	return usbNcmSendFrame(frame, length);
}

// The MK22 composite device keeps its USB serial port, so the serial protocol isn't tunnelled over UDP here
void ipGatewaySerialIn(const uint8_t *data, int length)
{
}
