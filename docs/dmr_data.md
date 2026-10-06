# DMR packet data and signalling

OpenGD77 can send and receive DMR packet data (ETSI TS 102 361-1 clause 8) and arbitrary signalling bursts as well as
voice: on its own (normal mode), as an MMDVM hotspot, and as a USB network adapter. This page lists what each
protocol supports.

| Radio | Tree | Notes |
| --- | --- | --- |
| GD-77, GD-77S, DM-1801, RD-5R (MK22) | `OpenGD77/firmware` | USB serial and the network adapter together (composite device) |
| MD-UV380, DM-1701, RT-84 (STM32F405) | `OpenGD77-MDUV380/MDUV380_firmware` | USB serial *or* the network adapter, chosen in the menu |

| Piece | Where (MK22 tree; the STM32 tree has the same files under `application/`) |
| --- | --- |
| Packet layer: data headers, Rate 1/2 blocks, CRC-32, preamble CSBKs, IPv4/UDP, Motorola TMS text and ACKs, ETSI response ACK; RX reassembly including confirmed data and compressed UDP/IP headers | `source/functions/dmrData.c` (no hardware dependencies) |
| Burst engine: one burst per slot on the current timeslot, DMO or through the repeater wakeup, RX classification of data bursts | `source/hardware/HR-C6000.c` (`HRC6000DataTxStart`, `DMR_STATE_DATA_TX_*`) |
| Service: SMS inbox, automatic TMS / confirmed data ACKs, USB `D` commands, delivery to the network adapter | `source/functions/dmrDataService.c` |
| Hotspot passthrough between MMDVMHost and RF | `source/hotspot/uiHotspot.c` (STM32: `source/functions/hotspot.c`), `source/hotspot/dmrDataFrame.c` |
| Network gateway: Ethernet, ARP, IPv4, ICMP, DHCP, UDP | `source/functions/ipGateway.c` (no hardware dependencies) |
| CDC-NCM USB function | `source/usb/usb_ncm.c` (MK22: KSDK class; STM32: ST USB device class) |

## Over the air (DMR)

### Burst types

Bursts are sent with the HR-C6000 `CRC_MCU_Control` bit (register 0x40 bit 3) set, so the chip sends the 96 info
bits exactly as given (it still does the FEC, slot type and sync). Any CSBK, MBC, data header or block can be sent
this way, not only the ones the firmware builds itself, and bad CRCs go out as given.

| Burst (DT) | TX, normal mode (`D` commands, network adapter) | RX, normal mode | Hotspot, MMDVMHost → RF | Hotspot, RF → MMDVMHost |
| --- | --- | --- | --- | --- |
| PI header (0) | - | - | refused (NAK 4) | - |
| Voice LC header, terminator (1, 2) | voice path | voice path | voice path | voice path |
| CSBK (3), any opcode / FID | yes | yes (bad CRC dropped) | yes | yes (bad CRC dropped) |
| MBC header, continuation (4, 5) | yes | yes (bad CRC dropped) | yes | yes (bad CRC dropped) |
| Data header (6), any DPF | yes | yes (bad CRC dropped) | yes | yes (bad CRC dropped) |
| Rate 1/2 block (7) | yes | yes, also with a CRC error | yes | yes, also with a CRC error |
| Rate 3/4 block (8), 18 bytes | yes (`D` send kinds 1-3 with rate flag 1, raw bursts) | yes, also with a CRC error (the HR-C6000 decodes the Trellis code) | yes (Trellis decoded, single and double bit errors corrected; DM-1701 on air) | yes (Trellis coded), also with a CRC error |
| Idle (9) | - | - | refused (NAK 4) | - |
| Rate 1 block (10), 24 bytes | yes (`D` send kinds 1-3 with rate flag 2, raw bursts) | yes, also with a CRC error | yes (DM-1701 on air) | yes, also with a CRC error |
| USBD (11) | - | - | refused (NAK 4) | - |

- A transmission is at most 40 bursts (`DMR_DATA_MAX_BURSTS`), one per slot on the channel's timeslot. It waits for a
  busy channel to clear, exactly like a PTT press, and needs a digital channel that can transmit.
- The RX queue in the HR-C6000 driver is 8 bursts deep; the packet CRC catches anything lost.

### Packet formats (data header DPF)

| DPF | TX | RX |
| --- | --- | --- |
| Unconfirmed (2) | built by the radio (`D` send kinds 1-3 at any of the three rates, network adapter at Rate 1/2) | reassembled, CRC-32 checked |
| Confirmed (3) | raw bursts only (no retries or selective ACKs yet) | reassembled; to this radio, acknowledged with an ETSI response (ACK) |
| Response (1) | sent as the ACK of confirmed data | passed on as bursts |
| UDT (0), defined short (13), raw short (14), proprietary (15) | raw bursts | passed on as bursts (hotspot: the burst count is taken from the header) |

### Payloads (SAP)

| SAP | TX | RX |
| --- | --- | --- |
| IP (4), IPv4 + UDP, uncompressed | yes | yes |
| Compressed UDP/IP (3) | no | yes (port IDs 1 ETSI text, 98 Motorola TMS) |
| Anything else | `D` send kind 3 (any DPF and SAP) or raw bursts | as bursts |

Over the air IP packets use the Motorola CAI addresses: radio IDs are `12.x.y.z`, talkgroups `225.x.y.z`. This is
what Brandmeister, Motorola, TYT MD-380 (CPS "compressed UDP data header" set to None) and Anytone (SMS format set to
Motorola) use.

### Applications

| Application | Support |
| --- | --- |
| Motorola TMS text (UDP 4007) | TX with or without an ACK request, unconfirmed Rate 1/2, two preamble CSBKs. RX into a 4 message inbox, UTF-16LE text shown as Latin-1; requested ACKs are sent automatically |
| LRRP location (UDP 4001), ARS registration (UDP 4005) | carried as UDP (network adapter, `D` UDP send), not decoded by the radio |
| ETSI defined short data, Hytera text | not decoded |

## USB serial: `D` commands

On the serial port in CPS mode (on the STM32 radios also over UDP port 3334 on the network adapter, outside hotspot mode). Each request is
one USB packet (at most 64 bytes): `'D'`, sub command, arguments. Every reply starts with `'D'`, the sub command and a
result byte (1 = OK, 0 = failed / busy / nothing available).

| Sub | Request arguments | Reply after the result byte |
| --- | --- | --- |
| 1 Clear | | |
| 2 Append bytes | len, bytes (up to 61) | |
| 3 Append burst | dataType, flags (1 = payload holds its CRC), len (up to 24), payload | |
| 4 Send | kind, flags, dst (3 bytes, big endian), port / dpf+sap (2 bytes) | |
| 5 Status | | rx bursts waiting, inbox count (the result byte is the TX status: 0 idle, 1 running, 2 done, 3 failed) |
| 6 Pop RX burst | | dataType, flags (2 = HR-C6000 CRC error), len, payload |
| 7 Pop message | | src (3), dst (3), group, len, text (Latin-1) |
| 8 Network mode | `1` on, `0` off, `0xFF` query | saved mode, network link up |

Send kinds: 0 the appended bursts as they are, 1 TMS text from the appended bytes, 2 UDP datagram from the appended
bytes to the port, 3 unconfirmed packet from the appended bytes with DPF and SAP in the two port bytes. Send flags:
1 = group destination, 2 = request a TMS ACK, bits 2-3 the block rate of kinds 1-3 (0 Rate 1/2, 1 Rate 3/4,
2 Rate 1; 3 is refused). Kind 0 sends the appended bursts as they are, at whatever rate they were built.

The RX burst queue holds the last 8 data bursts received (oldest dropped). Sub command 8 saves with the other
settings (e.g. the CPS save and reboot command `C 6 0`) and takes effect at the next boot; the STM32 radios also
switch from the menu.

## MMDVM (hotspot mode)

Over the serial port, or on the STM32 radios over UDP port 3334 on the network adapter (MMDVMHost `Protocol=udp`). The hotspot is simplex
(DMO) and uses `DMR_DATA2` frames both ways.

- Voice: the host's terminator ends the transmission once the buffered voice is out (then the radio sends its own
  terminator); without one, 360 ms without voice ends it. A call shorter than the buffering minimum starts on its
  terminator. On the STM32 radios voice starts straight after the three LC headers (no startup silence).
- Data: MMDVMHost frames with data sync and the types marked "yes" in the burst table go out on RF bit for bit (the
  info bits are taken from the 33 byte frame by BPTC, Trellis or Rate 1 decoding, so the frame's CRCs are kept).
  Bursts are collected into a list (`hotspotData.c`). A list goes out when it holds as many bursts as its data header
  announced (else its preamble CSBKs or MBC), or 180 ms after its last frame, and only from idle receive. While one is
  on air the next one fills the rest of the 40 burst array. Types the radio can't send, and frames that don't fit,
  are NAKed (4 and 5) instead of being acknowledged and dropped.
- Data bursts received on RF go to MMDVMHost as 33 byte `DMR_DATA2` frames with the radio's colour code and RSSI:
  CSBK, MBC, data headers (with a good CRC) and Rate 1/2, Rate 3/4 and Rate 1 blocks (also with a CRC error).
- GET_STATUS: transmitting (0x01) from buffering until the radio's last burst is on air, and while data is queued or
  on air; carrier detect (0x40) from the receiver's noise level, as for the squelch (so analog signals and other
  colour codes count), while not transmitting; the DMR space is the smaller of the voice buffer and the data list.
- `D` commands and the network adapter's own data path don't run in hotspot mode.

## USB network adapter (CDC-NCM)

### USB

| | MK22 | STM32F405 |
| --- | --- | --- |
| Turned on by | `BIT_USB_NETWORK` (`D` sub command 8), at the next boot | Menu > Options > General > USB: Serial / Network (straight away), also `BIT_USB_NETWORK` |
| Device | composite VID 0x1FC9 PID 0x0095: serial port (interfaces 0-1) + CDC-NCM (interfaces 2-3) | VID 0x1FC9 PID 0x0096: CDC-NCM alone (with an IAD); the serial protocol goes over UDP port 3334 |
| Why | | the OTG_FS controller has too few endpoints for both |

- NCM with 16 bit NTBs, one datagram per NTB, padded so that no zero length packet is needed. The link is announced
  (NetworkConnection) when the host configures the device, which macOS waits for.
- Host drivers: Linux `cdc_ncm`, macOS built in, Windows 11 UsbNcm by class. Windows 10 gets the `WINNCM` compatible ID
  from Microsoft OS 1.0 descriptors (string 0xEE "MSFT100", vendor code 0x47, Extended Compat ID), which binds its
  UsbNcm driver; on the MK22 the serial port binds to the built in usbser driver and gets a new COM port number. The
  Linux udev rules include PID 0x0095 and tell ModemManager to leave the radio alone.

### Addressing

The radio runs a tiny gateway (no TCP/IP stack). The address ranges are set at compile time in `ipGateway.h` (each a
base and a /8 to /24 prefix; the low bits of an address are the DMR ID):

| Range | Default | Use |
| --- | --- | --- |
| Individual | 11.0.0.0/8 | DMR IDs. DHCP gives the host its own radio's ID: radio 10005 gives 11.0.39.21 (10.250.39.21 with 10.250.0.0/16) |
| Multicast | 225.0.0.0/8 | Talkgroups both ways: send to 225.x.y.z, and group data received over the air arrives there (join the group on the radio's interface) |
| Link | /32 | The host alone; the radio is 11.<ID> xor 1 (11.0.39.20 for radio 10005), on the link by an option 121 route with router 0.0.0.0 |
| Group (optional) | none | A unicast range for sending to talkgroups, e.g. 10.251.0.0/16 next to 10.250.0.0/16, for hosts that ignore the multicast route |

- IDs bigger than a range are truncated to its low bits. In a /16, radio 0x010203 gets 10.250.2.3, and its host also
  gets the data sent over the air to radio 0x0203; sending to 10.250.2.3 reaches radio 0x0203. Overlaps are possible
  but unlikely, since high IDs are sparse. The radio itself still only acknowledges data to its full ID.
- Each radio has an address of its own, so several radios can be plugged into one host and each one's UDP port 3334
  is reachable. They all route the same ranges though, so the host sends all its DMR traffic through one of them
  (bind to a radio's interface to choose).
- The radio's address is also the address of radio ID xor 1. The radio itself only answers ping, DHCP and UDP port
  3334 there; anything else sent to it goes over the air to radio ID xor 1, and data from that radio arrives from it.
  The radio's address is the DHCP server identifier.
- `-DIPGW_LINK_PREFIX=31` makes the host and the radio a /31 instead (the radio answers ARP for its own address
  only). macOS treats both ends of a /31 as broadcast addresses and refuses a plain send to the radio (it needs
  SO_BROADCAST), so the /32 is the default.
- `-DIPGW_LINK_PREFIX=8` (any prefix up to the individual one) gives the older shared link instead, for hosts that
  can't use either: every radio is on the link, the radio is 11.255.255.254 (that ID can't be used), and only the
  multicast range (and an off link group range) is routed. One radio per host.

### Protocols

| Protocol | Support |
| --- | --- |
| Ethernet | unicast, broadcast and IPv4 multicast frames up to 600 bytes |
| ARP | answers for every address but the host's (on a /31 only for the radio's own address) |
| IPv4 | no fragments (fragments are dropped); datagrams over ~450 bytes can't go over the air |
| ICMP | echo (ping) of the radio's address |
| DHCP | one lease, the host's address from the radio's DMR ID; options 53, 54, 51 (10 minutes), 1, 121 and 249 (same routes), no default route. A renewal after the radio's DMR ID changed is refused (NAK), so the host gets the new address |
| UDP, host → air | from the host's own address only (never anything a host forwards). Individual address → private data to that ID; 225.x.y.z (or the group range) → group data to that talkgroup; the all call (the top individual address 11.255.255.255, 255.255.255.255, or the subnet broadcast on a shared link) → group data to 16777215, for UDP ports 4000-4099 only, so the host's own broadcasts (NetBIOS, discovery, LAN sync) never key the radio. Sent as unconfirmed IPv4/UDP (SAP 4) with the CAI addresses |
| UDP, air → host | every UDP packet received over the air (SAP 4 or compressed SAP 3), from the source's individual address: to the host's address, to 225.x.y.z (multicast MAC) for talkgroups, to 255.255.255.255 (subnet broadcast on a shared link) for the all call. Data between two other radios goes to a MAC address the host doesn't have, so the host ignores it but Wireshark (promiscuous) shows it |
| UDP 40077, monitor | every data burst received (CSBKs, MBC and headers with a good CRC, blocks also with a CRC error) as a UDP broadcast from the radio: version (1), timeslot (1-2), colour code, DT, flags, length, payload |
| UDP 3334, serial protocol (STM32) | datagrams to the radio's address are handled exactly like bytes on the serial port, replies go back to the address and port that last sent: MMDVM (hotspot mode) and the `D` commands. One client at a time |
| TCP, IPv6 | no |

MMDVMHost over the network adapter (STM32 radios), for a radio whose host got 11.0.39.21:

```
[Modem]
Protocol=udp
ModemAddress=11.0.39.20    # the radio: the host's address xor 1
ModemPort=3334
LocalAddress=11.0.39.21    # the address the host got by DHCP (its radio's DMR ID)
LocalPort=3335
```

The CPS needs the serial port (on the STM32 radios, serial mode).

## Tested

| | Status |
| --- | --- |
| Packet layer, gateway | host tests (below), bursts checked with RadioDesk's independent decoder, gateway frames with tshark |
| DM-1701: DMR voice TX/RX, serial `D` commands, CPS | on hardware |
| DM-1701 network adapter on macOS: DHCP (/32), routes, ping, `D` commands and MMDVM over UDP 3334 | on hardware |
| DM-1701: TMS sent from the host over the network adapter | on air, received and decoded by an SDR |
| DM-1701: data frames from MMDVM over UDP | on air bit for bit, received by an SDR |
| DM-1701: Rate 3/4 and Rate 1 packets from MMDVM over UDP (built by RadioDesk's encoder, all at once and paced, Rate 3/4 also with 1-2 bit errors per block) | on air bit for bit (errors corrected), CRC-32 good in RadioDesk's decoder |
| DM-1701: packets built by the radio at Rate 3/4 and Rate 1 (`D` send kinds 1-3) | on air, received by an MMDVM_HS: Rate 3/4 TMS 3 of 3 and UDP, Rate 1 raw packet, CRC-32 good; Rate 1 TMS 1 of 3 (single RF bit errors, Rate 1 has no FEC); rate 3 refused |
| DM-1701: data RX in normal mode, TMS from an MMDVM_HS (467.375 MHz) | Rate 1/2, Rate 3/4 and Rate 1 to its own ID, into the inbox; group TMS to the selected talkgroup taken, to another talkgroup ignored. Rate 1 needs a clean channel (one bit error loses the packet) |
| Data RX in hotspot mode, confirmed data, the network adapter on Windows and Linux, the MK22 network adapter | not yet on hardware |

## Host tests

`make -C firmware/tests` builds the packet layer, the hotspot data lists and the network gateway natively and runs
their tests. The packet test prints the bursts and the 33 byte frames (also Rate 3/4 and Rate 1) so that an independent
decoder can check them; RadioDesk's decoder agrees on every frame and corrects exactly the same two bit errors as the
Trellis decoder (421 of 560, hard decision; all single bit errors). The gateway test runs
with the default ranges (/32 link), with a /31 link, with a shared /8 link and with a /16 plan, and writes its frames
to pcaps that are checked with tshark (when Wireshark is installed).

## Not done yet

- Rate 3/4 or Rate 1 from the network adapter (it sends Rate 1/2).
- Confirmed data on TX (retries and selective ACKs). Confirmed data on RX is acknowledged.
- SMS compose / inbox screens, storing messages in flash.
- ETSI defined short data and Hytera text formats.
- A Wireshark dissector for the monitor records (UDP 40077).
