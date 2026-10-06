# OpenGD77
The OpenGD77 firmware releases R20260131 for both MCU families, as published on opengd77.com, side by side, with DMR
packet data, a USB network adapter (CDC-NCM) and MMDVM hotspot fixes added:

| Folder | What |
| --- | --- |
| `firmware/` | NXP MK22: Radioddity GD-77 and GD-77S, TYT MD-760 and MD-730, Baofeng DM-1801, DM-1801A, DM-860, RD-5R and DM-5R Tier2 (`OPENGD77_20260131.zip`, release README: [README.GD77.md](README.GD77.md)) |
| `MDUV380_firmware/` | STM32F405: TYT MD-UV380 / Retevis RT-3S, Baofeng DM-1701 / Retevis RT-84 and others (`OpenGD77_MDUV380_DM1701_20260130.zip`, release README: [README.MDUV380.md](README.MDUV380.md)) |
| `shared/` | the packet data, data service, network gateway and hotspot data code, which has no hardware dependencies and is built into both trees; `shared/tests` has its host tests |
| `docs/dmr_data.md` | what the work adds, protocol by protocol |
| `KNOWN_BUGS.md` | the hotspot bug list and the status of each fix |

Where the two releases had a file of the same name at the top level, both are kept with the family in the name
(`prepare_GD77`, `prepare_MDUV380`, `tools/package_bin_with_languages/package_GD77.bat`, ...); `prepare` and
`prepare.bat` run both releases' scripts. The other top-level folders (ComTool, Linux, OpenGD77CommDriver, the older
tools, `BUILD.md`, `LICENCE`) come from open-ham/OpenGD77, the 2022 MK22 code this repository started from.

# Building
- **STM32:** `make -C MDUV380_firmware PLATFORM=DM1701` (see the top of `MDUV380_firmware/Makefile`), or STM32CubeIDE.
- **MK22:** `cd firmware && mkdir -p build && cd build && make -f ../Makefile` (see the top of `firmware/Makefile`), or
  MCUXpresso. The release is built with MCUXpresso and NXP's Redlib; with the GNU Arm toolchain and newlib-nano it
  doesn't fit before the codec (about 25 KB over, the packet data work adds about 15 KB), so the command line build
  is for compiling and size checks only.
- **Host tests:** `make -C shared/tests`.
- Both IDE projects link `shared/` as a folder (`.project`) and have `shared/source` as a source folder and
  `shared/include` as an include path in every configuration (not yet tried in the IDEs).
- No branch contains the AMBE codec. The firmware loader merges it in from the official firmware.

Tested: the STM32 tree on a DM-1701, on air. The MK22 tree builds and passes the host tests but hasn't been run on a
radio.

# Branches in this fork

| Branch | Contents |
| --- | --- |
| `combined-main` | the two releases R20260131 side by side, unchanged (merge of `gd77-main` and `dm1701-main`) |
| `combined-dmr-packet-data` | `combined-main` plus the work, with the shared code in `shared/` (this branch) |
| `main` | open-ham/OpenGD77 unchanged: the 2022 MK22 code (GD-77, GD-77S, DM-1801, RD-5R) |
| `gd77-main` | `main` plus the MK22 release R20260131, unchanged |
| `gd77-dmr-packet-data` | `gd77-main` plus the work |
| `dmr-packet-data` | the work on the 2022 code (superseded) |
| `dm1701-main` | the STM32 release R20260131, unchanged; its own history |
| `dm1701-dmr-packet-data` | `dm1701-main` plus the work |

To bring in a new release, import each zip into `gd77-main` or `dm1701-main` (replacing that tree), merge them into
`combined-main`, and merge `combined-main` into `combined-dmr-packet-data`.
