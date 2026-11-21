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
//   setRaw(k, on)        keep the raw bytes of port k (0 UART0, 1 UART1, 2 USB)
//   takeRaw(k)           raw bytes sent by the firmware on port k since the last call
//   feed(k, bytes)       bytes typed in a terminal: to UART k (one frame time
//                        apart) or to the USB serial port (k = 2)
//
// The emulators run on a SimulationClock: time only moves when run() ticks it.

const FUNC_SPI = 1;
const FUNC_UART = 2;
const FUNC_I2C = 3;
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

// ---- Bus sequences on the pins ------------------------------------------------

/**
 * Runs one generator at a time on the emulator clock: the generator yields
 * delays in ns; done(result) is called when it returns.
 */
class Sequencer {
  constructor(clock) {
    this.gen = null;
    this.done = null;
    this.alarm = createAlarm(clock, () => this.next());
  }

  get busy() {
    return this.gen !== null;
  }

  run(gen, done) {
    this.gen = gen;
    this.done = done;
    this.next();
  }

  next() {
    const r = this.gen.next();
    if (r.done) {
      const done = this.done;
      this.gen = null;
      this.done = null;
      done(r.value);
    } else {
      this.alarm.schedule(r.value);
    }
  }
}

/**
 * I2C master of the chip on its pins (open drain): START, address, bytes,
 * ACK bits and STOP as SDA / SCL edges with the SCL timing of the
 * controller; ACK and read bits are the SDA levels of the circuit; clock
 * stretching is honoured. Without an SDA and an SCL "io" pin, every
 * address is NACKed (as without a slave).
 */
class I2cMaster {
  constructor(bridge, index) {
    this.bridge = bridge;
    this.index = index;
    this.i2c = bridge.mcu.i2c[index];
    this.seq = new Sequencer(bridge.clock);
    this.sda = -1;
    this.scl = -1;
    const i2c = this.i2c;
    i2c.onStart = (restart) => this.go(this.start(restart), () => i2c.completeStart());
    i2c.onConnect = (address, mode) =>
      this.go(this.writeByte(((address & 0x7f) << 1) | (mode ? 1 : 0)), (ack) => i2c.completeConnect(ack));
    i2c.onWriteByte = (value) => this.go(this.writeByte(value), (ack) => i2c.completeWrite(ack));
    i2c.onReadByte = (ack) => this.go(this.readByte(ack), (value) => i2c.completeRead(value));
    i2c.onStop = () => this.go(this.stop(), () => i2c.completeStop());
  }

  /** SDA / SCL "io" pins of this controller (function I2C), or -1. */
  findPins() {
    const { bridge, index } = this;
    this.sda = -1;
    this.scl = -1;
    for (const n of bridge.ios) {
      if (bridge.mcu.gpio[n].functionSelect === FUNC_I2C && ((n >> 1) & 1) === index) {
        if (n % 2 === 0) {
          this.sda = n;
        } else {
          this.scl = n;
        }
      }
    }
    return this.sda >= 0 && this.scl >= 0;
  }

  go(gen, done) {
    if (this.sda < 0 || this.scl < 0) {
      if (!this.findPins()) {
        done(false);           // No bus in the circuit: NACK / 0x00.
        return;
      }
    }
    this.seq.run(gen, done);
  }

  get tLow() {
    const counts = this.i2c.sclLowPeriod;
    return counts >= 8 ? (counts * 1e9) / this.bridge.mcu.clkSys : 4700;
  }

  get tHigh() {
    const counts = this.i2c.sclHighPeriod;
    return counts >= 6 ? (counts * 1e9) / this.bridge.mcu.clkSys : 4000;
  }

  get hold() {
    return Math.min(500, this.tLow / 4);
  }

  /** Drive pin n low (true) or release it. */
  drive(n, low) {
    if (this.bridge.i2cLow[n] !== low) {
      this.bridge.i2cLow[n] = low;
      this.bridge.dirty = true;
    }
  }

  line(n) {
    return !this.bridge.i2cLow[n] && this.bridge.inputs[n];
  }

  *releaseScl() {
    this.drive(this.scl, false);
    for (let k = 0; k < 4000 && !this.bridge.inputs[this.scl]; ++k) {
      yield 250;               // Rise time, or a slave stretching the clock.
    }
  }

  /** One clock with SCL low on entry and on exit; returns SDA at mid-high. */
  *clock(release) {
    this.drive(this.sda, !release);
    yield this.tLow - this.hold;
    yield* this.releaseScl();
    yield this.tHigh / 2;
    const bit = this.line(this.sda);
    yield this.tHigh / 2;
    this.drive(this.scl, true);
    yield this.hold;
    return bit;
  }

  *start(restart) {
    // Both lines high (repeated START: from the end of a byte, SCL low)...
    this.drive(this.sda, false);
    if (restart) {
      yield this.tLow - this.hold;
    }
    yield* this.releaseScl();
    yield this.tHigh;
    // ...then SDA falls while SCL is high.
    this.drive(this.sda, true);
    yield this.tHigh;
    this.drive(this.scl, true);
    yield this.hold;
  }

  *writeByte(value) {
    for (let k = 7; k >= 0; --k) {
      yield* this.clock(((value >> k) & 1) === 1);
    }
    const nack = yield* this.clock(true);
    return !nack;
  }

  *readByte(ack) {
    let value = 0;
    for (let k = 0; k < 8; ++k) {
      value = (value << 1) | ((yield* this.clock(true)) ? 1 : 0);
    }
    yield* this.clock(!ack);
    return value;
  }

  *stop() {
    this.drive(this.sda, true);
    yield this.tLow - this.hold;
    yield* this.releaseScl();
    yield this.tHigh;
    this.drive(this.sda, false);
    yield this.tLow;           // Bus free time.
  }
}

/**
 * SPI master of the chip on its pins: SCK, TX (MOSI) and CSn driven with
 * the clock rate, mode (CPOL / CPHA) and frame size of the controller; RX
 * (MISO) read from the circuit. Without an SCK "io" pin, the frame is
 * exchanged at once and reads 0.
 */
class SpiMaster {
  constructor(bridge, index) {
    this.bridge = bridge;
    this.index = index;
    this.spi = bridge.mcu.spi[index];
    this.seq = new Sequencer(bridge.clock);
    this.sck = null;           // Level driven on SCK (null: idle level = CPOL)
    this.tx = false;
    this.cs = true;
    this.spi.onTransmit = (value) => this.transmit(value);
  }

  get cpol() {
    return (this.spi.control0 & (1 << 6)) !== 0;
  }

  get cpha() {
    return (this.spi.control0 & (1 << 7)) !== 0;
  }

  /** Pin of a role (0 RX, 1 CSn, 2 SCK, 3 TX) among the "io" pins, or -1. */
  pin(role) {
    const { bridge, index } = this;
    for (const n of bridge.ios) {
      if (bridge.mcu.gpio[n].functionSelect === FUNC_SPI && n % 4 === role &&
          ((n >> 3) & 1) === index) {
        return n;
      }
    }
    return -1;
  }

  /** level | oe << 1 for a driven role, or -1 for RX. */
  word(role) {
    switch (role) {
      case 1:
        return (this.cs ? 1 : 0) | 2;
      case 2:
        return ((this.sck === null ? this.cpol : this.sck) ? 1 : 0) | 2;
      case 3:
        return (this.tx ? 1 : 0) | 2;
      default:
        return -1;
    }
  }

  set(field, value) {
    if (this[field] !== value) {
      this[field] = value;
      this.bridge.dirty = true;
    }
  }

  transmit(value) {
    if (this.pin(2) < 0) {
      this.spi.completeTransmit(0);
      return;
    }
    this.seq.run(this.frame(value), (rx) => this.spi.completeTransmit(rx));
  }

  *frame(value) {
    const freq = this.spi.clockFrequency;
    const half = freq > 0 ? 5e8 / freq : 500;
    const bits = this.spi.dataBits;
    const rx = this.pin(0);
    const { cpol, cpha } = this;
    let result = 0;
    this.set('sck', cpol);
    this.set('cs', false);
    yield half;
    for (let k = bits - 1; k >= 0; --k) {
      const bit = ((value >> k) & 1) === 1;
      if (!cpha) {
        this.set('tx', bit);
        yield half;
        result = (result << 1) | (rx >= 0 && this.bridge.inputs[rx] ? 1 : 0);
        this.set('sck', !cpol);
        yield half;
        this.set('sck', cpol);
      } else {
        this.set('sck', !cpol);
        this.set('tx', bit);
        yield half;
        result = (result << 1) | (rx >= 0 && this.bridge.inputs[rx] ? 1 : 0);
        this.set('sck', cpol);
        yield half;
      }
    }
    yield half;
    this.set('cs', true);
    return result;
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
    this.raw = [null, null, null];    // Raw bytes kept for a pseudo-terminal
    this.rxQueue = [[], []];          // Typed bytes waiting for UART0 / UART1
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

    this.rxAlarms = [0, 1].map((k) => createAlarm(clock, () => this.feedNext(k)));

    // I2C and SPI controllers: signals on their pins.
    this.i2cLow = new Array(mcu.gpio.length).fill(false);
    this.i2cMasters = [0, 1].map((k) => new I2cMaster(this, k));
    this.spiMasters = [0, 1].map((k) => new SpiMaster(this, k));

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
    if (g.functionSelect === FUNC_I2C) {
      return this.i2cLow[n] ? 2 : pu | pd;               // Open drain
    }
    if (g.functionSelect === FUNC_SPI) {
      const w = this.spiMasters[(n >> 3) & 1].word(n % 4);
      return w >= 0 ? w : pu | pd;                        // RX: input
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
    if (this.raw[k] !== null) {
      this.raw[k].push(value);
    }
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

  setRaw(k, on) {
    this.raw[k] = on ? [] : null;
  }

  takeRaw(k) {
    const out = this.raw[k] ?? [];
    if (this.raw[k] !== null) {
      this.raw[k] = [];
    }
    return out;
  }

  feed(k, bytes) {
    if (k === 2) {
      for (const b of bytes) {
        this.cdc.sendSerialByte(b);
      }
      return;
    }
    const queue = this.rxQueue[k];
    const idle = queue.length === 0;
    queue.push(...bytes);
    if (idle && queue.length > 0) {
      this.feedNext(k);
    }
  }

  /** One typed byte to the UART, the next one a frame time later (10 bits). */
  feedNext(k) {
    const queue = this.rxQueue[k];
    if (queue.length === 0) {
      return;
    }
    this.mcu.uart[k].feedByte(queue.shift());
    if (queue.length > 0) {
      const baud = this.mcu.uart[k].baudRate;
      this.rxAlarms[k].schedule(10e9 / (baud > 0 ? baud : 115200));
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
