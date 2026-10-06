# OpenGD77
The OpenGD77 firmware releases R20260131 for both MCU families, as published on opengd77.com, side by side:

| Folder | Radios | Release README |
| --- | --- | --- |
| `firmware/` | NXP MK22: Radioddity GD-77 and GD-77S, TYT MD-760 and MD-730, Baofeng DM-1801, DM-1801A, DM-860, RD-5R and DM-5R Tier2 (`OPENGD77_20260131.zip`) | [README.GD77.md](README.GD77.md) |
| `MDUV380_firmware/` | STM32F405: TYT MD-UV380 / Retevis RT-3S, Baofeng DM-1701 / Retevis RT-84 and others (`OpenGD77_MDUV380_DM1701_20260130.zip`) | [README.MDUV380.md](README.MDUV380.md) |

Both trees are unchanged. Where the two releases had a file of the same name at the top level, both are kept with
the family in the name (`prepare_GD77`, `prepare_MDUV380`, `tools/package_bin_with_languages/package_GD77.bat`, ...);
`prepare` and `prepare.bat` run both releases' scripts. `license.txt` is the same in both releases.

The other top-level folders (ComTool, Linux, OpenGD77CommDriver, the older tools, `BUILD.md`, `LICENCE`) come from
open-ham/OpenGD77, the 2022 MK22 code this repository started from.
