# Bare-metal Raspberry Pi Pico 2 (RP2350) demo for the co-simulation, built
# for both core architectures from one source. clang + lld (LLVM targets
# riscv32 and thumbv8m); no Pico SDK needed.
PICO_CC ?= clang
PICO_OBJCOPY ?= llvm-objcopy
COMMON   = -Os -std=c11 -ffreestanding -nostdlib -Wall -Wextra
RISCV    = --target=riscv32-unknown-none-elf -march=rv32imac_zicsr -mabi=ilp32 -mno-relax
ARM      = --target=thumbv8m.main-none-eabi -mcpu=cortex-m33 -mfloat-abi=soft
LDFLAGS  = -fuse-ld=lld -Wl,-T,rp2350_flash.ld -Wl,--gc-sections

TARGETS = pico2_demo_riscv.bin pico2_demo_arm.bin

all: $(TARGETS)

pico2_demo_riscv.elf: pico2_demo.c rp2350_flash.ld
	$(PICO_CC) $(RISCV) $(COMMON) $(LDFLAGS) -o $@ $<

pico2_demo_arm.elf: pico2_demo.c rp2350_flash.ld
	$(PICO_CC) $(ARM) $(COMMON) $(LDFLAGS) -o $@ $<

%.bin: %.elf
	$(PICO_OBJCOPY) -O binary $< $@

clean:
	rm -f *.elf *.bin

.PHONY: all clean
.PRECIOUS: %.elf
