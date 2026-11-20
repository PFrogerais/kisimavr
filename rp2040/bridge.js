// SPDX-License-Identifier: GPL-3.0-or-later
// kisimavr - RP2040 (Raspberry Pi Pico) co-simulation: the JavaScript side,
// on rp2040js (third_party/rp2040js). The interface with the C++ side is
// described in pico_common.js.

import { RP2040 } from '../third_party/rp2040js/rp2040.js';
import { SimulationClock } from '../third_party/rp2040js/clock/simulation-clock.js';
import { GPIOPinState } from '../third_party/rp2040js/gpio-pin.js';
import { USBCDC } from '../third_party/rp2040js/usb/cdc.js';
import { bootromB1 } from '../third_party/rp2040js/bootrom.js';
import { PicoBridge, bytesToText, forEachUf2Block, isUf2, makeLogger } from './pico_common.js';

const FLASH_BASE = 0x10000000;
const TIMER_SLICE = 1024;           // Instructions between two runs of setTimeout() callbacks

// rp2040js runs its PIO with setTimeout(): those callbacks are run from the
// simulation loop, about every TIMER_SLICE instructions.
const pendingTimers = new Map();
let nextTimerId = 1;
globalThis.setTimeout = (fn) => {
  const id = nextTimerId++;
  pendingTimers.set(id, fn);
  return id;
};
globalThis.clearTimeout = (id) => {
  pendingTimers.delete(id);
};

function runPendingTimers() {
  if (pendingTimers.size === 0) {
    return;
  }
  const due = [...pendingTimers.values()];
  pendingTimers.clear();
  for (const fn of due) {
    fn();
  }
}

function loadHex(mcu, text) {
  let upper = 0;
  for (const raw of text.split('\n')) {
    const line = raw.trim();
    if (!line.startsWith(':')) {
      continue;
    }
    const count = parseInt(line.substr(1, 2), 16);
    const addr = parseInt(line.substr(3, 4), 16);
    const type = parseInt(line.substr(7, 2), 16);
    if (type === 0x04) {
      upper = parseInt(line.substr(9, 4), 16) << 16;
    } else if (type === 0x00) {
      const absolute = (upper + addr) >>> 0;
      const offset = absolute >= FLASH_BASE ? absolute - FLASH_BASE : absolute;
      for (let i = 0; i < count; ++i) {
        mcu.flash[offset + i] = parseInt(line.substr(9 + 2 * i, 2), 16);
      }
    }
  }
}

export class Rp2040Bridge extends PicoBridge {
  constructor(ios, adc) {
    super(ios, adc);
    const clock = new SimulationClock();
    const mcu = new RP2040(clock);
    mcu.logger = makeLogger('RP2040');
    this.attach(mcu, clock, USBCDC, GPIOPinState);
    this.instructions = 0;
  }

  load(bytes, isText) {
    const { mcu } = this;
    mcu.loadBootrom(bootromB1);    // Also resets the core and erases the flash.
    if (isText) {
      loadHex(mcu, bytesToText(bytes));
    } else if (isUf2(bytes)) {
      let blocks = 0;
      forEachUf2Block(bytes, (address, payload) => {
        if (address >= FLASH_BASE && address - FLASH_BASE + payload.length <= mcu.flash.length) {
          mcu.flash.set(payload, address - FLASH_BASE);
          ++blocks;
        }
      });
      if (blocks === 0) {
        throw new Error('UF2 file without RP2040 flash blocks');
      }
    } else {
      mcu.flash.set(bytes, 0);
    }
    // As the bootrom would: boot2 at the start of the flash, then the
    // vector table at 0x10000100.
    mcu.core.SP = 0x20041f00;
    mcu.core.PC = FLASH_BASE;
  }

  advance(targetNs) {
    const { mcu, clock } = this;
    if (mcu.core.waiting) {
      const toAlarm = clock.nanosToNextAlarm;
      const left = targetNs - clock.nanos;
      clock.tick(toAlarm > 0 && toAlarm < left ? toAlarm : left);
    } else {
      const cycles = mcu.core.executeInstruction();
      clock.tick((cycles * 1e9) / mcu.clkSys);
      if (++this.instructions % TIMER_SLICE === 0) {
        runPendingTimers();
      }
    }
  }
}
