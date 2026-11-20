# Firmware builds for the co-simulation demos (avr-gcc + avr-libc).
MCU    ?= atmega328p
F_CPU  ?= 16000000UL

CC      = avr-gcc
OBJCOPY = avr-objcopy
CFLAGS  = -mmcu=$(MCU) -DF_CPU=$(F_CPU) -Os -std=c11 -Wall -Wextra

TARGETS = blink.hex adc_pwm.hex uart_adc.hex full_demo.hex i2c_lm75.hex uart_cmd.hex

all: $(TARGETS)

%.elf: %.c
	$(CC) $(CFLAGS) -o $@ $<

%.hex: %.elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

clean:
	rm -f *.elf *.hex

.PHONY: all clean
.PRECIOUS: %.elf
