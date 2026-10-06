/*
 * USB CDC-NCM network adapter, STM32 (ST USB device library) placeholder
 *
 * The STM32F405 OTG_FS core has only 3 IN endpoints besides EP0, while CDC-ACM + CDC-NCM need 4, so the composite
 * device used on the MK22 radios doesn't fit. Until that is settled the network link is always down here, and the
 * packet data features work through the USB serial 'D' commands only.
 */

#include "usb/usb_ncm.h"
#include "functions/ipGateway.h"

bool usbNcmIsUp(void)
{
	return false;
}

uint8_t usbNcmGetDataAlternate(void)
{
	return 0;
}

bool usbNcmSendFrame(const uint8_t *frame, int length)
{
	(void)frame;
	(void)length;
	return false;
}

void usbNcmTick(void)
{
}

bool ipGatewaySendFrame(const uint8_t *frame, int length)
{
	return usbNcmSendFrame(frame, length);
}
