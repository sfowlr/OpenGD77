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

## Host tests

`make -C firmware/tests` builds the packet layer natively and runs a round trip of every case, then prints the
bursts and the 33 byte frames so that they can be checked by an independent decoder.

## Not done yet

- Rate 3/4 (Trellis) and Rate 1 blocks in hotspot passthrough (RX on the radio itself handles them).
- Confirmed data on TX (retries and selective ACKs). Confirmed data on RX is acknowledged.
- SMS compose / inbox screens, storing messages in flash.
- ETSI defined short data and Hytera text formats.
