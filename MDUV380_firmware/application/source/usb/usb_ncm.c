/*
 * USB CDC-NCM (Network Control Model) network adapter for the ST USB device library
 *
 * The STM32F405 OTG_FS core has 3 IN endpoints besides EP0, too few for CDC-ACM + CDC-NCM together, so the radio is
 * either a serial port or this network adapter (BIT_USB_NETWORK). In network mode the serial protocol (MMDVMHost,
 * CPS style commands) is tunnelled over UDP by the gateway, see ipGateway.h.
 *
 * NTB16 only. Frames from the host are reassembled from 64 byte packets using the NTB block length, so the transfer
 * does not depend on the host sending a zero length packet. A complete NTB is handed to the main task, and the OUT
 * endpoint is only re-armed once the task has consumed it, which flow controls the host. Frames to the host are sent
 * one per NTB.
 *
 * Interfaces: 0 communication (notifications on EP 0x82), 1 data (alternate 1: bulk 0x81 / 0x01), grouped by an IAD
 * so that Windows' composite driver keeps them together. Windows 10 gets the WINNCM compatible ID from Microsoft OS
 * 1.0 descriptors (string 0xEE, vendor request MS_OS_VENDOR_CODE); Windows 11, Linux and macOS bind by class.
 */

#include <string.h>
#include "usbd_ctlreq.h"
#include "usbd_ioreq.h"
#include "usb/usb_ncm.h"
#include "functions/ipGateway.h"

#define NCM_COMM_INTERFACE				0
#define NCM_DATA_INTERFACE				1
#define NCM_NOTIFY_EP					0x82
#define NCM_IN_EP						0x81
#define NCM_OUT_EP						0x01
#define NCM_BULK_PACKET_SIZE			64
#define NCM_NOTIFY_PACKET_SIZE			16
#define NCM_NOTIFY_INTERVAL				16		// ms
#define NCM_MAC_STRING_INDEX			6
#define MS_OS_STRING_INDEX				0xEE
#define MS_OS_VENDOR_CODE				0x47
#define MS_OS_EXTENDED_COMPAT_ID		0x0004

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
#define REPORTED_SPEED					12000000UL

#define CONFIG_DESCRIPTOR_LENGTH		94

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

__ALIGN_BEGIN static uint8_t configDescriptor[CONFIG_DESCRIPTOR_LENGTH] __ALIGN_END = {
	// Configuration: 2 interfaces, self powered
	0x09, USB_DESC_TYPE_CONFIGURATION, CONFIG_DESCRIPTOR_LENGTH, 0x00, 0x02, 0x01, 0x00, 0xC0, 0x32,
	// Interface association: the two NCM interfaces are one function
	0x08, 0x0B, NCM_COMM_INTERFACE, 0x02, 0x02, 0x0D, 0x00, 0x00,
	// Communication interface: CDC, NCM, no protocol
	0x09, USB_DESC_TYPE_INTERFACE, NCM_COMM_INTERFACE, 0x00, 0x01, 0x02, 0x0D, 0x00, 0x00,
	// CDC header, version 1.10
	0x05, 0x24, 0x00, 0x10, 0x01,
	// Union: communication interface, then the data interface
	0x05, 0x24, 0x06, NCM_COMM_INTERFACE, NCM_DATA_INTERFACE,
	// Ethernet networking: iMACAddress, no statistics, wMaxSegmentSize 1514, no multicast filters, no power filters
	0x0D, 0x24, 0x0F, NCM_MAC_STRING_INDEX, 0x00, 0x00, 0x00, 0x00, 0xEA, 0x05, 0x00, 0x00, 0x00,
	// NCM version 1.00, no optional requests
	0x06, 0x24, 0x1A, 0x00, 0x01, 0x00,
	// Notification endpoint
	0x07, USB_DESC_TYPE_ENDPOINT, NCM_NOTIFY_EP, USBD_EP_TYPE_INTR, NCM_NOTIFY_PACKET_SIZE, 0x00, NCM_NOTIFY_INTERVAL,
	// Data interface, alternate 0: no endpoints (the link is down)
	0x09, USB_DESC_TYPE_INTERFACE, NCM_DATA_INTERFACE, 0x00, 0x00, 0x0A, 0x00, 0x01, 0x00,
	// Data interface, alternate 1: NTB transfers
	0x09, USB_DESC_TYPE_INTERFACE, NCM_DATA_INTERFACE, 0x01, 0x02, 0x0A, 0x00, 0x01, 0x00,
	0x07, USB_DESC_TYPE_ENDPOINT, NCM_IN_EP, USBD_EP_TYPE_BULK, NCM_BULK_PACKET_SIZE, 0x00, 0x00,
	0x07, USB_DESC_TYPE_ENDPOINT, NCM_OUT_EP, USBD_EP_TYPE_BULK, NCM_BULK_PACKET_SIZE, 0x00, 0x00
};

// "MSFT100" and the vendor code that Windows uses to ask for the compatible IDs
__ALIGN_BEGIN static const uint8_t msOsString[18] __ALIGN_END = {
	18, USB_DESC_TYPE_STRING, 'M', 0, 'S', 0, 'F', 0, 'T', 0, '1', 0, '0', 0, '0', 0, MS_OS_VENDOR_CODE, 0
};

// Extended compat ID: function 0 (the NCM interfaces) is WINNCM
__ALIGN_BEGIN static const uint8_t msOsCompatId[40] __ALIGN_END = {
	40, 0, 0, 0, 0x00, 0x01, MS_OS_EXTENDED_COMPAT_ID & 0xFF, MS_OS_EXTENDED_COMPAT_ID >> 8, 1, 0, 0, 0, 0, 0, 0, 0,
	NCM_COMM_INTERFACE, 0x01, 'W', 'I', 'N', 'N', 'C', 'M', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

__ALIGN_BEGIN static uint8_t macString[2 + (12 * 2)] __ALIGN_END;

typedef struct
{
	volatile bool configured;
	volatile uint8_t dataAlternate;
	volatile bool inBusy;
	volatile bool outReady;
	volatile uint16_t outLength;
	uint16_t parseNdp;				// main task NTB parsing cursor
	uint16_t parseEntry;
	uint16_t inSequence;
	uint32_t ntbInputSize;
	uint8_t pendingRequest;			// class request waiting for its OUT data stage
	volatile uint8_t notifyPending;	// notifications still to send after the link comes up
} usbNcm_t;

static usbNcm_t ncm;
static USBD_HandleTypeDef *ncmDevice;

// OTG_FS has no DMA (the CPU copies to and from the FIFOs), so the big buffer can be in the CCM RAM
__ALIGN_BEGIN __attribute__((section(".ccmram"))) static uint8_t ntbOut[NTB_OUT_SIZE] __ALIGN_END;
__ALIGN_BEGIN static uint8_t ntbIn[NTB_IN_SIZE] __ALIGN_END;
__ALIGN_BEGIN static uint8_t notification[16] __ALIGN_END;
__ALIGN_BEGIN static uint8_t controlBuffer[32] __ALIGN_END;

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

void usbNcmSetMacAddress(const uint8_t hostMac[6])
{
	static const char HEX[] = "0123456789ABCDEF";

	macString[0] = sizeof(macString);
	macString[1] = USB_DESC_TYPE_STRING;
	for (int i = 0; i < 12; i++)
	{
		macString[2 + (2 * i)] = HEX[(hostMac[i / 2] >> ((i & 1) ? 0 : 4)) & 0x0F];
		macString[3 + (2 * i)] = 0;
	}
}

bool usbNcmIsUp(void)
{
	return ncm.configured && (ncm.dataAlternate == 1);
}

uint8_t usbNcmGetDataAlternate(void)
{
	return ncm.dataAlternate;
}

static void armOut(void)
{
	USBD_LL_PrepareReceive(ncmDevice, NCM_OUT_EP, &ntbOut[ncm.outLength], NCM_BULK_PACKET_SIZE);
}

static void sendNotification(uint8_t code)
{
	notification[0] = 0xA1;// class, interface, device to host
	notification[1] = code;
	put16(&notification[2], (code == NCM_NOTIFY_NETWORK_CONNECTION) ? 1 : 0);// connected
	put16(&notification[4], NCM_COMM_INTERFACE);
	put16(&notification[6], (code == NCM_NOTIFY_SPEED_CHANGE) ? 8 : 0);
	put32(&notification[8], REPORTED_SPEED);// downlink
	put32(&notification[12], REPORTED_SPEED);// uplink
	USBD_LL_Transmit(ncmDevice, NCM_NOTIFY_EP, notification, (code == NCM_NOTIFY_SPEED_CHANGE) ? 16 : 8);
}

static void setDataAlternate(uint8_t alternate)
{
	if (ncm.dataAlternate == 1)
	{
		USBD_LL_CloseEP(ncmDevice, NCM_IN_EP);
		USBD_LL_CloseEP(ncmDevice, NCM_OUT_EP);
		ncmDevice->ep_in[NCM_IN_EP & 0x0F].is_used = 0;
		ncmDevice->ep_out[NCM_OUT_EP & 0x0F].is_used = 0;
	}

	ncm.dataAlternate = alternate;
	ncm.inBusy = false;
	ncm.outReady = false;
	ncm.outLength = 0;

	if (alternate == 1)
	{
		USBD_LL_OpenEP(ncmDevice, NCM_IN_EP, USBD_EP_TYPE_BULK, NCM_BULK_PACKET_SIZE);
		USBD_LL_OpenEP(ncmDevice, NCM_OUT_EP, USBD_EP_TYPE_BULK, NCM_BULK_PACKET_SIZE);
		ncmDevice->ep_in[NCM_IN_EP & 0x0F].is_used = 1;
		ncmDevice->ep_out[NCM_OUT_EP & 0x0F].is_used = 1;
		armOut();
		ncm.notifyPending = 1;
		sendNotification(NCM_NOTIFY_SPEED_CHANGE);
	}
}

static uint8_t ncmInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
	ncmDevice = pdev;
	memset(&ncm, 0, sizeof(ncm));
	ncm.ntbInputSize = 2048;

	USBD_LL_OpenEP(pdev, NCM_NOTIFY_EP, USBD_EP_TYPE_INTR, NCM_NOTIFY_PACKET_SIZE);
	pdev->ep_in[NCM_NOTIFY_EP & 0x0F].is_used = 1;
	pdev->ep_in[NCM_NOTIFY_EP & 0x0F].bInterval = NCM_NOTIFY_INTERVAL;
	ncm.configured = true;

	// macOS only selects the data alternate setting once the link is reported up, Linux and Windows select it first.
	// So the speed and the connection are announced now, and again when alternate 1 is selected.
	ncm.notifyPending = 1;
	sendNotification(NCM_NOTIFY_SPEED_CHANGE);

	return (uint8_t)USBD_OK;
}

static uint8_t ncmDeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
	setDataAlternate(0);
	USBD_LL_CloseEP(pdev, NCM_NOTIFY_EP);
	pdev->ep_in[NCM_NOTIFY_EP & 0x0F].is_used = 0;
	ncm.configured = false;

	return (uint8_t)USBD_OK;
}

static uint8_t classRequest(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
	uint16_t length = 0;

	if (LOBYTE(req->wIndex) != NCM_COMM_INTERFACE)
	{
		return (uint8_t)USBD_FAIL;
	}

	switch (req->bRequest)
	{
		case NCM_GET_NTB_PARAMETERS:
			memcpy(controlBuffer, NTB_PARAMETERS, sizeof(NTB_PARAMETERS));
			length = sizeof(NTB_PARAMETERS);
			break;
		case NCM_GET_NTB_FORMAT:
		case NCM_GET_CRC_MODE:
			put16(controlBuffer, 0);// NTB16, no CRC
			length = 2;
			break;
		case NCM_GET_NTB_INPUT_SIZE:
			put32(controlBuffer, ncm.ntbInputSize);
			length = 4;
			break;
		case NCM_GET_MAX_DATAGRAM_SIZE:
			put16(controlBuffer, 1514);
			length = 2;
			break;
		case NCM_GET_NET_ADDRESS:
			memset(controlBuffer, 0, 6);// the host uses the address in the iMACAddress string
			length = 6;
			break;
		case NCM_SET_NTB_INPUT_SIZE:
		case NCM_SET_MAX_DATAGRAM_SIZE:
			if ((req->wLength == 0) || (req->wLength > sizeof(controlBuffer)))
			{
				return (uint8_t)USBD_FAIL;
			}
			ncm.pendingRequest = req->bRequest;
			USBD_CtlPrepareRx(pdev, controlBuffer, req->wLength);
			return (uint8_t)USBD_OK;
		case NCM_SET_NTB_FORMAT:
		case NCM_SET_CRC_MODE:
			return (req->wValue == 0) ? (uint8_t)USBD_OK : (uint8_t)USBD_FAIL;
		case NCM_SET_ETHERNET_PACKET_FILTER:
			return (uint8_t)USBD_OK;
		default:
			return (uint8_t)USBD_FAIL;
	}

	USBD_CtlSendData(pdev, controlBuffer, MIN(length, req->wLength));
	return (uint8_t)USBD_OK;
}

static uint8_t ncmSetup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
	switch (req->bmRequest & USB_REQ_TYPE_MASK)
	{
		case USB_REQ_TYPE_CLASS:
			if (classRequest(pdev, req) != USBD_OK)
			{
				USBD_CtlError(pdev, req);
				return (uint8_t)USBD_FAIL;
			}
			return (uint8_t)USBD_OK;

		case USB_REQ_TYPE_VENDOR:
			// Microsoft OS 1.0 descriptors: extended compat ID
			if ((req->bRequest == MS_OS_VENDOR_CODE) && (req->wIndex == MS_OS_EXTENDED_COMPAT_ID))
			{
				USBD_CtlSendData(pdev, (uint8_t *)msOsCompatId, MIN(sizeof(msOsCompatId), req->wLength));
				return (uint8_t)USBD_OK;
			}
			USBD_CtlError(pdev, req);
			return (uint8_t)USBD_FAIL;

		case USB_REQ_TYPE_STANDARD:
			switch (req->bRequest)
			{
				case USB_REQ_GET_STATUS:
					put16(controlBuffer, 0);
					USBD_CtlSendData(pdev, controlBuffer, 2);
					return (uint8_t)USBD_OK;

				case USB_REQ_GET_INTERFACE:
					controlBuffer[0] = (LOBYTE(req->wIndex) == NCM_DATA_INTERFACE) ? ncm.dataAlternate : 0;
					USBD_CtlSendData(pdev, controlBuffer, 1);
					return (uint8_t)USBD_OK;

				case USB_REQ_SET_INTERFACE:
					if ((LOBYTE(req->wIndex) == NCM_DATA_INTERFACE) && (req->wValue <= 1))
					{
						if (req->wValue != ncm.dataAlternate)
						{
							setDataAlternate(req->wValue);
						}
						return (uint8_t)USBD_OK;
					}
					if ((LOBYTE(req->wIndex) == NCM_COMM_INTERFACE) && (req->wValue == 0))
					{
						return (uint8_t)USBD_OK;
					}
					break;

				case USB_REQ_CLEAR_FEATURE:
					return (uint8_t)USBD_OK;

				default:
					break;
			}
			USBD_CtlError(pdev, req);
			return (uint8_t)USBD_FAIL;

		default:
			USBD_CtlError(pdev, req);
			return (uint8_t)USBD_FAIL;
	}
}

static uint8_t ncmEP0RxReady(USBD_HandleTypeDef *pdev)
{
	if (ncm.pendingRequest == NCM_SET_NTB_INPUT_SIZE)
	{
		ncm.ntbInputSize = controlBuffer[0] | (controlBuffer[1] << 8) | (controlBuffer[2] << 16) | ((uint32_t)controlBuffer[3] << 24);
	}
	ncm.pendingRequest = 0;

	return (uint8_t)USBD_OK;
}

// ISR: the speed is announced first, then the connection
static uint8_t ncmDataIn(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
	if (epnum == (NCM_NOTIFY_EP & 0x0F))
	{
		if (ncm.notifyPending && ncm.configured)
		{
			ncm.notifyPending = 0;
			sendNotification(NCM_NOTIFY_NETWORK_CONNECTION);
		}
	}
	else if (epnum == (NCM_IN_EP & 0x0F))
	{
		ncm.inBusy = false;
	}

	return (uint8_t)USBD_OK;
}

// ISR: collect the packets of one NTB
static uint8_t ncmDataOut(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
	if ((epnum != NCM_OUT_EP) || !usbNcmIsUp())
	{
		return (uint8_t)USBD_OK;
	}

	uint32_t received = USBD_LL_GetRxDataSize(pdev, epnum);
	bool shortPacket = (received < NCM_BULK_PACKET_SIZE);
	ncm.outLength += received;

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
			ncm.parseEntry = 8;
			ncm.outReady = true;// usbNcmTick re-arms the endpoint
			return (uint8_t)USBD_OK;
		}
	}

	if (shortPacket || ((ncm.outLength + NCM_BULK_PACKET_SIZE) > NTB_OUT_SIZE))
	{
		ncm.outLength = 0;// truncated NTB, or a zero length packet
	}

	armOut();
	return (uint8_t)USBD_OK;
}

static uint8_t *ncmGetConfigDescriptor(uint16_t *length)
{
	*length = sizeof(configDescriptor);
	return configDescriptor;
}

#if (USBD_SUPPORT_USER_STRING_DESC == 1U)
static uint8_t *ncmGetUsrStrDescriptor(USBD_HandleTypeDef *pdev, uint8_t index, uint16_t *length)
{
	if (index == NCM_MAC_STRING_INDEX)
	{
		*length = sizeof(macString);
		return macString;
	}
	if (index == MS_OS_STRING_INDEX)
	{
		*length = sizeof(msOsString);
		return (uint8_t *)msOsString;
	}
	return NULL;
}
#else
#error The network adapter needs USBD_SUPPORT_USER_STRING_DESC (MAC address and Microsoft OS strings)
#endif

USBD_ClassTypeDef USBD_NCM =
{
	ncmInit,
	ncmDeInit,
	ncmSetup,
	NULL,
	ncmEP0RxReady,
	ncmDataIn,
	ncmDataOut,
	NULL,
	NULL,
	NULL,
	ncmGetConfigDescriptor,
	ncmGetConfigDescriptor,
	ncmGetConfigDescriptor,
	NULL,
	ncmGetUsrStrDescriptor,
};

// One datagram per NTB16: NTH16 at 0, NDP16 at 12, the frame at 28 (4 byte aligned)
bool usbNcmSendFrame(const uint8_t *frame, int length)
{
	if (!usbNcmIsUp() || ncm.inBusy || (length <= 0) || ((NTH16_LENGTH + NDP16_LENGTH + length + 1) > NTB_IN_SIZE))
	{
		return false;
	}

	ncm.inBusy = true;

	int total = NTH16_LENGTH + NDP16_LENGTH + length;
	if ((total % NCM_BULK_PACKET_SIZE) == 0)
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

	if (USBD_LL_Transmit(ncmDevice, NCM_IN_EP, ntbIn, total) != USBD_OK)
	{
		ncm.inBusy = false;
		return false;
	}
	return true;
}

// Hands the datagrams of the received NTB to the gateway, one at a time while the IN endpoint is free
// (each one may need a reply), then re-arms the OUT endpoint
void usbNcmTick(void)
{
	if (!ncm.outReady)
	{
		return;
	}

	uint16_t blockLength = get16(&ntbOut[8]);
	int ndpLimit = 8;// against NDP chains that loop

	while (!ncm.inBusy && (ncm.parseNdp != 0) && ndpLimit--)
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
			ncm.parseEntry = 8;
			continue;
		}

		uint16_t index = get16(&ndp[ncm.parseEntry]);
		uint16_t length = get16(&ndp[ncm.parseEntry + 2]);
		ncm.parseEntry += 4;

		if ((index == 0) || (length == 0))
		{
			ncm.parseNdp = get16(&ndp[6]);// end of this NDP's datagrams
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
