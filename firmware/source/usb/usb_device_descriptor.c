/*
 * Copyright (c) 2015 - 2016, Freescale Semiconductor, Inc.
 * Copyright 2016 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "usb_device_config.h"
#include "usb.h"
#include "usb_device.h"

#include "usb_device_class.h"
#include "usb_device_cdc_acm.h"

#include "usb/usb_device_descriptor.h"

/*******************************************************************************
* Variables
******************************************************************************/
/* Define endpoint for communication class */
usb_device_endpoint_struct_t g_UsbDeviceCdcVcomCicEndpoints[USB_CDC_VCOM_ENDPOINT_CIC_COUNT] = {
    {
        USB_CDC_VCOM_INTERRUPT_IN_ENDPOINT | (USB_IN << 7U), USB_ENDPOINT_INTERRUPT,
        FS_CDC_VCOM_INTERRUPT_IN_PACKET_SIZE,
    },
};

/* Define endpoint for data class */
usb_device_endpoint_struct_t g_UsbDeviceCdcVcomDicEndpoints[USB_CDC_VCOM_ENDPOINT_DIC_COUNT] = {
    {
        USB_CDC_VCOM_BULK_IN_ENDPOINT | (USB_IN << 7U), USB_ENDPOINT_BULK, FS_CDC_VCOM_BULK_IN_PACKET_SIZE,
    },
    {
        USB_CDC_VCOM_BULK_OUT_ENDPOINT | (USB_OUT << 7U), USB_ENDPOINT_BULK, FS_CDC_VCOM_BULK_OUT_PACKET_SIZE,
    }};

/* Define interface for communication class */
usb_device_interface_struct_t g_UsbDeviceCdcVcomCommunicationInterface[] = {{
    0,
    {
        USB_CDC_VCOM_ENDPOINT_CIC_COUNT, g_UsbDeviceCdcVcomCicEndpoints,
    },
}};

/* Define interface for data class */
usb_device_interface_struct_t g_UsbDeviceCdcVcomDataInterface[] = {{
    0,
    {
        USB_CDC_VCOM_ENDPOINT_DIC_COUNT, g_UsbDeviceCdcVcomDicEndpoints,
    },
}};

/* Define interfaces for virtual com */
usb_device_interfaces_struct_t g_UsbDeviceCdcVcomInterfaces[USB_CDC_VCOM_INTERFACE_COUNT] = {
    {USB_CDC_VCOM_CIC_CLASS, USB_CDC_VCOM_CIC_SUBCLASS, USB_CDC_VCOM_CIC_PROTOCOL, USB_CDC_VCOM_COMM_INTERFACE_INDEX,
     g_UsbDeviceCdcVcomCommunicationInterface,
     sizeof(g_UsbDeviceCdcVcomCommunicationInterface) / sizeof(usb_device_interfaces_struct_t)},
    {USB_CDC_VCOM_DIC_CLASS, USB_CDC_VCOM_DIC_SUBCLASS, USB_CDC_VCOM_DIC_PROTOCOL, USB_CDC_VCOM_DATA_INTERFACE_INDEX,
     g_UsbDeviceCdcVcomDataInterface, sizeof(g_UsbDeviceCdcVcomDataInterface) / sizeof(usb_device_interfaces_struct_t)},
};

/* Define configurations for virtual com */
usb_device_interface_list_t g_UsbDeviceCdcVcomInterfaceList[USB_DEVICE_CONFIGURATION_COUNT] = {
    {
        USB_CDC_VCOM_INTERFACE_COUNT, g_UsbDeviceCdcVcomInterfaces,
    },
};

/* Define class information for virtual com */
usb_device_class_struct_t g_UsbDeviceCdcVcomConfig = {
    g_UsbDeviceCdcVcomInterfaceList, kUSB_DeviceClassTypeCdc, USB_DEVICE_CONFIGURATION_COUNT,
};

/* Define device descriptor */
USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceDescriptor[] = {
    /* Size of this descriptor in bytes */
    USB_DESCRIPTOR_LENGTH_DEVICE,
    /* DEVICE Descriptor Type */
    USB_DESCRIPTOR_TYPE_DEVICE,
    /* USB Specification Release Number in Binary-Coded Decimal (i.e., 2.10 is 210H). */
    USB_SHORT_GET_LOW(USB_DEVICE_SPECIFIC_BCD_VERSION), USB_SHORT_GET_HIGH(USB_DEVICE_SPECIFIC_BCD_VERSION),
    /* Class code (assigned by the USB-IF). */
    USB_DEVICE_CLASS,
    /* Subclass code (assigned by the USB-IF). */
    USB_DEVICE_SUBCLASS,
    /* Protocol code (assigned by the USB-IF). */
    USB_DEVICE_PROTOCOL,
    /* Maximum packet size for endpoint zero (only 8, 16, 32, or 64 are valid) */
    USB_CONTROL_MAX_PACKET_SIZE,
    /* Vendor ID (assigned by the USB-IF) */
    0xC9U, 0x1FU,
    /* Product ID (assigned by the manufacturer) */
    USB_SHORT_GET_LOW(USB_DEVICE_PRODUCT_ID), USB_SHORT_GET_HIGH(USB_DEVICE_PRODUCT_ID),
    /* Device release number in binary-coded decimal */
    USB_SHORT_GET_LOW(USB_DEVICE_DEMO_BCD_VERSION), USB_SHORT_GET_HIGH(USB_DEVICE_DEMO_BCD_VERSION),
    /* Index of string descriptor describing manufacturer */
    0x01,
    /* Index of string descriptor describing product */
    0x02,
    /* Index of string descriptor describing the device's serial number */
    0x00,
    /* Number of possible configurations */
    USB_DEVICE_CONFIGURATION_COUNT,
};

/* Define configuration descriptor */
USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceConfigurationDescriptor[] = {
    /* Size of this descriptor in bytes */
    USB_DESCRIPTOR_LENGTH_CONFIGURE,
    /* CONFIGURATION Descriptor Type */
    USB_DESCRIPTOR_TYPE_CONFIGURE,
    /* Total length of data returned for this configuration. */
    USB_SHORT_GET_LOW(USB_DESCRIPTOR_LENGTH_CONFIGURE + USB_DESCRIPTOR_LENGTH_INTERFACE +
                      USB_DESCRIPTOR_LENGTH_CDC_HEADER_FUNC + USB_DESCRIPTOR_LENGTH_CDC_CALL_MANAG +
                      USB_DESCRIPTOR_LENGTH_CDC_ABSTRACT + USB_DESCRIPTOR_LENGTH_CDC_UNION_FUNC +
                      USB_DESCRIPTOR_LENGTH_ENDPOINT + USB_DESCRIPTOR_LENGTH_INTERFACE +
                      USB_DESCRIPTOR_LENGTH_ENDPOINT + USB_DESCRIPTOR_LENGTH_ENDPOINT),
    USB_SHORT_GET_HIGH(USB_DESCRIPTOR_LENGTH_CONFIGURE + USB_DESCRIPTOR_LENGTH_INTERFACE +
                       USB_DESCRIPTOR_LENGTH_CDC_HEADER_FUNC + USB_DESCRIPTOR_LENGTH_CDC_CALL_MANAG +
                       USB_DESCRIPTOR_LENGTH_CDC_ABSTRACT + USB_DESCRIPTOR_LENGTH_CDC_UNION_FUNC +
                       USB_DESCRIPTOR_LENGTH_ENDPOINT + USB_DESCRIPTOR_LENGTH_INTERFACE +
                       USB_DESCRIPTOR_LENGTH_ENDPOINT + USB_DESCRIPTOR_LENGTH_ENDPOINT),
    /* Number of interfaces supported by this configuration */
    USB_CDC_VCOM_INTERFACE_COUNT,
    /* Value to use as an argument to the SetConfiguration() request to select this configuration */
    USB_CDC_VCOM_CONFIGURE_INDEX,
    /* Index of string descriptor describing this configuration */
    0,
    /* Configuration characteristics D7: Reserved (set to one) D6: Self-powered D5: Remote Wakeup D4...0: Reserved
       (reset to zero) */
    (USB_DESCRIPTOR_CONFIGURE_ATTRIBUTE_D7_MASK) |
        (USB_DEVICE_CONFIG_SELF_POWER << USB_DESCRIPTOR_CONFIGURE_ATTRIBUTE_SELF_POWERED_SHIFT) |
        (USB_DEVICE_CONFIG_REMOTE_WAKEUP << USB_DESCRIPTOR_CONFIGURE_ATTRIBUTE_REMOTE_WAKEUP_SHIFT),
    /* Maximum power consumption of the USB * device from the bus in this specific * configuration when the device is
       fully * operational. Expressed in 2 mA units *  (i.e., 50 = 100 mA).  */
    USB_DEVICE_MAX_POWER,

    /* Communication Interface Descriptor */
    USB_DESCRIPTOR_LENGTH_INTERFACE, USB_DESCRIPTOR_TYPE_INTERFACE, USB_CDC_VCOM_COMM_INTERFACE_INDEX, 0x00,
    USB_CDC_VCOM_ENDPOINT_CIC_COUNT, USB_CDC_VCOM_CIC_CLASS, USB_CDC_VCOM_CIC_SUBCLASS, USB_CDC_VCOM_CIC_PROTOCOL,
    0x00, /* Interface Description String Index*/

    /* CDC Class-Specific descriptor */
    USB_DESCRIPTOR_LENGTH_CDC_HEADER_FUNC, /* Size of this descriptor in bytes */
    USB_DESCRIPTOR_TYPE_CDC_CS_INTERFACE,  /* CS_INTERFACE Descriptor Type */
    USB_CDC_HEADER_FUNC_DESC, 0x10,
    0x01, /* USB Class Definitions for Communications the Communication specification version 1.10 */

    USB_DESCRIPTOR_LENGTH_CDC_CALL_MANAG, /* Size of this descriptor in bytes */
    USB_DESCRIPTOR_TYPE_CDC_CS_INTERFACE, /* CS_INTERFACE Descriptor Type */
    USB_CDC_CALL_MANAGEMENT_FUNC_DESC,
    0x01, /*Bit 0: Whether device handle call management itself 1, Bit 1: Whether device can send/receive call
             management information over a Data Class Interface 0 */
    0x01, /* Indicates multiplexed commands are handled via data interface */

    USB_DESCRIPTOR_LENGTH_CDC_ABSTRACT,   /* Size of this descriptor in bytes */
    USB_DESCRIPTOR_TYPE_CDC_CS_INTERFACE, /* CS_INTERFACE Descriptor Type */
    USB_CDC_ABSTRACT_CONTROL_FUNC_DESC,
    0x06, /* Bit 0: Whether device supports the request combination of Set_Comm_Feature, Clear_Comm_Feature, and
             Get_Comm_Feature 0, Bit 1: Whether device supports the request combination of Set_Line_Coding,
             Set_Control_Line_State, Get_Line_Coding, and the notification Serial_State 1, Bit ...  */

    USB_DESCRIPTOR_LENGTH_CDC_UNION_FUNC, /* Size of this descriptor in bytes */
    USB_DESCRIPTOR_TYPE_CDC_CS_INTERFACE, /* CS_INTERFACE Descriptor Type */
    USB_CDC_UNION_FUNC_DESC, 0x00,        /* The interface number of the Communications or Data Class interface  */
    0x01,                                 /* Interface number of subordinate interface in the Union  */

    /*Notification Endpoint descriptor */
    USB_DESCRIPTOR_LENGTH_ENDPOINT, USB_DESCRIPTOR_TYPE_ENDPOINT, USB_CDC_VCOM_INTERRUPT_IN_ENDPOINT | (USB_IN << 7U),
    USB_ENDPOINT_INTERRUPT, USB_SHORT_GET_LOW(FS_CDC_VCOM_INTERRUPT_IN_PACKET_SIZE),
    USB_SHORT_GET_HIGH(FS_CDC_VCOM_INTERRUPT_IN_PACKET_SIZE), FS_CDC_VCOM_INTERRUPT_IN_INTERVAL,

    /* Data Interface Descriptor */
    USB_DESCRIPTOR_LENGTH_INTERFACE, USB_DESCRIPTOR_TYPE_INTERFACE, USB_CDC_VCOM_DATA_INTERFACE_INDEX, 0x00,
    USB_CDC_VCOM_ENDPOINT_DIC_COUNT, USB_CDC_VCOM_DIC_CLASS, USB_CDC_VCOM_DIC_SUBCLASS, USB_CDC_VCOM_DIC_PROTOCOL,
    0x00, /* Interface Description String Index*/

    /*Bulk IN Endpoint descriptor */
    USB_DESCRIPTOR_LENGTH_ENDPOINT, USB_DESCRIPTOR_TYPE_ENDPOINT, USB_CDC_VCOM_BULK_IN_ENDPOINT | (USB_IN << 7U),
    USB_ENDPOINT_BULK, USB_SHORT_GET_LOW(FS_CDC_VCOM_BULK_IN_PACKET_SIZE),
    USB_SHORT_GET_HIGH(FS_CDC_VCOM_BULK_IN_PACKET_SIZE), 0x00, /* The polling interval value is every 0 Frames */

    /*Bulk OUT Endpoint descriptor */
    USB_DESCRIPTOR_LENGTH_ENDPOINT, USB_DESCRIPTOR_TYPE_ENDPOINT, USB_CDC_VCOM_BULK_OUT_ENDPOINT | (USB_OUT << 7U),
    USB_ENDPOINT_BULK, USB_SHORT_GET_LOW(FS_CDC_VCOM_BULK_OUT_PACKET_SIZE),
    USB_SHORT_GET_HIGH(FS_CDC_VCOM_BULK_OUT_PACKET_SIZE), 0x00, /* The polling interval value is every 0 Frames */
};

/* CDC-NCM function, the network side of the composite device */
usb_device_endpoint_struct_t g_UsbDeviceNcmCommEndpoints[1] = {
    {USB_NCM_INTERRUPT_IN_ENDPOINT | (USB_IN << 7U), USB_ENDPOINT_INTERRUPT, FS_NCM_INTERRUPT_IN_PACKET_SIZE},
};

usb_device_endpoint_struct_t g_UsbDeviceNcmDataEndpoints[2] = {
    {USB_NCM_BULK_ENDPOINT | (USB_IN << 7U), USB_ENDPOINT_BULK, FS_NCM_BULK_PACKET_SIZE},
    {USB_NCM_BULK_ENDPOINT | (USB_OUT << 7U), USB_ENDPOINT_BULK, FS_NCM_BULK_PACKET_SIZE},
};

usb_device_interface_struct_t g_UsbDeviceNcmCommInterface[] = {{0, {1, g_UsbDeviceNcmCommEndpoints}}};

/* Alternate setting 0 has no endpoints (link down), 1 has the two bulk endpoints */
usb_device_interface_struct_t g_UsbDeviceNcmDataInterface[] = {{0, {0, NULL}}, {1, {2, g_UsbDeviceNcmDataEndpoints}}};

usb_device_interfaces_struct_t g_UsbDeviceNcmInterfaces[2] = {
    {CDC_COMM_CLASS, USB_CDC_NCM_SUBCLASS, 0x00, USB_NCM_COMM_INTERFACE_INDEX, g_UsbDeviceNcmCommInterface, 1},
    {CDC_DATA_CLASS, 0x00, USB_CDC_NCM_DATA_PROTOCOL, USB_NCM_DATA_INTERFACE_INDEX, g_UsbDeviceNcmDataInterface, 2},
};

usb_device_interface_list_t g_UsbDeviceNcmInterfaceList[USB_DEVICE_CONFIGURATION_COUNT] = {{2, g_UsbDeviceNcmInterfaces}};

usb_device_class_struct_t g_UsbDeviceNcmConfig = {
    g_UsbDeviceNcmInterfaceList, kUSB_DeviceClassTypeCdcNcm, USB_DEVICE_CONFIGURATION_COUNT,
};

/* Composite configuration: an IAD per function, the serial port first so that it stays interfaces 0 and 1 */
USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceCompositeConfigurationDescriptor[] = {
    USB_DESCRIPTOR_LENGTH_CONFIGURE, USB_DESCRIPTOR_TYPE_CONFIGURE,
    0x00, 0x00, /* wTotalLength, set by USB_DeviceDescriptorsSelect */
    USB_COMPOSITE_INTERFACE_COUNT, USB_CDC_VCOM_CONFIGURE_INDEX, 0,
    (USB_DESCRIPTOR_CONFIGURE_ATTRIBUTE_D7_MASK) |
        (USB_DEVICE_CONFIG_SELF_POWER << USB_DESCRIPTOR_CONFIGURE_ATTRIBUTE_SELF_POWERED_SHIFT) |
        (USB_DEVICE_CONFIG_REMOTE_WAKEUP << USB_DESCRIPTOR_CONFIGURE_ATTRIBUTE_REMOTE_WAKEUP_SHIFT),
    USB_DEVICE_MAX_POWER,

    /* Serial port: IAD, then the same interfaces as the serial port only configuration */
    8, USB_DESCRIPTOR_TYPE_IAD, USB_CDC_VCOM_COMM_INTERFACE_INDEX, 2, USB_CDC_VCOM_CIC_CLASS, USB_CDC_VCOM_CIC_SUBCLASS,
    USB_CDC_VCOM_CIC_PROTOCOL, 0x00,

    USB_DESCRIPTOR_LENGTH_INTERFACE, USB_DESCRIPTOR_TYPE_INTERFACE, USB_CDC_VCOM_COMM_INTERFACE_INDEX, 0x00,
    USB_CDC_VCOM_ENDPOINT_CIC_COUNT, USB_CDC_VCOM_CIC_CLASS, USB_CDC_VCOM_CIC_SUBCLASS, USB_CDC_VCOM_CIC_PROTOCOL, 0x00,
    USB_DESCRIPTOR_LENGTH_CDC_HEADER_FUNC, USB_DESCRIPTOR_TYPE_CDC_CS_INTERFACE, USB_CDC_HEADER_FUNC_DESC, 0x10, 0x01,
    USB_DESCRIPTOR_LENGTH_CDC_CALL_MANAG, USB_DESCRIPTOR_TYPE_CDC_CS_INTERFACE, USB_CDC_CALL_MANAGEMENT_FUNC_DESC, 0x01, 0x01,
    USB_DESCRIPTOR_LENGTH_CDC_ABSTRACT, USB_DESCRIPTOR_TYPE_CDC_CS_INTERFACE, USB_CDC_ABSTRACT_CONTROL_FUNC_DESC, 0x06,
    USB_DESCRIPTOR_LENGTH_CDC_UNION_FUNC, USB_DESCRIPTOR_TYPE_CDC_CS_INTERFACE, USB_CDC_UNION_FUNC_DESC,
    USB_CDC_VCOM_COMM_INTERFACE_INDEX, USB_CDC_VCOM_DATA_INTERFACE_INDEX,
    USB_DESCRIPTOR_LENGTH_ENDPOINT, USB_DESCRIPTOR_TYPE_ENDPOINT, USB_CDC_VCOM_INTERRUPT_IN_ENDPOINT | (USB_IN << 7U),
    USB_ENDPOINT_INTERRUPT, USB_SHORT_GET_LOW(FS_CDC_VCOM_INTERRUPT_IN_PACKET_SIZE),
    USB_SHORT_GET_HIGH(FS_CDC_VCOM_INTERRUPT_IN_PACKET_SIZE), FS_CDC_VCOM_INTERRUPT_IN_INTERVAL,

    USB_DESCRIPTOR_LENGTH_INTERFACE, USB_DESCRIPTOR_TYPE_INTERFACE, USB_CDC_VCOM_DATA_INTERFACE_INDEX, 0x00,
    USB_CDC_VCOM_ENDPOINT_DIC_COUNT, USB_CDC_VCOM_DIC_CLASS, USB_CDC_VCOM_DIC_SUBCLASS, USB_CDC_VCOM_DIC_PROTOCOL, 0x00,
    USB_DESCRIPTOR_LENGTH_ENDPOINT, USB_DESCRIPTOR_TYPE_ENDPOINT, USB_CDC_VCOM_BULK_IN_ENDPOINT | (USB_IN << 7U),
    USB_ENDPOINT_BULK, USB_SHORT_GET_LOW(FS_CDC_VCOM_BULK_IN_PACKET_SIZE), USB_SHORT_GET_HIGH(FS_CDC_VCOM_BULK_IN_PACKET_SIZE), 0x00,
    USB_DESCRIPTOR_LENGTH_ENDPOINT, USB_DESCRIPTOR_TYPE_ENDPOINT, USB_CDC_VCOM_BULK_OUT_ENDPOINT | (USB_OUT << 7U),
    USB_ENDPOINT_BULK, USB_SHORT_GET_LOW(FS_CDC_VCOM_BULK_OUT_PACKET_SIZE), USB_SHORT_GET_HIGH(FS_CDC_VCOM_BULK_OUT_PACKET_SIZE), 0x00,

    /* Network: IAD, NCM communication interface */
    8, USB_DESCRIPTOR_TYPE_IAD, USB_NCM_COMM_INTERFACE_INDEX, 2, CDC_COMM_CLASS, USB_CDC_NCM_SUBCLASS, 0x00, 0x00,

    USB_DESCRIPTOR_LENGTH_INTERFACE, USB_DESCRIPTOR_TYPE_INTERFACE, USB_NCM_COMM_INTERFACE_INDEX, 0x00, 1,
    CDC_COMM_CLASS, USB_CDC_NCM_SUBCLASS, 0x00, 0x00,
    USB_DESCRIPTOR_LENGTH_CDC_HEADER_FUNC, USB_DESCRIPTOR_TYPE_CDC_CS_INTERFACE, USB_CDC_HEADER_FUNC_DESC, 0x10, 0x01,
    USB_DESCRIPTOR_LENGTH_CDC_UNION_FUNC, USB_DESCRIPTOR_TYPE_CDC_CS_INTERFACE, USB_CDC_UNION_FUNC_DESC,
    USB_NCM_COMM_INTERFACE_INDEX, USB_NCM_DATA_INTERFACE_INDEX,
    /* Ethernet networking: iMACAddress, no statistics, wMaxSegmentSize 1514, no multicast or power filters */
    13, USB_DESCRIPTOR_TYPE_CDC_CS_INTERFACE, USB_CDC_ETHERNET_NETWORKING_FUNC_DESC, USB_NCM_MAC_STRING_INDEX,
    0x00, 0x00, 0x00, 0x00, USB_SHORT_GET_LOW(1514), USB_SHORT_GET_HIGH(1514), 0x00, 0x00, 0x00,
    /* NCM 1.00, no optional capabilities */
    6, USB_DESCRIPTOR_TYPE_CDC_CS_INTERFACE, USB_CDC_NCM_FUNC_DESC, 0x00, 0x01, 0x00,
    USB_DESCRIPTOR_LENGTH_ENDPOINT, USB_DESCRIPTOR_TYPE_ENDPOINT, USB_NCM_INTERRUPT_IN_ENDPOINT | (USB_IN << 7U),
    USB_ENDPOINT_INTERRUPT, USB_SHORT_GET_LOW(FS_NCM_INTERRUPT_IN_PACKET_SIZE), USB_SHORT_GET_HIGH(FS_NCM_INTERRUPT_IN_PACKET_SIZE),
    FS_NCM_INTERRUPT_IN_INTERVAL,

    /* NCM data interface, alternate setting 0: no endpoints */
    USB_DESCRIPTOR_LENGTH_INTERFACE, USB_DESCRIPTOR_TYPE_INTERFACE, USB_NCM_DATA_INTERFACE_INDEX, 0x00, 0,
    CDC_DATA_CLASS, 0x00, USB_CDC_NCM_DATA_PROTOCOL, 0x00,
    /* alternate setting 1: the bulk endpoints */
    USB_DESCRIPTOR_LENGTH_INTERFACE, USB_DESCRIPTOR_TYPE_INTERFACE, USB_NCM_DATA_INTERFACE_INDEX, 0x01, 2,
    CDC_DATA_CLASS, 0x00, USB_CDC_NCM_DATA_PROTOCOL, 0x00,
    USB_DESCRIPTOR_LENGTH_ENDPOINT, USB_DESCRIPTOR_TYPE_ENDPOINT, USB_NCM_BULK_ENDPOINT | (USB_IN << 7U),
    USB_ENDPOINT_BULK, USB_SHORT_GET_LOW(FS_NCM_BULK_PACKET_SIZE), USB_SHORT_GET_HIGH(FS_NCM_BULK_PACKET_SIZE), 0x00,
    USB_DESCRIPTOR_LENGTH_ENDPOINT, USB_DESCRIPTOR_TYPE_ENDPOINT, USB_NCM_BULK_ENDPOINT | (USB_OUT << 7U),
    USB_ENDPOINT_BULK, USB_SHORT_GET_LOW(FS_NCM_BULK_PACKET_SIZE), USB_SHORT_GET_HIGH(FS_NCM_BULK_PACKET_SIZE), 0x00,
};

/* The host side MAC address of the NCM link, as 12 hex digits */
USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceStringMac[2U + 2U * 12U] = {2U + 2U * 12U, USB_DESCRIPTOR_TYPE_STRING};

/* Microsoft OS 1.0 descriptors: the "MSFT100" string at index 0xEE gives the vendor request code, and the
 * Extended Compat ID gives the NCM function the WINNCM compatible ID, which binds the Windows 10 UsbNcm driver
 * (Windows 11 binds it from the class codes alone) */
USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceStringMsOs[18] = {18, USB_DESCRIPTOR_TYPE_STRING, 'M', 0, 'S', 0, 'F', 0, 'T', 0, '1', 0, '0', 0, '0', 0,
                                     USB_MS_OS_VENDOR_CODE, 0x00};

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceMsOsCompatId[40] = {
    40, 0, 0, 0,                  /* dwLength */
    0x00, 0x01,                   /* bcdVersion 1.00 */
    0x04, 0x00,                   /* wIndex: Extended Compat ID */
    1,                            /* bCount: one function section */
    0, 0, 0, 0, 0, 0, 0,          /* reserved */
    USB_NCM_COMM_INTERFACE_INDEX, /* bFirstInterfaceNumber */
    0x01,                         /* reserved, must be 1 */
    'W', 'I', 'N', 'N', 'C', 'M', 0, 0, /* compatibleID */
    0, 0, 0, 0, 0, 0, 0, 0,       /* subCompatibleID */
    0, 0, 0, 0, 0, 0,             /* reserved */
};

static bool compositeSelected = false;

/* Define string descriptor */
USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceString0[] = {2U + 2U, USB_DESCRIPTOR_TYPE_STRING, 0x09, 0x04};

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceString1[] = {
    2U + 2U * 18U, USB_DESCRIPTOR_TYPE_STRING,
    'N',           0x00U,
    'X',           0x00U,
    'P',           0x00U,
    ' ',           0x00U,
    'S',           0x00U,
    'E',           0x00U,
    'M',           0x00U,
    'I',           0x00U,
    'C',           0x00U,
    'O',           0x00U,
    'N',           0x00U,
    'D',           0x00U,
    'U',           0x00U,
    'C',           0x00U,
    'T',           0x00U,
    'O',           0x00U,
    'R',           0x00U,
    'S',           0x00U,
};

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceString2[] = {2U + 2U * 20U, USB_DESCRIPTOR_TYPE_STRING,
                                'M',           0,
                                'C',           0,
                                'U',           0,
                                ' ',           0,
                                'V',           0,
                                'I',           0,
                                'R',           0,
                                'T',           0,
                                'U',           0,
                                'A',           0,
                                'L',           0,
                                ' ',           0,
                                'C',           0,
                                'O',           0,
                                'M',           0,
                                ' ',           0,
                                'D',           0,
                                'E',           0,
                                'M',           0,
                                'O',           0};

uint8_t *g_UsbDeviceStringDescriptorArray[USB_DEVICE_STRING_COUNT] = {g_UsbDeviceString0, g_UsbDeviceString1,
                                                                      g_UsbDeviceString2, g_UsbDeviceStringMac};

/* Define string descriptor size */
uint32_t g_UsbDeviceStringDescriptorLength[USB_DEVICE_STRING_COUNT] = {
    sizeof(g_UsbDeviceString0), sizeof(g_UsbDeviceString1), sizeof(g_UsbDeviceString2), sizeof(g_UsbDeviceStringMac)};
usb_language_t g_UsbDeviceLanguage[USB_DEVICE_LANGUAGE_COUNT] = {{
    g_UsbDeviceStringDescriptorArray, g_UsbDeviceStringDescriptorLength, (uint16_t)0x0409,
}};

usb_language_list_t g_UsbDeviceLanguageList = {
    g_UsbDeviceString0, sizeof(g_UsbDeviceString0), g_UsbDeviceLanguage, USB_DEVICE_LANGUAGE_COUNT,
};

/*******************************************************************************
* Code
******************************************************************************/
/*!
 * @brief USB device get device descriptor function.
 *
 * This function gets the device descriptor of the USB device.
 *
 * @param handle The USB device handle.
 * @param deviceDescriptor The pointer to the device descriptor structure.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceGetDeviceDescriptor(usb_device_handle handle,
                                           usb_device_get_device_descriptor_struct_t *deviceDescriptor)
{
    deviceDescriptor->buffer = g_UsbDeviceDescriptor;
    deviceDescriptor->length = USB_DESCRIPTOR_LENGTH_DEVICE;
    return kStatus_USB_Success;
}

/*!
 * @brief USB device get configuration descriptor function.
 *
 * This function gets the configuration descriptor of the USB device.
 *
 * @param handle The USB device handle.
 * @param configurationDescriptor The pointer to the configuration descriptor structure.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceGetConfigurationDescriptor(
    usb_device_handle handle, usb_device_get_configuration_descriptor_struct_t *configurationDescriptor)
{
    if (USB_CDC_VCOM_CONFIGURE_INDEX > configurationDescriptor->configuration)
    {
        if (compositeSelected)
        {
            configurationDescriptor->buffer = g_UsbDeviceCompositeConfigurationDescriptor;
            configurationDescriptor->length = sizeof(g_UsbDeviceCompositeConfigurationDescriptor);
        }
        else
        {
            configurationDescriptor->buffer = g_UsbDeviceConfigurationDescriptor;
            configurationDescriptor->length = USB_DESCRIPTOR_LENGTH_CONFIGURATION_ALL;
        }
        return kStatus_USB_Success;
    }
    return kStatus_USB_InvalidRequest;
}

void USB_DeviceDescriptorsSelect(bool composite, const uint8_t hostMac[6])
{
    static const char HEX[] = "0123456789ABCDEF";

    compositeSelected = composite;

    /* Composite devices with IADs use the Miscellaneous class, and a different product ID so that
     * hosts don't reuse the drivers they bound to the serial port only device */
    g_UsbDeviceDescriptor[4] = composite ? 0xEF : USB_DEVICE_CLASS;
    g_UsbDeviceDescriptor[5] = composite ? 0x02 : USB_DEVICE_SUBCLASS;
    g_UsbDeviceDescriptor[6] = composite ? 0x01 : USB_DEVICE_PROTOCOL;
    g_UsbDeviceDescriptor[10] = USB_SHORT_GET_LOW(composite ? USB_DEVICE_PRODUCT_ID_COMPOSITE : USB_DEVICE_PRODUCT_ID);
    g_UsbDeviceDescriptor[11] = USB_SHORT_GET_HIGH(composite ? USB_DEVICE_PRODUCT_ID_COMPOSITE : USB_DEVICE_PRODUCT_ID);

    g_UsbDeviceCompositeConfigurationDescriptor[2] = USB_SHORT_GET_LOW(sizeof(g_UsbDeviceCompositeConfigurationDescriptor));
    g_UsbDeviceCompositeConfigurationDescriptor[3] = USB_SHORT_GET_HIGH(sizeof(g_UsbDeviceCompositeConfigurationDescriptor));

    for (int i = 0; i < 6; i++)
    {
        g_UsbDeviceStringMac[2 + (4 * i)] = HEX[hostMac[i] >> 4];
        g_UsbDeviceStringMac[3 + (4 * i)] = 0;
        g_UsbDeviceStringMac[4 + (4 * i)] = HEX[hostMac[i] & 0x0F];
        g_UsbDeviceStringMac[5 + (4 * i)] = 0;
    }
}

bool USB_DeviceDescriptorsIsComposite(void)
{
    return compositeSelected;
}

/* Device to host vendor request with our vendor code and wIndex 4: the Extended Compat ID */
usb_status_t USB_DeviceMsOsVendorRequest(usb_device_control_request_struct_t *request)
{
    if (!compositeSelected || !request->isSetup || (request->setup->bRequest != USB_MS_OS_VENDOR_CODE) ||
        ((request->setup->bmRequestType & USB_REQUEST_TYPE_DIR_MASK) != USB_REQUEST_TYPE_DIR_IN) ||
        (request->setup->wIndex != 0x0004))
    {
        return kStatus_USB_InvalidRequest;
    }

    request->buffer = g_UsbDeviceMsOsCompatId;
    request->length = sizeof(g_UsbDeviceMsOsCompatId);
    return kStatus_USB_Success;
}

/*!
 * @brief USB device get string descriptor function.
 *
 * This function gets the string descriptor of the USB device.
 *
 * @param handle The USB device handle.
 * @param stringDescriptor Pointer to the string descriptor structure.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceGetStringDescriptor(usb_device_handle handle,
                                           usb_device_get_string_descriptor_struct_t *stringDescriptor)
{
    if ((stringDescriptor->stringIndex == USB_MS_OS_STRING_INDEX) && compositeSelected)
    {
        stringDescriptor->buffer = g_UsbDeviceStringMsOs;
        stringDescriptor->length = sizeof(g_UsbDeviceStringMsOs);
    }
    else if (stringDescriptor->stringIndex == 0U)
    {
        stringDescriptor->buffer = (uint8_t *)g_UsbDeviceLanguageList.languageString;
        stringDescriptor->length = g_UsbDeviceLanguageList.stringLength;
    }
    else
    {
        uint8_t languageId = 0U;
        uint8_t languageIndex = USB_DEVICE_STRING_COUNT;

        for (; languageId < USB_DEVICE_LANGUAGE_COUNT; languageId++)
        {
            if (stringDescriptor->languageId == g_UsbDeviceLanguageList.languageList[languageId].languageId)
            {
                if (stringDescriptor->stringIndex < USB_DEVICE_STRING_COUNT)
                {
                    languageIndex = stringDescriptor->stringIndex;
                }
                break;
            }
        }

        if (USB_DEVICE_STRING_COUNT == languageIndex)
        {
            return kStatus_USB_InvalidRequest;
        }
        stringDescriptor->buffer = (uint8_t *)g_UsbDeviceLanguageList.languageList[languageId].string[languageIndex];
        stringDescriptor->length = g_UsbDeviceLanguageList.languageList[languageId].length[languageIndex];
    }
    return kStatus_USB_Success;
}

/*!
 * @brief USB device set speed function.
 *
 * This function sets the speed of the USB device.
 *
 * Due to the difference of HS and FS descriptors, the device descriptors and configurations need to be updated to match
 * current speed.
 * As the default, the device descriptors and configurations are configured by using FS parameters for both EHCI and
 * KHCI.
 * When the EHCI is enabled, the application needs to call this function to update device by using current speed.
 * The updated information includes endpoint max packet size, endpoint interval, etc.
 *
 * @param handle The USB device handle.
 * @param speed Speed type. USB_SPEED_HIGH/USB_SPEED_FULL/USB_SPEED_LOW.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceSetSpeed(usb_device_handle handle, uint8_t speed)
{
    usb_descriptor_union_t *ptr1;
    usb_descriptor_union_t *ptr2;

    ptr1 = (usb_descriptor_union_t *)(&g_UsbDeviceConfigurationDescriptor[0]);
    ptr2 = (usb_descriptor_union_t *)(&g_UsbDeviceConfigurationDescriptor[USB_DESCRIPTOR_LENGTH_CONFIGURATION_ALL - 1]);

    while (ptr1 < ptr2)
    {
        if (ptr1->common.bDescriptorType == USB_DESCRIPTOR_TYPE_ENDPOINT)
        {
            if (USB_SPEED_HIGH == speed)
            {
                if (((ptr1->endpoint.bEndpointAddress & USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK) ==
                     USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_IN) &&
                     (USB_CDC_VCOM_INTERRUPT_IN_ENDPOINT == (ptr1->endpoint.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK)))
                {
                    ptr1->endpoint.bInterval = HS_CDC_VCOM_INTERRUPT_IN_INTERVAL;
                    USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(HS_CDC_VCOM_INTERRUPT_IN_PACKET_SIZE,
                                                       ptr1->endpoint.wMaxPacketSize);
                }
                else if (((ptr1->endpoint.bEndpointAddress & USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK) ==
                     USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_IN) &&
                         (USB_CDC_VCOM_BULK_IN_ENDPOINT == (ptr1->endpoint.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK)))
                {
                    USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(HS_CDC_VCOM_BULK_IN_PACKET_SIZE, ptr1->endpoint.wMaxPacketSize);
                }
                else if (((ptr1->endpoint.bEndpointAddress & USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK) ==
                     USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_OUT) &&
                         (USB_CDC_VCOM_BULK_OUT_ENDPOINT == (ptr1->endpoint.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK)))
                {
                    USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(HS_CDC_VCOM_BULK_OUT_PACKET_SIZE, ptr1->endpoint.wMaxPacketSize);
                }
            }
            else
            {
                if (((ptr1->endpoint.bEndpointAddress & USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK) ==
                     USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_IN) &&
                      (USB_CDC_VCOM_INTERRUPT_IN_ENDPOINT == (ptr1->endpoint.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK)))
                {
                    ptr1->endpoint.bInterval = FS_CDC_VCOM_INTERRUPT_IN_INTERVAL;
                    USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(FS_CDC_VCOM_INTERRUPT_IN_PACKET_SIZE,
                                                       ptr1->endpoint.wMaxPacketSize);
                }
                else if (((ptr1->endpoint.bEndpointAddress & USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK) ==
                     USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_IN) &&
                      (USB_CDC_VCOM_BULK_IN_ENDPOINT == (ptr1->endpoint.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK)))
                {
                    USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(FS_CDC_VCOM_BULK_IN_PACKET_SIZE, ptr1->endpoint.wMaxPacketSize);
                }
                else if (((ptr1->endpoint.bEndpointAddress & USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK) ==
                     USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_OUT) &&
                    (USB_CDC_VCOM_BULK_OUT_ENDPOINT == (ptr1->endpoint.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK)))
                {
                    USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(FS_CDC_VCOM_BULK_OUT_PACKET_SIZE, ptr1->endpoint.wMaxPacketSize);
                }
            }
        }
        ptr1 = (usb_descriptor_union_t *)((uint8_t *)ptr1 + ptr1->common.bLength);
    }

    for (int i = 0; i < USB_CDC_VCOM_ENDPOINT_CIC_COUNT; i++)
    {
        if (USB_SPEED_HIGH == speed)
        {
            g_UsbDeviceCdcVcomCicEndpoints[i].maxPacketSize = HS_CDC_VCOM_INTERRUPT_IN_PACKET_SIZE;
        }
        else
        {
            g_UsbDeviceCdcVcomCicEndpoints[i].maxPacketSize = FS_CDC_VCOM_INTERRUPT_IN_PACKET_SIZE;
        }
    }
    for (int i = 0; i < USB_CDC_VCOM_ENDPOINT_DIC_COUNT; i++)
    {
        if (USB_SPEED_HIGH == speed)
        {
            if (g_UsbDeviceCdcVcomDicEndpoints[i].endpointAddress & USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK)
            {
                g_UsbDeviceCdcVcomDicEndpoints[i].maxPacketSize = HS_CDC_VCOM_BULK_IN_PACKET_SIZE;
            }
            else
            {
                g_UsbDeviceCdcVcomDicEndpoints[i].maxPacketSize = HS_CDC_VCOM_BULK_OUT_PACKET_SIZE;
            }
        }
        else
        {
            if (g_UsbDeviceCdcVcomDicEndpoints[i].endpointAddress & USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK)
            {
                g_UsbDeviceCdcVcomDicEndpoints[i].maxPacketSize = FS_CDC_VCOM_BULK_IN_PACKET_SIZE;
            }
            else
            {
                g_UsbDeviceCdcVcomDicEndpoints[i].maxPacketSize = FS_CDC_VCOM_BULK_OUT_PACKET_SIZE;
            }
        }
    }

    return kStatus_USB_Success;
}
