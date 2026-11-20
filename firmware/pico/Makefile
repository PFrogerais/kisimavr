# Bare-metal Raspberry Pi Pico (RP2040) demos for the co-simulation.
# clang + lld (any recent LLVM targets thumbv6m); no Pico SDK needed.
PICO_CC ?= clang
PICO_OBJCOPY ?= llvm-objcopy
CFLAGS   = --target=thumbv6m-none-eabi -mcpu=cortex-m0plus -Os -std=c11 \
           -ffreestanding -nostdlib -Wall -Wextra
LDFLAGS  = -fuse-ld=lld -Wl,-T,rp2040_flash.ld -Wl,--gc-sections

TARGETS = pico_demo.bin pico_i2c.bin pico_spi.bin pico_radar.bin pico_ventilo.bin

all: $(TARGETS)

%.elf: %.c pico_hw.h rp2040_flash.ld
	$(PICO_CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

%.bin: %.elf
	$(PICO_OBJCOPY) -O binary $< $@

clean:
	rm -f *.elf *.bin

.PHONY: all clean
.PRECIOUS: %.elf
