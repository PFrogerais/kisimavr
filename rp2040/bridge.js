// SPDX-License-Identifier: GPL-3.0-or-later
// kisimavr - RP2040 (Raspberry Pi Pico) co-simulation: the JavaScript side.
//
// Runs inside mcu_cosim.so (QuickJS). The C++ side (src/rp2040_device.cpp)
// creates one Rp2040Bridge per d_cosim instance and calls:
//   load(bytes)          firmware: UF2, Intel HEX (text) or raw binary
//   run(targetNs)        run until targetNs, or until a pin changes: returns
//                        [timeNs, k0, l0, oe0, pu0, pd0, k1, ...] for the
//                        changed "io" pins (k = index in the io list)
//   setInput(k, level)   voltage read back on io pin k (true = high)
//   setComparator(c, v)  SAR comparator of ADC channel c (true: Vin > Vdac)
//   dacCode()            12-bit code of the SAR DAC
//   takeLines()          finished text lines: [timeNs, "UART0", "text", ...]
//
// rp2040js runs on a SimulationClock: time only moves when run() ticks it.

import { RP2040 } from '../third_party/rp2040js/rp2040.js';
import { SimulationClock } from '../third_party/rp2040js/clock/simulation-clock.js';
import { GPIOPinState } from '../third_party/rp2040js/gpio-pin.js';
import { USBCDC } from '../third_party/rp2040js/usb/cdc.js';
import { bootromB1 } from '../third_party/rp2040js/bootrom.js';

const FLASH_BASE = 0x10000000;
const FUNC_UART = 2;
// One SAR step per microsecond: ngspice must compute the comparator after
// each DAC change, and its time step is up to 0.5 us in the demos
// (".tran 0.5u ..."). A conversion takes 12 us instead of 2 us on the chip.
const SAR_STEP_NS = 1000;
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

/** Quiet logger: only errors, through the plug-in's stderr. */
const logger = {
  debug() {},
  info() {},
  warn() {},
  error(component, message) {
    globalThis.kisimPrint?.(`RP2040 ${component}: ${message}`);
  },
};

// ---- Firmware formats ------------------------------------------------------

function loadUf2(mcu, bytes) {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  let blocks = 0;
  for (let off = 0; off + 512 <= bytes.length; off += 512) {
    if (view.getUint32(off, true) !== 0x0a324655 || view.getUint32(off + 4, true) !== 0x9e5d5157) {
      continue;
    }
    const flags = view.getUint32(off + 8, true);
    if (flags & 1) {
      continue; // "not main flash"
    }
    const address = view.getUint32(off + 12, true);
    const size = view.getUint32(off + 16, true);
    if (address >= FLASH_BASE && size <= 476) {
      mcu.flash.set(bytes.subarray(off + 32, off + 32 + size), address - FLASH_BASE);
      ++blocks;
    }
  }
  if (blocks === 0) {
    throw new Error('UF2 file without RP2040 flash blocks');
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

// ---- UART frames on the pins -----------------------------------------------

/** TX line of one UART: bytes from the firmware sent as 8N1 frames. */
class TxLine {
  constructor(clock, onChange) {
    this.clock = clock;
    this.onChange = onChange;
    this.level = true;
    this.queue = [];
    this.bits = [];
    this.bitNs = 1e9 / 115200;
    this.alarm = clock.createAlarm(() => this.nextBit());
  }

  send(byte, baud) {
    this.queue.push(byte);
    if (baud > 0) {
      this.bitNs = 1e9 / baud;
    }
    if (this.bits.length === 0 && !this.alarm.scheduled) {
      this.startByte();
    }
  }

  startByte() {
    const b = this.queue.shift();
    this.bits = [0];
    for (let k = 0; k < 8; ++k) {
      this.bits.push((b >> k) & 1);
    }
    this.bits.push(1);
    this.nextBit();
  }

  nextBit() {
    if (this.bits.length === 0) {
      if (this.queue.length > 0) {
        this.startByte();
      }
      return;
    }
    const level = this.bits.shift() === 1;
    if (level !== this.level) {
      this.level = level;
      this.onChange();
    }
    this.alarm.schedule(this.bitNs);
  }
}

/** RX decoder of one UART: frames read on the pin, bytes to the UART. */
class RxDecoder {
  constructor(clock, uart) {
    this.clock = clock;
    this.uart = uart;
    this.level = true;
    this.bit = -1;
    this.value = 0;
    this.busy = false;
    this.alarm = clock.createAlarm(() => this.sample());
  }

  bitNs() {
    const baud = this.uart.baudRate;
    return baud > 0 ? 1e9 / baud : 1e9 / 115200;
  }

  edge(level) {
    const falling = this.level && !level;
    this.level = level;
    if (falling && !this.busy) {
      this.busy = true;
      this.bit = -1;
      this.value = 0;
      this.alarm.schedule(this.bitNs() / 2);    // Middle of the start bit.
    }
  }

  sample() {
    if (this.bit < 0) {
      if (this.level) {
        this.busy = false;                     // Glitch, not a start bit.
        return;
      }
    } else if (this.bit < 8) {
      if (this.level) {
        this.value |= 1 << this.bit;
      }
    } else {
      this.busy = false;
      if (this.level) {
        this.uart.feedByte(this.value);
      }
      return;
    }
    ++this.bit;
    this.alarm.schedule(this.bitNs());
  }
}

// ---- The bridge ------------------------------------------------------------

export class Rp2040Bridge {
  /**
   * @param {number[]} ios      GPIO numbers of the "io" pins, in port order
   * @param {number[]} adc      ADC channels (0..3 = GP26..GP29) with a SAR comparator
   */
  constructor(ios, adc) {
    this.clock = new SimulationClock();
    this.mcu = new RP2040(this.clock);
    this.mcu.logger = logger;
    this.ios = ios;
    this.adc = adc;
    this.dirty = true;
    this.last = ios.map(() => -1);
    this.lines = [];
    this.text = ['', '', ''];
    this.textTime = [0, 0, 0];
    this.inputs = new Array(30).fill(false);
    this.instructions = 0;

    const mark = () => {
      this.dirty = true;
    };
    for (const pin of this.mcu.gpio) {
      pin.addListener(mark);
    }

    // UARTs: text, TX frames, RX decoding.
    this.tx = [0, 1].map(() => new TxLine(this.clock, mark));
    this.rx = [0, 1].map((k) => new RxDecoder(this.clock, this.mcu.uart[k]));
    this.mcu.uart.forEach((uart, k) => {
      uart.onByte = (value) => {
        this.tx[k].send(value, uart.baudRate);
        this.textByte(k, value);
      };
    });

    // USB serial (Serial of Arduino-pico): text only.
    this.cdc = new USBCDC(this.mcu.usbCtrl);
    this.cdc.onSerialData = (buffer) => {
      for (const b of buffer) {
        this.textByte(2, b);
      }
    };

    // ADC: SAR conversion across the d_cosim boundary.
    this.code = 0;
    this.lastCode = 0;
    this.cmp = [false, false, false, false];
    this.sar = { channel: -1, bit: 11, alarm: this.clock.createAlarm(() => this.sarStep()) };
    this.mcu.adc.channelValues[4] = Math.round((0.706 / 3.3) * 4095); // 27 degC
    const rpAdc = this.mcu.adc;
    const defaultRead = rpAdc.onADCRead;
    rpAdc.onADCRead = (channel) => {
      if (this.adc.includes(channel)) {
        this.sar.channel = channel;
        this.sar.bit = 11;
        this.code = 1 << 11;
        this.dirty = true;
        this.sar.alarm.schedule(SAR_STEP_NS);
      } else {
        defaultRead(channel);
      }
    };
  }

  // -- Firmware ---------------------------------------------------------------

  load(bytes, isText) {
    this.mcu.loadBootrom(bootromB1);    // Also resets the core and erases the flash.
    if (isText) {
      let text = '';
      for (let i = 0; i < bytes.length; i += 4096) {
        text += String.fromCharCode.apply(null, bytes.subarray(i, i + 4096));
      }
      loadHex(this.mcu, text);
    } else if (bytes.length >= 512 && bytes[0] === 0x55 && bytes[1] === 0x46 && bytes[2] === 0x32) {
      loadUf2(this.mcu, bytes);
    } else {
      this.mcu.flash.set(bytes, 0);
    }
    // As the bootrom would: boot2 at the start of the flash, then the
    // vector table at 0x10000100.
    this.mcu.core.SP = 0x20041f00;
    this.mcu.core.PC = FLASH_BASE;
  }

  // -- Pins -------------------------------------------------------------------

  /** level | oe << 1 | pu << 2 | pd << 3 of GPIO n. */
  pinWord(n) {
    const g = this.mcu.gpio[n];
    const pu = g.pullupEnabled ? 4 : 0;
    const pd = g.pulldownEnabled ? 8 : 0;
    if (g.functionSelect === FUNC_UART) {
      const role = n % 4;
      const uart = (n >> 3) & 1;
      if (role === 0) {
        return (this.tx[uart].level ? 1 : 0) | 2;        // TX: driven
      }
      return pu | pd;                                     // RX, CTS, RTS: input
    }
    switch (g.value) {
      case GPIOPinState.High:
        return 1 | 2;
      case GPIOPinState.Low:
        return 2;
      case GPIOPinState.InputBusKeeper:
        return this.inputs[n] ? 4 : 8;                    // Keeps the last level.
      default:
        return pu | pd;
    }
  }

  setInput(k, value) {
    const level = !!value;
    const n = this.ios[k];
    this.inputs[n] = level;
    this.mcu.gpio[n].setInputValue(level);
    if (this.mcu.gpio[n].functionSelect === FUNC_UART && n % 4 === 1) {
      this.rx[(n >> 3) & 1].edge(level);
    }
  }

  setComparator(c, value) {
    this.cmp[c] = !!value;
  }

  dacCode() {
    return this.code;
  }

  sarStep() {
    const s = this.sar;
    const ch = s.channel;
    const idx = this.adc.indexOf(ch);
    if (!this.cmp[idx]) {
      this.code &= ~(1 << s.bit);                         // Vin < Vdac: bit off
    }
    --s.bit;
    if (s.bit >= 0) {
      this.code |= 1 << s.bit;
      this.dirty = true;
      s.alarm.schedule(SAR_STEP_NS);
      return;
    }
    const result = this.code;
    s.channel = -1;
    this.mcu.adc.completeADCRead(result, false);
  }

  // -- Text -------------------------------------------------------------------

  textByte(k, value) {
    if (value === 10) {
      this.flushText(k);
    } else if (value !== 13) {
      if (this.text[k].length === 0) {
        this.textTime[k] = this.clock.nanos;
      }
      this.text[k] += value >= 32 && value < 127 ? String.fromCharCode(value) : '.';
      if (this.text[k].length >= 200) {
        this.flushText(k);
      }
    }
  }

  flushText(k) {
    if (this.text[k].length > 0) {
      this.lines.push(this.textTime[k], ['UART0', 'UART1', 'USB'][k], this.text[k]);
      this.text[k] = '';
    }
  }

  takeLines(final) {
    if (final) {
      [0, 1, 2].forEach((k) => this.flushText(k));
    }
    const out = this.lines;
    this.lines = [];
    return out;
  }

  // -- Time -------------------------------------------------------------------

  /** Changes as [k, level, oe, pu, pd, ...]; k = -1: SAR DAC, [-1, code, 0, 0, 0]. */
  changes() {
    const out = [];
    this.ios.forEach((n, k) => {
      const w = this.pinWord(n);
      if (w !== this.last[k]) {
        this.last[k] = w;
        out.push(k, w & 1, (w >> 1) & 1, (w >> 2) & 1, (w >> 3) & 1);
      }
    });
    if (this.code !== this.lastCode) {
      this.lastCode = this.code;
      out.push(-1, this.code, 0, 0, 0);
    }
    return out;
  }

  run(targetNs) {
    const { mcu, clock } = this;
    if (this.dirty) {
      this.dirty = false;
      const out = this.changes();
      if (out.length > 0) {
        return [clock.nanos, ...out];
      }
    }
    while (clock.nanos < targetNs) {
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
      if (this.dirty) {
        this.dirty = false;
        const out = this.changes();
        if (out.length > 0) {
          return [Math.min(clock.nanos, targetNs), ...out];
        }
      }
    }
    return [targetNs];
  }
}
