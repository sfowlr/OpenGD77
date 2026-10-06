/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : usb_device.c
  * @version        : v1.0_Cube
  * @brief          : This file implements the USB Device
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2020 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at:
  *                             www.st.com/SLA0044
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/

#include "usb_device.h"
#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_cdc.h"
#include "usbd_cdc_if.h"
#include "usb/usb_ncm.h"
#include "functions/ipGateway.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* USER CODE BEGIN PV */
/* Private variables ---------------------------------------------------------*/

/* USER CODE END PV */

/* USER CODE BEGIN PFP */
/* Private function prototypes -----------------------------------------------*/

/* USER CODE END PFP */

/* USB Device Core handle declaration. */
USBD_HandleTypeDef hUsbDeviceFS;

/*
 * -- Insert your variables declaration here --
 */
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/*
 * -- Insert your external function declaration here --
 */
/* USER CODE BEGIN 1 */
volatile bool usbNetworkMode = false;

// Locally administered MAC addresses from the MCU unique ID: 02:47:44 (G D) for the host, 06:47:44 for the radio
static void usbNetworkInit(void)
{
	const uint32_t *uidWords = (const uint32_t *)UID_BASE;
	uint32_t uid = uidWords[0] ^ uidWords[1] ^ uidWords[2];
	uint8_t hostMac[6] = { 0x02, 0x47, 0x44, (uid >> 16) & 0xFF, (uid >> 8) & 0xFF, uid & 0xFF };
	uint8_t gatewayMac[6] = { 0x06, 0x47, 0x44, (uid >> 16) & 0xFF, (uid >> 8) & 0xFF, uid & 0xFF };

	usbNcmSetMacAddress(hostMac);
	ipGatewayInit(gatewayMac, hostMac);
}

void usbDeviceSetNetworkMode(bool network)
{
	if (network != usbNetworkMode)
	{
		MX_USB_DEVICE_DeInit();
		usbNetworkMode = network;
		MX_USB_DEVICE_Init();
	}
}

void MX_USB_DEVICE_DeInit(void)
{
	if (USBD_Stop(&hUsbDeviceFS) != USBD_OK)
	{
		Error_Handler();
	}
#if 0
	if (USBD_DeInit(&hUsbDeviceFS) != USBD_OK)
	{
		Error_Handler();
	}
#endif
}
/* USER CODE END 1 */

/**
  * Init USB device Library, add supported class and start the library
  * @retval None
  */
void MX_USB_DEVICE_Init(void)
{
  /* USER CODE BEGIN USB_DEVICE_Init_PreTreatment */
  
  /* USER CODE END USB_DEVICE_Init_PreTreatment */

  /* Init Device Library, add supported class and start the library. */
  if (USBD_Init(&hUsbDeviceFS, &FS_Desc, DEVICE_FS) != USBD_OK)
  {
    Error_Handler();
  }
  USBD_FS_SetNetworkMode(usbNetworkMode);
  if (usbNetworkMode)
  {
    usbNetworkInit();
    if (USBD_RegisterClass(&hUsbDeviceFS, &USBD_NCM) != USBD_OK)
    {
      Error_Handler();
    }
  }
  else
  {
  if (USBD_RegisterClass(&hUsbDeviceFS, &USBD_CDC) != USBD_OK)
  {
    Error_Handler();
  }
  if (USBD_CDC_RegisterInterface(&hUsbDeviceFS, &USBD_Interface_fops_FS) != USBD_OK)
  {
    Error_Handler();
  }
  }
  if (USBD_Start(&hUsbDeviceFS) != USBD_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN USB_DEVICE_Init_PostTreatment */
  
  /* USER CODE END USB_DEVICE_Init_PostTreatment */
}

/**
  * @}
  */

/**
  * @}
  */

