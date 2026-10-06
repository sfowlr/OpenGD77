##
## Command line build, equivalent to the MCUXpresso configurations in .cproject
##
##   export PATH=$ARM_TOOLCHAIN_PATH:$PATH
##   mkdir build && cd build && make -f ../Makefile -j8
##   make -f ../Makefile -j8 RADIO=DM1801 VERBOSE=1     (GD77, GD77S, DM1801, DM1801A, RD5R; JAPANESE=1)
##   make -f ../Makefile -j8 OPTIMIZE=size              (see OPTIMIZE below)
##
## As in .cproject, files are built with -O0 except the folders and files in OS_SOURCES, built with -Os.
## The codec sections are zero filled placeholders (../linkerdata/codec_bin_section_*.bin, made here if missing),
## which is enough to link and check the size but not to flash: the real sections come from the official firmware
## (see ../linkerdata/readme.txt).
##

RADIO ?= GD77
BUILD_DIR ?= ./bin
MKDIR_P ?= mkdir -p
BIN2SGL ?=

ifeq ("$(RADIO)","GD77")
	DEFINES := -DPLATFORM_GD77
	BIN2SGL_OPT :=
	SGL_FILE := OpenGD77.sgl
else ifeq ("$(RADIO)","GD77s")
	DEFINES := -DPLATFORM_GD77S
	BIN2SGL_OPT := -m GD-77S
	SGL_FILE := OpenGD77S.sgl
else ifeq ("$(RADIO)","GD77S")
	DEFINES := -DPLATFORM_GD77S
	BIN2SGL_OPT := -m GD-77S
	SGL_FILE := OpenGD77S.sgl
else ifeq ("$(RADIO)","DM1801")
	DEFINES := -DPLATFORM_DM1801
	BIN2SGL_OPT := -m DM-1801
	SGL_FILE := OpenDM1801.sgl
else ifeq ("$(RADIO)","DM1801A")
	DEFINES := -DPLATFORM_DM1801A
	BIN2SGL_OPT := -m DM-1801
	SGL_FILE := OpenDM1801A.sgl
else ifeq ("$(RADIO)","RD5R")
	DEFINES := -DPLATFORM_RD5R
	BIN2SGL_OPT := -m RD-5R
	SGL_FILE := OpenDM5R.sgl
else
$(error RADIO must be one of GD77, GD77S, DM1801, DM1801A, RD5R)
endif

ifeq ("$(JAPANESE)","1")
	DEFINES += -DLANGUAGE_BUILD_JAPANESE
endif

SRC := \
$(wildcard ../source/*.c)                   \
$(wildcard ../source/dmr_codec/*.c)         \
../source/dmr_codec/codec_bin.S             \
$(wildcard ../source/functions/*.c)         \
$(wildcard ../source/hardware/*.c)          \
$(wildcard ../source/interfaces/*.c)        \
$(wildcard ../source/io/*.c)                \
$(wildcard ../source/SeggerRTT/RTT/*.c)     \
$(wildcard ../source/SeggerRTT/Syscalls/*.c)\
$(wildcard ../source/usb/*.c)               \
$(wildcard ../source/user_interface/*.c)

DRIVERS_SRC := \
$(wildcard ../drivers/*.c)                     \
$(wildcard ../usb/device/class/*.c)            \
$(wildcard ../usb/device/source/*.c)           \
$(wildcard ../usb/device/source/khci/*.c)      \
$(wildcard ../osa/*.c)                         \
$(wildcard ../lists/*.c)

DEVICE_SRC := \
$(wildcard ../board/*.c)      \
$(wildcard ../device/*.c)     \
$(wildcard ../startup/*.c)

OS_SRC := \
$(wildcard ../amazon-freertos/FreeRTOS/*.c)                 \
$(filter-out %/heap_1.c %/heap_2.c %/heap_3.c %/heap_5.c, $(wildcard ../amazon-freertos/FreeRTOS/portable/*.c))

ALL_SRC := $(SRC) $(OS_SRC) $(DEVICE_SRC) $(DRIVERS_SRC)

# The DMR packet data and network code shared with the STM32 tree (../../shared), built with -Os
SHARED_DIR := ../../shared
SHARED_SRC := $(wildcard $(SHARED_DIR)/source/*/*.c)

## OPTIMIZE=cproject (the default): -O0 except OS_SOURCES, as the MCUXpresso build. That build links NXP's Redlib,
## which isn't part of the GNU Arm toolchain; with newlib-nano the release doesn't fit before the codec, so
## OPTIMIZE=size builds everything with -Os except the files that the earlier releases kept at -O0 (timing).
OPTIMIZE ?= cproject
O0_SOURCES := \
../drivers/fsl_dspi.c ../source/SeggerRTT/RTT/SEGGER_RTT.c ../source/SeggerRTT/RTT/SEGGER_RTT_printf.c         \
../source/SeggerRTT/Syscalls/SEGGER_RTT_Syscalls_GCC.c ../source/firmware.c ../source/functions/sound.c         \
../source/hardware/SPI_Flash.c ../source/hardware/UC1701_transfer.c ../source/main.c ../source/semihost_hardfault.c

# Built with -Os, from .cproject (GD-77 configuration)
OS_SOURCES := \
$(wildcard ../usb/device/class/*.c) $(wildcard ../usb/device/source/*.c) $(wildcard ../usb/device/source/khci/*.c) \
$(OS_SRC) $(wildcard ../source/usb/*.c) $(wildcard ../source/user_interface/*.c) $(wildcard ../device/*.c)         \
$(wildcard ../board/*.c)                                                                                          \
$(addprefix ../source/interfaces/, adc.c i2c.c dac.c spi.c pit.c wdog.c i2s.c gps.c)                              \
$(addprefix ../source/functions/, aprs.c satellite.c codeplug.c rxPowerSaving.c calibration.c trx.c vox.c         \
                                  hotspot.c settings.c voicePrompts.c ticks.c)                                    \
../source/dmr_codec/codec.c                                                                                       \
$(addprefix ../source/io/, LEDs.c keyboard.c buttons.c display.c rotary_switch.c)                                 \
$(addprefix ../source/hardware/, HR-C6000.c AT1846S.c UC1701.c EEPROM.c)                                          \
$(addprefix ../drivers/, fsl_common.c fsl_sai_edma.c fsl_i2c.c fsl_ftm.c fsl_clock.c fsl_gpio.c fsl_adc16.c       \
                         fsl_dac.c fsl_smc.c fsl_wdog.c fsl_dmamux.c fsl_pit.c fsl_edma.c fsl_sai.c)             

INCLUDE_DIRS := \
-I$(SHARED_DIR)/include \
-I../board -I../include -I../include/usb -I../source -I../ -I../amazon-freertos/include          \
-I../amazon-freertos/FreeRTOS/portable -I../drivers -I../device -I../CMSIS -I../usb/device/class \
-I../usb/device/source -I../usb/device/include -I../usb/device/source/khci                       \
-I../usb/device/source/lpcip3511 -I../usb/include -I../osa -I../lists

DEFINES += \
-DGITVERSION=\"`git rev-parse --short HEAD || echo UNKNOWN`\" \
-DSDK_DEBUGCONSOLE=0     \
-DCR_INTEGER_PRINTF      \
-DPRINTF_FLOAT_ENABLE=0  \
-DFSL_RTOS_FREE_RTOS     \
-DSDK_OS_FREE_RTOS       \
-DCPU_MK22FN512VLL12     \
-DCPU_MK22FN512VLL12_cm4 \
-D__MCUXPRESSO           \
-D__USE_CMSIS            \
-DNDEBUG                 \
-D__NEWLIB_H__

ARCH     := -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb
CFLAGS   := $(INCLUDE_DIRS) $(ARCH) $(DEFINES) $(EXTRA_CFLAGS) -std=gnu99 -fno-common -gdwarf-4 -g3 -Wall \
            -ffunction-sections -fdata-sections -ffreestanding -fno-builtin -fmerge-constants -c
AFLAGS   := $(ARCH)
LFLAGS   := -nostdlib -Xlinker -Map="$(BUILD_DIR)/firmware.map" -Xlinker --gc-sections -Xlinker -print-memory-usage \
            -Xlinker --sort-section=alignment $(ARCH) -T ../linkerscripts/firmware_newlib.ld
DFLAGS   := -MMD -MP

CODEC_SECTIONS := ../linkerdata/codec_bin_section_1.bin ../linkerdata/codec_bin_section_2.bin
CODEC_ADDRESS  := 0x54000

ifeq ("$(VERBOSE)","1")
Q :=
ECHO := @true
else
Q := @
ECHO := @echo
endif

SHARED_OBJ := $(patsubst $(SHARED_DIR)/%.c, $(BUILD_DIR)/shared/%.o, $(SHARED_SRC))
OBJ := $(addprefix $(BUILD_DIR)/, $(addsuffix .o, $(basename $(ALL_SRC)))) $(SHARED_OBJ)
ifeq ("$(OPTIMIZE)","size")
OS_OBJ := $(addprefix $(BUILD_DIR)/, $(addsuffix .o, $(basename $(filter-out $(O0_SOURCES), $(filter %.c, $(ALL_SRC))))))
else
OS_OBJ := $(addprefix $(BUILD_DIR)/, $(addsuffix .o, $(basename $(OS_SOURCES))))
endif

CC  := arm-none-eabi-gcc
AS  := arm-none-eabi-as
CP  := arm-none-eabi-objcopy
SZ  := arm-none-eabi-size
OD  := arm-none-eabi-objdump

all: $(SGL_FILE)

$(SGL_FILE): $(BUILD_DIR)/firmware.axf
	$(ECHO) 'Performing post-build steps'
	$(Q)$(SZ) $<
	$(Q)end=$$($(OD) -h $< | awk '$$2 == ".data" { print $$3, $$5 }'); set -- $$end; \
	 echo "Flash free before the codec at $(CODEC_ADDRESS): $$(( $(CODEC_ADDRESS) - 0x$$1 - 0x$$2 )) bytes"
	$(Q)$(CP) -O binary $< $(BUILD_DIR)/firmware.bin
	$(Q)if [ -x "$(BIN2SGL)" ]; then "$(BIN2SGL)" $(BIN2SGL_OPT) $(BUILD_DIR)/firmware.bin && mv $(BUILD_DIR)/firmware.sgl $(BUILD_DIR)/$(SGL_FILE); \
	 else echo "No .sgl: set BIN2SGL to the bin2sgl tool (firmware.bin is built)"; fi

$(BUILD_DIR)/firmware.axf: $(OBJ)
	$(ECHO) "[LD ] firmware.axf"
	$(Q)$(CC) $(LFLAGS) -o $@ $(OBJ)

# Zero filled placeholders of the size of the official codec sections (../linkerdata/readme.txt)
../linkerdata/codec_bin_section_1.bin:
	$(Q)head -c 16384 /dev/zero > $@
../linkerdata/codec_bin_section_2.bin:
	$(Q)head -c 163984 /dev/zero > $@

$(BUILD_DIR)/../source/dmr_codec/codec_bin.o: $(CODEC_SECTIONS)

$(BUILD_DIR)/%.o: %.S
	$(Q)$(MKDIR_P) $(dir $@)
	$(ECHO) "[AS ] $<"
	$(Q)$(CC) $(DFLAGS) $(ARCH) -c $< -o $@

$(SHARED_OBJ): $(BUILD_DIR)/shared/%.o: $(SHARED_DIR)/%.c
	$(Q)$(MKDIR_P) $(dir $@)
	$(ECHO) "[CCs] $<"
	$(Q)$(CC) $(DFLAGS) $(CFLAGS) -Os $< -o $@

$(OS_OBJ): $(BUILD_DIR)/%.o: %.c
	$(Q)$(MKDIR_P) $(dir $@)
	$(ECHO) "[CCs] $<"
	$(Q)$(CC) $(DFLAGS) $(CFLAGS) -Os $< -o $@

$(BUILD_DIR)/%.o: %.c
	$(Q)$(MKDIR_P) $(dir $@)
	$(ECHO) "[CC0] $<"
	$(Q)$(CC) $(DFLAGS) $(CFLAGS) -O0 $< -o $@

# Rebuild what shows the version when the commit changes
$(BUILD_DIR)/gitversion: FORCE
	$(Q)$(MKDIR_P) $(dir $@)
	$(Q)v=$$(git rev-parse --short HEAD 2>/dev/null || echo UNKNOWN); [ "$$(cat $@ 2>/dev/null)" = "$$v" ] || echo $$v > $@
$(filter %/user_interface/menuFirmwareInfoScreen.o %/functions/hotspot.o %/usb/usb_com.o, $(OBJ)): $(BUILD_DIR)/gitversion
FORCE:

# No built-in suffix rules: on a case-insensitive file system (macOS) the built-in .S -> .s rule
# treats source/dmr_codec/codec_bin.S as an intermediate codec_bin.s and deletes it after the build
.SUFFIXES:
.SECONDARY:

-include $(OBJ:.o=.d)
