// SPDX-License-Identifier: GPL-3.0-or-later
// kisimavr - Raspberry Pi Pico / Pico 2 co-simulation: code shared by the
// RP2040 bridge (bridge.js, rp2040js) and the RP2350 bridge
// (rp2350_bridge.js, rp2350js).
//
// The C++ side (src/rp2040_device.cpp) creates one bridge per d_cosim
// instance and calls:
//   load(bytes, isText)  firmware: UF2, Intel HEX (text) or raw binary
//   run(targetNs)        run until targetNs, or until a pin changes: returns
//                        [timeNs, k0, l0, oe0, pu0, pd0, k1, ...] for the
//                        changed "io" pins (k = index in the io list)
//   setInput(k, level)   voltage read back on io pin k (true = high)
//   setComparator(c, v)  SAR comparator of ADC channel c (true: Vin > Vdac)
//   takeLines(final)     finished text lines: [timeNs, "UART0", "text", ...]
//
// The emulators run on a SimulationClock: time only moves when run() ticks it.

const FUNC_UART = 2;
// One SAR step per microsecond: ngspice must compute the comparator after
// each DAC change, and its time step is up to 0.5 us in the demos
// (".tran 0.5u ..."). A conversion takes 12 us instead of 2 us on the chip.
const SAR_STEP_NS = 1000;

/** Quiet logger: only errors, through the plug-in's stderr. */
export function makeLogger(chip) {
  return {
    debug() {},
    info() {},
    warn() {},
    error(component, message) {
      globalThis.kisimPrint?.(`${chip} ${component}: ${message}`);
    },
  };
}

/** Firmware bytes (Uint8Array) as a string: Intel HEX files. */
export function bytesToText(bytes) {
  let text = '';
  for (let i = 0; i < bytes.length; i += 4096) {
    text += String.fromCharCode.apply(null, bytes.subarray(i, i + 4096));
  }
  return text;
}

/** True for a UF2 file ("UF2\n" magic at the start). */
export function isUf2(bytes) {
  return bytes.length >= 512 && bytes[0] === 0x55 && bytes[1] === 0x46 && bytes[2] === 0x32;
}

/**
 * Blocks of a UF2 file: calls put(address, payload, familyId) for each block
 * of the main flash or RAM ("not main flash" blocks are skipped).
 */
export function forEachUf2Block(bytes, put) {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
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
    const family = flags & 0x2000 ? view.getUint32(off + 28, true) : 0;
    if (size <= 476) {
      put(address, bytes.subarray(off + 32, off + 32 + size), family);
    }
  }
}

/**
 * Alarm of the emulator clock: rp2040js calls a function, rp2350js the
 * fire() method of an object; a function with a fire property is both.
 */
function createAlarm(clock, fn) {
  const callback = () => fn();
  callback.fire = callback;
  return clock.createAlarm(callback);
}

// ---- UART frames on the pins -----------------------------------------------

/** TX line of one UART: bytes from the firmware sent as 8N1 frames. */
class TxLine {
  constructor(clock, onChange) {
    this.onChange = onChange;
    this.level = true;
    this.queue = [];
    this.bits = [];
    this.bitNs = 1e9 / 115200;
    this.alarm = createAlarm(clock, () => this.nextBit());
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
    this.uart = uart;
    this.level = true;
    this.bit = -1;
    this.value = 0;
    this.busy = false;
    this.alarm = createAlarm(clock, () => this.sample());
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

/**
 * Pins, UARTs, USB text and SAR ADC around an rp2040js / rp2350js chip.
 * Subclasses create the chip, then call attach(), and provide load() and
 * advance(targetNs) (execute one instruction, or skip an idle period).
 */
export class PicoBridge {
  /**
   * @param {number[]} ios      GPIO numbers of the "io" pins, in port order
   * @param {number[]} adc      ADC channels (0..3 = GP26..GP29) with a SAR comparator
   */
  constructor(ios, adc) {
    this.ios = ios;
    this.adc = adc;
    this.dirty = true;
    this.last = ios.map(() => -1);
    this.lines = [];
    this.text = ['', '', ''];
    this.textTime = [0, 0, 0];
    this.inputs = [];
    this.code = 0;
    this.lastCode = 0;
    this.cmp = adc.map(() => false);
    this.mcu = null;
    this.clock = null;
  }

  /**
   * @param mcu          RP2040 or RP2350 of the emulator
   * @param clock        its SimulationClock
   * @param USBCDC       USB CDC host class of the emulator
   * @param pinStates    its GPIOPinState enum
   */
  attach(mcu, clock, USBCDC, pinStates) {
    this.mcu = mcu;
    this.clock = clock;
    this.pinStates = pinStates;
    this.inputs = new Array(mcu.gpio.length).fill(false);
    const mark = () => {
      this.dirty = true;
    };
    for (const pin of mcu.gpio) {
      pin.addListener(mark);
    }

    // UARTs: text, TX frames, RX decoding.
    this.tx = [0, 1].map(() => new TxLine(clock, mark));
    this.rx = [0, 1].map((k) => new RxDecoder(clock, mcu.uart[k]));
    mcu.uart.forEach((uart, k) => {
      uart.onByte = (value) => {
        this.tx[k].send(value, uart.baudRate);
        this.textByte(k, value);
      };
    });

    // USB serial (Serial of Arduino-pico): text only.
    this.cdc = new USBCDC(mcu.usbCtrl);
    this.cdc.onSerialData = (buffer, length) => {
      const n = length ?? buffer.length;
      for (let i = 0; i < n; ++i) {
        this.textByte(2, buffer[i]);
      }
    };

    // ADC: SAR conversion across the d_cosim boundary.
    this.sar = { channel: -1, bit: 11, alarm: createAlarm(clock, () => this.sarStep()) };
    mcu.adc.channelValues[4] = Math.round((0.706 / 3.3) * 4095); // 27 degC
    const rpAdc = mcu.adc;
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
    const s = this.pinStates;
    switch (g.value) {
      case s.High:
        return 1 | 2;
      case s.Low:
        return 2;
      case s.InputBusKeeper:
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
    const idx = this.adc.indexOf(s.channel);
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
    const { clock } = this;
    if (this.dirty) {
      this.dirty = false;
      const out = this.changes();
      if (out.length > 0) {
        return [clock.nanos, ...out];
      }
    }
    while (clock.nanos < targetNs) {
      this.advance(targetNs);
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
