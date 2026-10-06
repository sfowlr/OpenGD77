# DMR packet data and signalling

OpenGD77 can send and receive DMR packet data (ETSI TS 102 361-1 clause 8) as well as voice.

| Piece | Where |
| --- | --- |
| Packet layer: data headers, Rate 1/2 blocks, CRC-32, preamble CSBKs, IPv4/UDP, Motorola TMS text and ACKs, ETSI response ACK; RX reassembly including confirmed data and compressed UDP/IP headers | `firmware/source/functions/dmrData.c` (no hardware dependencies) |
| Burst engine: one burst per slot on the current timeslot, DMO or through the repeater wakeup, RX classification of data bursts | `firmware/source/hardware/HR-C6000.c` (`HRC6000DataTxStart`, `DMR_STATE_DATA_TX_*`) |
| Service: SMS inbox, automatic TMS / confirmed data ACKs, USB `D` commands | `firmware/source/functions/dmrDataService.c` |
| Hotspot passthrough: data frames from MMDVMHost go out on RF bit for bit, data bursts received on RF go to MMDVMHost | `firmware/source/hotspot/uiHotspot.c` (`hotspotDataTick`), `firmware/source/hotspot/dmrDataFrame.c` |

The bursts are sent with the HR-C6000 `CRC_MCU_Control` bit (register 0x40 bit 3) set, so the chip sends the
96 info bits exactly as given (it still does the BPTC coding, slot type and sync). Any CSBK, MBC, data header or
Rate 1/2 block can be sent this way, not only the ones the firmware builds itself.

## SMS format

Motorola TMS over IPv4/UDP port 4007, unconfirmed Rate 1/2, two preamble CSBKs. Radio IDs map to `12.x.y.z`, groups
to `225.x.y.z`. This is what Brandmeister, Motorola, TYT MD-380 (CPS "compressed UDP data header" set to None) and
Anytone (SMS format set to Motorola) use. ETSI defined short data and the Hytera format are not decoded yet.

## USB `D` commands (CPS mode)

Each request is one USB packet (at most 64 bytes): `'D'`, sub command, arguments. Every reply starts with
`'D'`, the sub command and a result byte (1 = OK, 0 = failed / busy / nothing available).

| Sub | Request arguments | Reply after the result byte |
| --- | --- | --- |
| 1 Clear | | |
| 2 Append bytes | len, bytes (up to 61) | |
| 3 Append burst | dataType, flags (1 = payload holds its CRC), len, payload | |
| 4 Send | kind, flags, dst (3 bytes, big endian), port / dpf+sap (2 bytes) | |
| 5 Status | | rx bursts waiting, inbox count (the result byte is the TX status: 0 idle, 1 running, 2 done, 3 failed) |
| 6 Pop RX burst | | dataType, flags (2 = HR-C6000 CRC error), len, payload |
| 7 Pop message | | src (3), dst (3), group, len, text (Latin-1) |

Send kinds: 0 the appended bursts as they are, 1 TMS text from the appended bytes, 2 UDP datagram from the appended
bytes to the port, 3 unconfirmed packet from the appended bytes with DPF and SAP in the two port bytes. Send flags:
1 = group destination, 2 = request a TMS ACK.

The radio must be on a digital channel that can transmit. The transmission waits for a busy channel to clear,
exactly like a PTT press.

## USB network adapter (CDC-NCM)

With the `BIT_USB_NETWORK` setting on, the radio enumerates (on the next boot) as a composite device, VID 0x1FC9
PID 0x0095: the usual serial port (interfaces 0-1, so CPS and MMDVMHost keep working) plus a CDC-NCM network
adapter (interfaces 2-3). Turn it on or off with USB `D` sub command 8 (`[1]` on, `[0]` off, `[0xFF]` query; the
reply is the saved mode and whether the network link is up), then save and reboot with the CPS command `C 6 0`.

The radio runs a tiny gateway (`firmware/source/functions/ipGateway.c`, no TCP/IP stack). The address ranges are set
at compile time in `ipGateway.h` (each a base and a /8 to /24 prefix; the low bits of an address are the DMR ID):

| Range | Default | Use |
| --- | --- | --- |
| Individual | 11.0.0.0/8 | DMR IDs. DHCP gives the host its own radio's ID: radio 10005 gives 11.0.39.21 (10.250.39.21 with 10.250.0.0/16) |
| Multicast | 225.0.0.0/8 | Talkgroups both ways: send to 225.x.y.z, and group data received over the air arrives there (join the group on the radio's interface) |
| Link | /31 | The host and the radio only: the host is 11.<ID>, the radio the other address of the pair (11.<ID> xor 1, 11.0.39.20 for radio 10005) |
| Group (optional) | none | A unicast range for sending to talkgroups, e.g. 10.251.0.0/16 next to 10.250.0.0/16, for hosts that ignore the multicast route |

- IDs bigger than a range are truncated to its low bits. In a /16, radio 0x010203 gets 10.250.2.3, and its host also
  gets the data sent over the air to radio 0x0203; sending to 10.250.2.3 reaches radio 0x0203. Overlaps are possible
  but unlikely, since high IDs are sparse. The radio itself still only acknowledges data to its full ID.
- Option 121/249 routes the individual range, the multicast range and the optional group range through the radio,
  with no default route, so the host's other traffic is unaffected. The radio answers ARP for its own address only.
- Each radio has an address of its own, so several radios can be plugged into one host and each one's UDP port 3334
  is reachable. They all route the same ranges though, so the host sends all its DMR traffic through one of them
  (bind to a radio's interface to choose).
- The radio's address is also the address of radio ID xor 1. The radio itself only answers ping, DHCP and UDP port
  3334 there; anything else sent to it goes over the air to radio ID xor 1, and data from that radio arrives from it.
- `-DIPGW_LINK_PREFIX=8` (any prefix up to the individual one) gives the older shared link instead, for hosts that
  can't use a /31 (Windows may not): every radio is on the link, the radio is 11.255.255.254 (that ID can't be used),
  the radio answers ARP for every address but the host's, and only the multicast range (and an off link group range)
  is routed. One radio per host.
- The top individual address (11.255.255.255) is the all call, as is 255.255.255.255 (and the subnet broadcast on a
  shared link), for UDP ports 4000-4099 only, so that the host's own broadcasts (NetBIOS, discovery, LAN sync) never
  key the radio. Received all calls and monitor records go to 255.255.255.255 (the subnet broadcast on a shared
  link). The gateway only sends what has the host's own source address over the air.
- The lease is 10 minutes. If the radio's DMR ID changes, the renewal is refused and the host gets the new address.
- Every IP packet received over the air goes to the host, from the source's individual address (over the air the
  packets use the Motorola CAI addresses 12.x.y.z / 225.x.y.z, the gateway translates). Data between two
  other radios goes to a MAC address the host doesn't have, so the host ignores it but Wireshark (promiscuous) shows
  it.
- Every data burst received (CSBKs, headers, blocks, also those with CRC errors) is also sent as a UDP broadcast
  from the radio to port 40077: version (1), timeslot (1-2), colour code, DT, flags, length, payload.
- ARP for any address but the host's, and ping of the radio. No fragments, datagrams over ~450 bytes are dropped.

Host drivers: Linux `cdc_ncm`, macOS built in, Windows 11 UsbNcm by class. Windows 10 gets the `WINNCM` compatible
ID from Microsoft OS 1.0 descriptors (string 0xEE "MSFT100", vendor code 0x47, Extended Compat ID), which binds
its UsbNcm driver; the serial port binds to the built in usbser driver and gets a new COM port number. The Linux
udev rules include PID 0x0095 and tell ModemManager to leave the radio alone.

## Host tests

`make -C firmware/tests` builds the packet layer and the network gateway natively and runs their tests. The packet
test prints the bursts and the 33 byte frames so that an independent decoder can check them; the gateway test
writes its frames to a pcap that is checked with tshark (when Wireshark is installed).

## Not done yet

- Rate 3/4 (Trellis) and Rate 1 blocks in hotspot passthrough (RX on the radio itself handles them).
- Confirmed data on TX (retries and selective ACKs). Confirmed data on RX is acknowledged.
- SMS compose / inbox screens, storing messages in flash.
- ETSI defined short data and Hytera text formats.
