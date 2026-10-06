# OpenGD77
Firmware for DMR transceivers using the STM32F405VGT MCU, AT1846S RF chip and HR-C6000 DMR chipset.  
Including the Radioddiy TYT MD-380UV / Retevis RT-3S and Baofeng DM-1701 / Retevis RT-84

# Project status

The firmware is relatively stable and provides DMR and FM audio transmission and reception, as well as a DMR hotspot mode.  
However it does not support some core functionality that the official firmware supports, including sending and receiving of text / SMS messages

The firmware source code does not contain a AMBE codec required for DMR operation.  
This functionality is provided by the official firmware which is merged with the OpenGD77 by the OpenGD77CPS or firmware loader


# Branches in this fork
This fork (sfowlr/OpenGD77) holds two lines of the firmware: the MK22 code from open-ham/OpenGD77, and this newer STM32
code, which the OpenGD77 developers release only as zip files on opengd77.com.

| Branch | Contents |
| --- | --- |
| `combined-main` | the two releases R20260131 side by side, unchanged (merge of `gd77-main` and `dm1701-main`) |
| `combined-dmr-packet-data` | `combined-main` plus the work, with the code both trees share in `shared/` (the main work branch) |
| `main` | open-ham/OpenGD77 unchanged: the 2022 MK22 code (GD-77, GD-77S, DM-1801, RD-5R) |
| `gd77-main` | `main` plus the MK22 release R20260131 (`OPENGD77_20260131.zip`), unchanged |
| `gd77-dmr-packet-data` | `gd77-main` plus DMR packet data, a USB network adapter (CDC-NCM) and MMDVM hotspot fixes |
| `dmr-packet-data` | the same work on the 2022 code (superseded by `gd77-dmr-packet-data`) |
| `dm1701-main` | the STM32 release R20260131 (MD-UV380 / RT-3S, DM-1701 / RT-84 and others), unchanged; its own history |
| `dm1701-dmr-packet-data` | `dm1701-main` plus the same work |

- Compare each work branch with its base: `gd77-dmr-packet-data` with `gd77-main`, `dmr-packet-data` with `main`,
  `dm1701-dmr-packet-data` with `dm1701-main`. The `dm1701-*` branches aren't connected to the others, so there are no
  pull requests between them.
- The DMR data and network sources are the same file for file in both lines (`dmrData`, `dmrDataService`,
  `ipGateway`, `hotspot/dmrDataFrame`, `hotspot/hotspotData`, `usb_ncm.h`); a change to one is made in both.
- What it adds, protocol by protocol: `docs/dmr_data.md`. The hotspot bug list and the status of each fix:
  `KNOWN_BUGS.md` (on `dmr-packet-data`).
- Tested: the STM32 line on a DM-1701, on air. The MK22 line builds and passes the host tests (`make -C firmware/tests`)
  but hasn't been run on a GD-77 yet.
- No branch contains the AMBE codec. On the STM32 line `codec_bin_section_1.bin` is a zero filled placeholder; the
  firmware loader merges the codec in from the official firmware.
- Building from the command line: `make -C MDUV380_firmware PLATFORM=DM1701` (see the comments at the top of
  `MDUV380_firmware/Makefile`).


# User guide

See https://github.com/LibreDMR/OpenGD77_UserGuide


# Credits
Originally conceived by Kai DG4KLU.  
Further development by Roger VK3KYY, latterly assisted by Daniel F1RMB, Alex DL4LEX, Colin G4EML and others.

Current lead developer and source code gatekeeper is Roger VK3KYY


# Copyright

 The firmware is copyright of the OpenGD77 developers. See individual source files for copyright information.

## MCU SDK and API code:   
   See license files in sub-folders
	
## FreeRTOS
   Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  
   All Rights Reserved.


# License

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions
are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived
   from this software without specific prior written permission.

4. Use of this source code or binary releases for commercial purposes is strictly forbidden. This includes, without limitation,
   incorporation in a commercial product or incorporation into a product or project which allows commercial use.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


# Special thanks

Thanks to those who have assisted the project including :

BD4VOW
CT1HSN
CT4TX 
DG3GSP
DG4KLU
DJ0HF
DL4LEX
EA3BIL
EA3IGM
EA5SW
EB3AM
EW1ADG
F1CXG
F1RMB
G4ELM
IK0NWG
IU4LEG
IZ2EIB
JE4SMQ
JG1UAA
OH1E
OK2HAD
ON1HK
ON7LDS
OZ1MAX
PU4RON
SQ6SFO
SQ7PTE
TA5AYX
VK3KYY
VK4JWT
VK7JS
VK7ZCR
VK7ZJA
ZL1XE
