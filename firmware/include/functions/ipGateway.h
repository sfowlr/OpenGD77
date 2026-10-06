/*
 * Minimal IPv4 gateway between a USB network link and DMR packet data
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

#ifndef _OPENGD77_IPGATEWAY_H_
#define _OPENGD77_IPGATEWAY_H_

#include <stdbool.h>
#include <stdint.h>

// Addressing, chosen at compile time. Each range is a base address and a prefix length of 8 to 24 bits, and maps the
// low bits of the address to a DMR ID. Bigger IDs are truncated: in a /16 radio 0x010203 is x.y.2.3, and its host also
// gets the data sent to radio 0x0203. Sending from the host to x.y.2.3 reaches 0x0203 only.
//   individual   11.0.0.0/8 (default)  DMR IDs. The host gets its own radio's ID by DHCP, so radio 10005 gives it
//                                      11.0.39.21 (or 10.250.39.21 with 10.250.0.0/16). Over the air the packets keep
//                                      the Motorola CAI addresses (12.x.y.z, 225.x.y.z), see dmrData.c
//   multicast    225.0.0.0/8           talkgroups, both ways: send to 225.x.y.z for talkgroup x.y.z, and group data
//                                      received over the air arrives there (join the group on the radio's interface)
//   link         11.0.0.0/8            the subnet the host gets, it must hold the individual range. Option 121 adds the
//                                      route for the multicast range (and the group range, if any, when it is off link)
//   group        none (optional)       a unicast range for sending to talkgroups, for hosts or apps that can't use the
//                                      multicast route, e.g. 10.251.0.0/16 next to 10.250.0.0/16 with a /15 link.
//                                      Define IPGW_GROUP_NET and IPGW_GROUP_PREFIX to have it.
// The subnet broadcast address is the all call (16777215), for the DMR application ports 4000-4099 only. The radio
// itself is the top individual address but one (11.255.255.254), so that DMR ID can't be used.
#ifndef IPGW_INDIVIDUAL_NET
#define IPGW_INDIVIDUAL_NET		0x0B000000u		// 11.0.0.0
#define IPGW_INDIVIDUAL_PREFIX	8
#endif
#ifndef IPGW_MULTICAST_NET
#define IPGW_MULTICAST_NET		0xE1000000u		// 225.0.0.0
#define IPGW_MULTICAST_PREFIX	8
#endif
#ifndef IPGW_LINK_PREFIX
#define IPGW_LINK_PREFIX		8
#endif

#define IPGW_MASK(prefix)		(0xFFFFFFFFu << (32 - (prefix)))
#define IPGW_NETMASK			IPGW_MASK(IPGW_LINK_PREFIX)
#define IPGW_GATEWAY_IP			(IPGW_INDIVIDUAL_NET | (~IPGW_MASK(IPGW_INDIVIDUAL_PREFIX) - 1))
#define IPGW_BROADCAST_IP		((IPGW_INDIVIDUAL_NET & IPGW_NETMASK) | ~IPGW_NETMASK)
#define IPGW_ALL_CALL_ID		0x00FFFFFFu

_Static_assert((IPGW_INDIVIDUAL_PREFIX >= 8) && (IPGW_INDIVIDUAL_PREFIX <= 24) &&
				(IPGW_MULTICAST_PREFIX >= 8) && (IPGW_MULTICAST_PREFIX <= 24), "address ranges must be /8 to /24");
#if defined(IPGW_GROUP_NET)
_Static_assert((IPGW_GROUP_PREFIX >= 8) && (IPGW_GROUP_PREFIX <= 24), "address ranges must be /8 to /24");
#endif
_Static_assert((IPGW_LINK_PREFIX >= 1) && (IPGW_LINK_PREFIX <= IPGW_INDIVIDUAL_PREFIX) &&
				((IPGW_GATEWAY_IP & IPGW_NETMASK) == (IPGW_INDIVIDUAL_NET & IPGW_NETMASK)), "the link must hold the individual range");
_Static_assert((IPGW_MULTICAST_NET >> 28) == 0xE, "the multicast range must be in 224.0.0.0/4");

// Every data burst received over the air also goes to the host, as a UDP broadcast from the radio to this port:
// version (1), timeslot (1-2), colour code, DT, flags (dmrBurst_t), length, payload.
#define IPGW_MONITOR_PORT	40077

// UDP to the radio itself on this port carries the radio's serial protocol, so that MMDVMHost (Modem Protocol=udp,
// ModemAddress = the radio, ModemPort = 3334) and the CPS style 'D' commands also work without a serial port
#define IPGW_SERIAL_PORT	3334

#define IPGW_MAX_FRAME		600				// largest Ethernet frame handled, bigger datagrams can't go over the air anyway

// No hardware dependencies, so the gateway can be unit tested on a host (see firmware/tests)
void ipGatewayInit(const uint8_t gatewayMac[6], const uint8_t hostMac[6]);

// An Ethernet frame from the host
void ipGatewayEthernetIn(const uint8_t *frame, int length);

// The host's address, from the radio's DMR ID, 0 if the ID can't be used
uint32_t ipGatewayHostIP(void);

// A UDP datagram received over the air, to the host from the source's individual address. Private data to another radio
// is sent to a MAC address the host doesn't use, so the host ignores it but a packet capture shows it. Group data goes
// to the multicast range. False if the USB IN endpoint is busy.
bool ipGatewayDeliverUDP(bool group, uint32_t dst, uint32_t src, uint16_t srcPort, uint16_t dstPort, const uint8_t *payload, int length);

// A monitor record (see IPGW_MONITOR_PORT) to the host
bool ipGatewayDeliverMonitor(const uint8_t *record, int length);

// Bytes from the radio's serial protocol, as one UDP datagram to the host and port that last sent to IPGW_SERIAL_PORT.
// False if the USB IN endpoint is busy; dropped (true) while no host has sent anything yet.
bool ipGatewaySerialOut(const uint8_t *data, int length);

// Provided by the user of the gateway
bool ipGatewaySendFrame(const uint8_t *frame, int length);
uint32_t ipGatewayRadioId(void);
void ipGatewaySerialIn(const uint8_t *data, int length);
bool ipGatewayToAir(bool group, uint32_t dst, uint16_t srcPort, uint16_t dstPort, const uint8_t *payload, int length);

#endif
