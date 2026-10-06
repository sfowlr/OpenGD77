/*
 * USB CDC-NCM (Network Control Model) function for the NXP KSDK device stack
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

#ifndef _OPENGD77_USB_NCM_H_
#define _OPENGD77_USB_NCM_H_

#include "usb.h"
#include "usb_device.h"
#include "usb_device_class.h"

// Class driver entry points, see s_UsbDeviceClassInterfaceMap in usb_device_class.c
usb_status_t USB_DeviceNcmInit(uint8_t controllerId, usb_device_class_config_struct_t *config, class_handle_t *handle);
usb_status_t USB_DeviceNcmDeinit(class_handle_t handle);
usb_status_t USB_DeviceNcmEvent(void *handle, uint32_t event, void *param);

// True once the host has selected the data interface alternate setting 1 (the link is up)
bool usbNcmIsUp(void);
uint8_t usbNcmGetDataAlternate(void);

// One Ethernet frame to the host. Returns false if the link is down or the previous frame is still being sent
bool usbNcmSendFrame(const uint8_t *frame, int length);

// Main task: hands the frames received from the host to ipGatewayEthernetIn()
void usbNcmTick(void);

#endif
