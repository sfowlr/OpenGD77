## Command line build, equivalent to the STM32CubeIDE configurations in .cproject
##
##   make PLATFORM=DM1701      (DM1701, RT84, MDUV380, MDUV380_10W_PLUS; JAPANESE=1 for the Japanese builds)
##
## The codec section is a zero filled placeholder (application/source/linkerdata/codec_bin_section_1.bin, made here if
## missing); the firmware loader patches the real codec in from the official firmware when flashing.

PLATFORM ?= DM1701
JAPANESE ?=
BUILD    ?= build/$(PLATFORM)$(if $(JAPANESE),_JA)
PREFIX   ?= arm-none-eabi-

CC      := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
SIZE    := $(PREFIX)size
OBJDUMP := $(PREFIX)objdump

DEFS_DM1701           := -DPLATFORM_RT84_DM1701 -DPLATFORM_VARIANT_DM1701
DEFS_RT84             := -DPLATFORM_RT84_DM1701
DEFS_MDUV380          := -DPLATFORM_MDUV380
DEFS_MDUV380_10W_PLUS := -DPLATFORM_MDUV380 -DPLATFORM_VARIANT_UV380_PLUS_10W
ifeq ($(DEFS_$(PLATFORM)),)
$(error Unknown PLATFORM $(PLATFORM))
endif

GITVERSION := $(shell git rev-parse --short HEAD 2>/dev/null || echo UNKNOWN)

DEFS := -DUSE_HAL_DRIVER -DSTM32F405xx $(DEFS_$(PLATFORM)) -DNDEBUG -DDEBUG -DGITVERSION=\"$(GITVERSION)\" \
        $(if $(JAPANESE),-DLANGUAGE_BUILD_JAPANESE)

INCS := Middlewares/Third_Party/FreeRTOS/Source/include application/include \
        Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F USB_DEVICE/Target Drivers/CMSIS/Include Core/Inc \
        Drivers/STM32F4xx_HAL_Driver/Inc Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc USB_DEVICE/App \
        Drivers/CMSIS/Device/ST/STM32F4xx/Include Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 \
        Middlewares/ST/STM32_USB_Device_Library/Core/Inc Drivers/STM32F4xx_HAL_Driver/Inc/Legacy .

ARCH   := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
CFLAGS := $(ARCH) -std=gnu11 -Os -g3 -ffunction-sections -fdata-sections -fstack-usage -Wall \
          -Wno-format -Wno-format-truncation -Wno-stringop-overflow $(DEFS) $(addprefix -I,$(INCS))
ASFLAGS := $(ARCH) -g3 -x assembler-with-cpp $(DEFS) $(addprefix -I,$(INCS))
LDFLAGS := $(ARCH) -T STM32F405VGTX_FLASH.ld --specs=nosys.specs --specs=nano.specs -Wl,--gc-sections -static \
           -Wl,-Map=$(BUILD)/firmware.map -Wl,--start-group -lc -lm -Wl,--end-group

SRCDIRS := SeggerRTT application Core Middlewares Drivers USB_DEVICE
SOURCES := $(filter-out %/languages_builder.c,$(shell find $(SRCDIRS) -name '*.c' -o -name '*.s' -o -name '*.S'))
OBJS    := $(addprefix $(BUILD)/,$(addsuffix .o,$(basename $(SOURCES))))

CODEC_PLACEHOLDER := application/source/linkerdata/codec_bin_section_1.bin
CODEC_ADDRESS     := 0x0807537C

.SUFFIXES:

all: $(BUILD)/firmware.elf $(BUILD)/firmware.bin

$(CODEC_PLACEHOLDER):
	python3 -c "open('$@', 'wb').write(bytes(0x48BB0))"

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

# The files that show GITVERSION are rebuilt when it changes
$(BUILD)/gitversion: FORCE
	@mkdir -p $(dir $@)
	@echo $(GITVERSION) | cmp -s - $@ || echo $(GITVERSION) > $@

$(addprefix $(BUILD)/application/source/,usb/usb_com.o user_interface/menuFirmwareInfoScreen.o functions/hotspot.o): $(BUILD)/gitversion

# As in the CubeIDE project: the codec calls are inline assembly that relies on the unoptimised stack frame
$(BUILD)/application/source/dmr_codec/codec_interface.o: CFLAGS += -O0

# codec_bin.S includes the codec placeholder relative to the project's Debug directory
$(BUILD)/application/source/dmr_codec/codec_bin.o: application/source/dmr_codec/codec_bin.S $(CODEC_PLACEHOLDER)
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -Wa,-ICore -c $< -o $@

$(BUILD)/firmware.elf: $(OBJS) STM32F405VGTX_FLASH.ld
	$(CC) -o $@ $(OBJS) $(LDFLAGS)
	$(SIZE) $@
	@end=$$($(OBJDUMP) -h $@ | awk '$$2 == ".codec_bin_section_1" { print $$4 }'); \
	 echo "Flash free before the codec at $(CODEC_ADDRESS): $$(( $(CODEC_ADDRESS) - 0x$$end )) bytes"

$(BUILD)/firmware.bin: $(BUILD)/firmware.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean FORCE

-include $(OBJS:.o=.d)
