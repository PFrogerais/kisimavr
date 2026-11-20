import { BasePeripheral } from './peripheral.js';
export var DREQChannel;
(function (DREQChannel) {
    DREQChannel[DREQChannel["DREQ_PIO0_TX0"] = 0] = "DREQ_PIO0_TX0";
    DREQChannel[DREQChannel["DREQ_PIO0_TX1"] = 1] = "DREQ_PIO0_TX1";
    DREQChannel[DREQChannel["DREQ_PIO0_TX2"] = 2] = "DREQ_PIO0_TX2";
    DREQChannel[DREQChannel["DREQ_PIO0_TX3"] = 3] = "DREQ_PIO0_TX3";
    DREQChannel[DREQChannel["DREQ_PIO0_RX0"] = 4] = "DREQ_PIO0_RX0";
    DREQChannel[DREQChannel["DREQ_PIO0_RX1"] = 5] = "DREQ_PIO0_RX1";
    DREQChannel[DREQChannel["DREQ_PIO0_RX2"] = 6] = "DREQ_PIO0_RX2";
    DREQChannel[DREQChannel["DREQ_PIO0_RX3"] = 7] = "DREQ_PIO0_RX3";
    DREQChannel[DREQChannel["DREQ_PIO1_TX0"] = 8] = "DREQ_PIO1_TX0";
    DREQChannel[DREQChannel["DREQ_PIO1_TX1"] = 9] = "DREQ_PIO1_TX1";
    DREQChannel[DREQChannel["DREQ_PIO1_TX2"] = 10] = "DREQ_PIO1_TX2";
    DREQChannel[DREQChannel["DREQ_PIO1_TX3"] = 11] = "DREQ_PIO1_TX3";
    DREQChannel[DREQChannel["DREQ_PIO1_RX0"] = 12] = "DREQ_PIO1_RX0";
    DREQChannel[DREQChannel["DREQ_PIO1_RX1"] = 13] = "DREQ_PIO1_RX1";
    DREQChannel[DREQChannel["DREQ_PIO1_RX2"] = 14] = "DREQ_PIO1_RX2";
    DREQChannel[DREQChannel["DREQ_PIO1_RX3"] = 15] = "DREQ_PIO1_RX3";
    DREQChannel[DREQChannel["DREQ_PIO2_TX0"] = 16] = "DREQ_PIO2_TX0";
    DREQChannel[DREQChannel["DREQ_PIO2_TX1"] = 17] = "DREQ_PIO2_TX1";
    DREQChannel[DREQChannel["DREQ_PIO2_TX2"] = 18] = "DREQ_PIO2_TX2";
    DREQChannel[DREQChannel["DREQ_PIO2_TX3"] = 19] = "DREQ_PIO2_TX3";
    DREQChannel[DREQChannel["DREQ_PIO2_RX0"] = 20] = "DREQ_PIO2_RX0";
    DREQChannel[DREQChannel["DREQ_PIO2_RX1"] = 21] = "DREQ_PIO2_RX1";
    DREQChannel[DREQChannel["DREQ_PIO2_RX2"] = 22] = "DREQ_PIO2_RX2";
    DREQChannel[DREQChannel["DREQ_PIO2_RX3"] = 23] = "DREQ_PIO2_RX3";
    DREQChannel[DREQChannel["DREQ_SPI0_TX"] = 24] = "DREQ_SPI0_TX";
    DREQChannel[DREQChannel["DREQ_SPI0_RX"] = 25] = "DREQ_SPI0_RX";
    DREQChannel[DREQChannel["DREQ_SPI1_TX"] = 26] = "DREQ_SPI1_TX";
    DREQChannel[DREQChannel["DREQ_SPI1_RX"] = 27] = "DREQ_SPI1_RX";
    DREQChannel[DREQChannel["DREQ_UART0_TX"] = 28] = "DREQ_UART0_TX";
    DREQChannel[DREQChannel["DREQ_UART0_RX"] = 29] = "DREQ_UART0_RX";
    DREQChannel[DREQChannel["DREQ_UART1_TX"] = 30] = "DREQ_UART1_TX";
    DREQChannel[DREQChannel["DREQ_UART1_RX"] = 31] = "DREQ_UART1_RX";
    DREQChannel[DREQChannel["DREQ_PWM_WRAP0"] = 32] = "DREQ_PWM_WRAP0";
    DREQChannel[DREQChannel["DREQ_PWM_WRAP1"] = 33] = "DREQ_PWM_WRAP1";
    DREQChannel[DREQChannel["DREQ_PWM_WRAP2"] = 34] = "DREQ_PWM_WRAP2";
    DREQChannel[DREQChannel["DREQ_PWM_WRAP3"] = 35] = "DREQ_PWM_WRAP3";
    DREQChannel[DREQChannel["DREQ_PWM_WRAP4"] = 36] = "DREQ_PWM_WRAP4";
    DREQChannel[DREQChannel["DREQ_PWM_WRAP5"] = 37] = "DREQ_PWM_WRAP5";
    DREQChannel[DREQChannel["DREQ_PWM_WRAP6"] = 38] = "DREQ_PWM_WRAP6";
    DREQChannel[DREQChannel["DREQ_PWM_WRAP7"] = 39] = "DREQ_PWM_WRAP7";
    DREQChannel[DREQChannel["DREQ_PWM_WRAP8"] = 40] = "DREQ_PWM_WRAP8";
    DREQChannel[DREQChannel["DREQ_PWM_WRAP9"] = 41] = "DREQ_PWM_WRAP9";
    DREQChannel[DREQChannel["DREQ_PWM_WRAP10"] = 42] = "DREQ_PWM_WRAP10";
    DREQChannel[DREQChannel["DREQ_PWM_WRAP11"] = 43] = "DREQ_PWM_WRAP11";
    DREQChannel[DREQChannel["DREQ_I2C0_TX"] = 44] = "DREQ_I2C0_TX";
    DREQChannel[DREQChannel["DREQ_I2C0_RX"] = 45] = "DREQ_I2C0_RX";
    DREQChannel[DREQChannel["DREQ_I2C1_TX"] = 46] = "DREQ_I2C1_TX";
    DREQChannel[DREQChannel["DREQ_I2C1_RX"] = 47] = "DREQ_I2C1_RX";
    DREQChannel[DREQChannel["DREQ_ADC"] = 48] = "DREQ_ADC";
    DREQChannel[DREQChannel["DREQ_XIP_STREAM"] = 49] = "DREQ_XIP_STREAM";
    DREQChannel[DREQChannel["DREQ_XIP_QMITX"] = 50] = "DREQ_XIP_QMITX";
    DREQChannel[DREQChannel["DREQ_XIP_QMIRX"] = 51] = "DREQ_XIP_QMIRX";
    DREQChannel[DREQChannel["DREQ_HSTX"] = 52] = "DREQ_HSTX";
    DREQChannel[DREQChannel["DREQ_CORESIGHT"] = 53] = "DREQ_CORESIGHT";
    DREQChannel[DREQChannel["DREQ_SHA256"] = 54] = "DREQ_SHA256";
    DREQChannel[DREQChannel["DREQ_MAX"] = 55] = "DREQ_MAX";
})(DREQChannel || (DREQChannel = {}));
var TREQ;
(function (TREQ) {
    TREQ[TREQ["Timer0"] = 59] = "Timer0";
    TREQ[TREQ["Timer1"] = 60] = "Timer1";
    TREQ[TREQ["Timer2"] = 61] = "Timer2";
    TREQ[TREQ["Timer3"] = 62] = "Timer3";
    TREQ[TREQ["Permanent"] = 63] = "Permanent";
})(TREQ || (TREQ = {}));
const CHANNEL_COUNT = 16;
const REGISTERS_SIZE_PER_CHANNEL = 0x40;
const DEBUG_REGISTER_START = 0x800;
// Per-channel registers
const CHn_READ_ADDR = 0x000; // DMA Channel n Read Address pointer
const CHn_WRITE_ADDR = 0x004; // DMA Channel n Write Address pointer
const CHn_TRANS_COUNT = 0x008; // DMA Channel n Transfer Count
const CHn_CTRL_TRIG = 0x00c; // DMA Channel n Control and Status
const CHn_AL1_CTRL = 0x010; // Alias for channel n CTRL register
const CHn_AL1_READ_ADDR = 0x014; // Alias for channel n READ_ADDR register
const CHn_AL1_WRITE_ADDR = 0x018; // Alias for channel n WRITE_ADDR register
const CHn_AL1_TRANS_COUNT_TRIG = 0x01c; // Alias for channel n TRANS_COUNT register
const CHn_AL2_CTRL = 0x020; // Alias for channel n CTRL register
const CHn_AL2_TRANS_COUNT = 0x024; // Alias for channel n TRANS_COUNT register
const CHn_AL2_READ_ADDR = 0x028; // Alias for channel n READ_ADDR register
const CHn_AL2_WRITE_ADDR_TRIG = 0x02c; // Alias for channel n WRITE_ADDR register
const CHn_AL3_CTRL = 0x030; // Alias for channel n CTRL register
const CHn_AL3_WRITE_ADDR = 0x034; // Alias for channel n WRITE_ADDR register
const CHn_AL3_TRANS_COUNT = 0x038; // Alias for channel n TRANS_COUNT register
const CHn_AL3_READ_ADDR_TRIG = 0x03c; // Alias for channel n READ_ADDR register
const CHn_DBG_CTDREQ = 0x800;
const CHn_DBG_TCR = 0x804;
// General DMA registers
const INTR = 0x400; // Interrupt Status (raw)
const INTE0 = 0x404; // Interrupt Enables for IRQ 0
const INTF0 = 0x408; // Force Interrupts
const INTS0 = 0x40c; // Interrupt Status for IRQ 0
const INTE1 = 0x414; // Interrupt Enables for IRQ 1
const INTF1 = 0x418; // Force Interrupts for IRQ 1
const INTS1 = 0x41c; // Interrupt Status (masked) for IRQ 1
const INTE2 = 0x424; // Interrupt Enables for IRQ 2
const INTF2 = 0x428; // Force Interrupts for IRQ 2
const INTS2 = 0x42c; // Interrupt Status (masked) for IRQ 2
const INTE3 = 0x434; // Interrupt Enables for IRQ 3
const INTF3 = 0x438; // Force Interrupts for IRQ 3
const INTS3 = 0x43c; // Interrupt Status (masked) for IRQ 3
const TIMER0 = 0x440; // Pacing (X/Y) Fractional Timer
const TIMER1 = 0x444; // Pacing (X/Y) Fractional Timer
const TIMER2 = 0x448; // Pacing (X/Y) Fractional Timer
const TIMER3 = 0x44c; // Pacing (X/Y) Fractional Timer
const MULTI_CHAN_TRIGGER = 0x450; // Trigger one or more channels simultaneously
const SNIFF_CTRL = 0x454; // Sniffer Control
const SNIFF_DATA = 0x458; // Data accumulator for sniff hardware
const FIFO_LEVELS = 0x460; // Debug RAF, WAF, TDF levels
const CHAN_ABORT = 0x464; // Abort an in-progress transfer sequence on one or more channels
const N_CHANNELS = 0x468;
// CHn_CTRL_TRIG bits
const AHB_ERROR = 1 << 31;
const READ_ERROR = 1 << 30;
const WRITE_ERROR = 1 << 29;
const BUSY = 1 << 26;
const SNIFF_EN = 1 << 25;
const BSWAP = 1 << 24;
const IRQ_QUIET = 1 << 23;
const TREQ_SEL_MASK = 0x3f;
const TREQ_SEL_SHIFT = 17;
const CHAIN_TO_MASK = 0xf;
const CHAIN_TO_SHIFT = 13;
const RING_SEL = 1 << 12;
const RING_SIZE_MASK = 0xf;
const RING_SIZE_SHIFT = 8;
const INCR_WRITE_REV = 1 << 7; //TODO
const INCR_WRITE = 1 << 6;
const INCR_READ_REV = 1 << 5; //TODO
const INCR_READ = 1 << 4;
const DATA_SIZE_MASK = 0x3;
const DATA_SIZE_SHIFT = 2;
const HIGH_PRIORITY = 1 << 1;
const EN = 1 << 0;
const CHn_CTRL_TRIG_WRITE_MASK = 0xffffff;
const CHn_CTRL_TRIG_WC_MASK = READ_ERROR | WRITE_ERROR;
// Which transfer function fire() uses — resolved once when CTRL is written
// (matching real hardware, which captures it then rather than re-checking
// BSWAP on every fire()).
var TransferKind;
(function (TransferKind) {
    TransferKind[TransferKind["Direct8"] = 0] = "Direct8";
    TransferKind[TransferKind["Direct16"] = 1] = "Direct16";
    TransferKind[TransferKind["Swap16"] = 2] = "Swap16";
    TransferKind[TransferKind["Direct32"] = 3] = "Direct32";
    TransferKind[TransferKind["Swap32"] = 4] = "Swap32";
})(TransferKind || (TransferKind = {}));
export class RP2350DMAChannel {
    constructor(dma, rpchip, index) {
        this.dma = dma;
        this.rpchip = rpchip;
        this.index = index;
        this.ctrl = 0;
        this.readAddr = 0;
        this.writeAddr = 0;
        this.transCount = 0;
        this.dreqCounter = 0;
        this.transCountReload = 0;
        this.transMode = 0;
        this.treqValue = 0;
        this.dataSize = 1;
        this.transferKind = TransferKind.Direct8;
        this.chainTo = 0;
        this.ringMask = 0;
        this.transferAlarm = rpchip.clock.createAlarm(this);
        this.reset();
    }
    start() {
        if (!(this.ctrl & EN) || this.ctrl & BUSY) {
            return;
        }
        this.ctrl |= BUSY;
        this.transCount = this.transCountReload;
        if (this.transCount) {
            this.scheduleTransfer();
        }
    }
    get treq() {
        return this.treqValue;
    }
    get active() {
        return this.ctrl & EN && this.ctrl & BUSY;
    }
    transfer8() {
        const { rpchip } = this;
        rpchip.writeUint8(this.writeAddr, rpchip.readUint8(this.readAddr));
    }
    transfer16() {
        const { rpchip } = this;
        rpchip.writeUint16(this.writeAddr, rpchip.readUint16(this.readAddr));
    }
    transferSwap16() {
        const { rpchip } = this;
        const input = rpchip.readUint16(this.readAddr);
        rpchip.writeUint16(this.writeAddr, ((input & 0xff) << 8) | (input >> 8));
    }
    transfer32() {
        const { rpchip } = this;
        rpchip.writeUint32(this.writeAddr, rpchip.readUint32(this.readAddr));
    }
    transferSwap32() {
        const { rpchip } = this;
        const input = rpchip.readUint32(this.readAddr);
        rpchip.writeUint32(this.writeAddr, ((input & 0x000000ff) << 24) |
            ((input & 0x0000ff00) << 8) |
            ((input & 0x00ff0000) >> 8) |
            ((input >> 24) & 0xff));
    }
    fire() {
        const { ctrl, dataSize, ringMask } = this;
        switch (this.transferKind) {
            case TransferKind.Direct16:
                this.transfer16();
                break;
            case TransferKind.Swap16:
                this.transferSwap16();
                break;
            case TransferKind.Direct32:
                this.transfer32();
                break;
            case TransferKind.Swap32:
                this.transferSwap32();
                break;
            default:
                this.transfer8();
        }
        if (ctrl & INCR_READ) {
            if (ringMask && !(ctrl & RING_SEL)) {
                this.readAddr = (this.readAddr & ~ringMask) | ((this.readAddr + dataSize) & ringMask);
            }
            else {
                this.readAddr += dataSize;
            }
        }
        if (ctrl & INCR_WRITE) {
            if (ringMask && ctrl & RING_SEL) {
                this.writeAddr = (this.writeAddr & ~ringMask) | ((this.writeAddr + dataSize) & ringMask);
            }
            else {
                this.writeAddr += dataSize;
            }
        }
        if (this.transMode != 0xf) {
            this.transCount--;
        }
        if (this.transCount > 0) {
            this.scheduleTransfer();
        }
        else {
            if (!(this.ctrl & IRQ_QUIET)) {
                this.dma.intRaw |= 1 << this.index;
                this.dma.checkInterrupts();
            }
            if (this.chainTo !== this.index) {
                // `chainTo` is masked to CHAIN_TO_MASK (0-15) and `channels` is a full
                // Array(CHANNEL_COUNT === 16), so this index is always valid — no `?.`
                // needed (which cts2c can't transpile anyway).
                this.dma.channels[this.chainTo].start();
            }
            if (this.transMode == 1) {
                this.transCount = this.transCountReload;
                this.scheduleTransfer();
            }
            else {
                this.ctrl &= ~BUSY;
            }
        }
    }
    scheduleTransfer() {
        if (this.dma.dreq[this.treqValue] || this.treqValue === TREQ.Permanent) {
            this.transferAlarm.schedule(0);
        }
        else {
            const delay = this.dma.getTimer(this.treqValue);
            if (delay) {
                this.transferAlarm.schedule(delay * 1000);
            }
        }
    }
    abort() {
        this.ctrl &= ~BUSY;
        this.transferAlarm.cancel();
    }
    readUint32(offset) {
        switch (offset) {
            case CHn_READ_ADDR:
            case CHn_AL1_READ_ADDR:
            case CHn_AL2_READ_ADDR:
            case CHn_AL3_READ_ADDR_TRIG:
                return this.readAddr;
            case CHn_WRITE_ADDR:
            case CHn_AL1_WRITE_ADDR:
            case CHn_AL2_WRITE_ADDR_TRIG:
            case CHn_AL3_WRITE_ADDR:
                return this.writeAddr;
            case CHn_TRANS_COUNT:
            case CHn_AL1_TRANS_COUNT_TRIG:
            case CHn_AL2_TRANS_COUNT:
            case CHn_AL3_TRANS_COUNT:
                return this.transCount;
            case CHn_CTRL_TRIG:
            case CHn_AL1_CTRL:
            case CHn_AL2_CTRL:
            case CHn_AL3_CTRL:
                return this.ctrl;
            case CHn_DBG_CTDREQ:
                return this.dreqCounter;
            case CHn_DBG_TCR:
                return this.transCountReload;
        }
        return 0;
    }
    writeUint32(offset, value) {
        switch (offset) {
            case CHn_READ_ADDR:
            case CHn_AL1_READ_ADDR:
            case CHn_AL2_READ_ADDR:
            case CHn_AL3_READ_ADDR_TRIG:
                this.readAddr = value;
                break;
            case CHn_WRITE_ADDR:
            case CHn_AL1_WRITE_ADDR:
            case CHn_AL2_WRITE_ADDR_TRIG:
            case CHn_AL3_WRITE_ADDR:
                this.writeAddr = value;
                break;
            case CHn_TRANS_COUNT:
            case CHn_AL1_TRANS_COUNT_TRIG:
            case CHn_AL2_TRANS_COUNT:
            case CHn_AL3_TRANS_COUNT:
                this.transCountReload = value & 0x0fffffff;
                this.transMode = value >>> 28;
                break;
            case CHn_CTRL_TRIG:
            case CHn_AL1_CTRL:
            case CHn_AL2_CTRL:
            case CHn_AL3_CTRL: {
                this.ctrl = (this.ctrl & ~CHn_CTRL_TRIG_WRITE_MASK) | (value & CHn_CTRL_TRIG_WRITE_MASK);
                this.ctrl &= ~(value & CHn_CTRL_TRIG_WC_MASK); // Handle write-clear (WC) bits
                this.treqValue = (this.ctrl >> TREQ_SEL_SHIFT) & TREQ_SEL_MASK;
                this.chainTo = (this.ctrl >> CHAIN_TO_SHIFT) & CHAIN_TO_MASK;
                const ringSize = (this.ctrl >> RING_SIZE_SHIFT) & RING_SIZE_MASK;
                this.ringMask = ringSize ? (1 << ringSize) - 1 : 0;
                switch ((this.ctrl >> DATA_SIZE_SHIFT) & DATA_SIZE_MASK) {
                    case 1:
                        this.dataSize = 2;
                        this.transferKind = this.ctrl & BSWAP ? TransferKind.Swap16 : TransferKind.Direct16;
                        break;
                    case 2:
                        this.dataSize = 4;
                        this.transferKind = this.ctrl & BSWAP ? TransferKind.Swap32 : TransferKind.Direct32;
                        break;
                    case 0:
                    default:
                        this.dataSize = 1;
                        this.transferKind = TransferKind.Direct8;
                }
                if (this.ctrl & EN && this.ctrl & BUSY) {
                    this.scheduleTransfer();
                }
                if (!(this.ctrl & EN)) {
                    this.transferAlarm.cancel();
                }
                break;
            }
            case CHn_DBG_CTDREQ:
                this.dreqCounter = 0;
                break;
        }
        if (offset === CHn_AL3_READ_ADDR_TRIG ||
            offset === CHn_AL2_WRITE_ADDR_TRIG ||
            offset === CHn_AL1_TRANS_COUNT_TRIG ||
            offset === CHn_CTRL_TRIG) {
            if (value) {
                this.start();
            }
            else if (this.ctrl & IRQ_QUIET) {
                // Null trigger interrupts
                this.dma.intRaw |= 1 << this.index;
                this.dma.checkInterrupts();
            }
        }
    }
    reset() {
        this.writeUint32(CHn_CTRL_TRIG, this.index << CHAIN_TO_SHIFT);
    }
}
export class RP2350DMA extends BasePeripheral {
    constructor(rpchip, name, dma_irq_base) {
        super(rpchip, name);
        this.rpchip = rpchip;
        this.dma_irq_base = dma_irq_base;
        this.channels = Array(CHANNEL_COUNT)
            .fill(0)
            .map((v, i) => new RP2350DMAChannel(this, this.rpchip, i));
        this.intRaw = 0;
        this.intEnable = [0, 0, 0, 0];
        this.intForce = [0, 0, 0, 0];
        this.timer0 = 0;
        this.timer1 = 0;
        this.timer2 = 0;
        this.timer3 = 0;
        this.dreq = Array(DREQChannel.DREQ_MAX);
    }
    intStatus(intNum) {
        return (this.intRaw & this.intEnable[intNum]) | this.intForce[intNum];
    }
    intNumFromOffset(offset) {
        return ((offset - INTE0) / (INTE1 - INTE0)) >>> 0;
    }
    readUint32(offset) {
        if (offset % DEBUG_REGISTER_START < CHANNEL_COUNT * REGISTERS_SIZE_PER_CHANNEL) {
            // this handles both the normal CHn registers (starting at 0x0) as well as the CHn_DBG registers (starting at DEBUG_REGISTER_START)
            const channelIndex = ((offset % DEBUG_REGISTER_START) / REGISTERS_SIZE_PER_CHANNEL) >>> 0;
            return this.channels[channelIndex].readUint32(offset & (DEBUG_REGISTER_START | (REGISTERS_SIZE_PER_CHANNEL - 1)));
        }
        switch (offset) {
            case TIMER0:
                return this.timer0;
            case TIMER1:
                return this.timer1;
            case TIMER2:
                return this.timer2;
            case TIMER3:
                return this.timer3;
            case INTR:
                return this.intRaw;
            case INTE0:
            case INTE1:
            case INTE2:
            case INTE3:
                return this.intEnable[this.intNumFromOffset(offset)];
            case INTF0:
            case INTF1:
            case INTF2:
            case INTF3:
                return this.intForce[this.intNumFromOffset(offset)];
            case INTS0:
            case INTS1:
            case INTS2:
            case INTS3:
                return this.intStatus(this.intNumFromOffset(offset));
            case CHAN_ABORT:
                return 0;
            case N_CHANNELS:
                return this.channels.length;
        }
        return super.readUint32(offset);
    }
    writeUint32(offset, value) {
        if (offset % DEBUG_REGISTER_START < CHANNEL_COUNT * REGISTERS_SIZE_PER_CHANNEL) {
            // this handles both the normal CHn registers (starting at 0x0) as well as the CHn_DBG registers (starting at DEBUG_REGISTER_START)
            const channelIndex = ((offset % DEBUG_REGISTER_START) / REGISTERS_SIZE_PER_CHANNEL) >>> 0;
            this.channels[channelIndex].writeUint32(offset & (DEBUG_REGISTER_START | (REGISTERS_SIZE_PER_CHANNEL - 1)), value);
            return;
        }
        switch (offset) {
            case TIMER0:
                this.timer0 = value;
                return;
            case TIMER1:
                this.timer1 = value;
                return;
            case TIMER2:
                this.timer2 = value;
                return;
            case TIMER3:
                this.timer3 = value;
                return;
            case INTR:
            case INTS0:
            case INTS1:
            case INTS2:
            case INTS3:
                this.intRaw &= ~this.rawWriteValue;
                this.checkInterrupts();
                return;
            case INTE0:
            case INTE1:
            case INTE2:
            case INTE3:
                this.intEnable[this.intNumFromOffset(offset)] = value & 0xffff;
                this.checkInterrupts();
                return;
            case INTF0:
            case INTF1:
            case INTF2:
            case INTF3:
                this.intForce[this.intNumFromOffset(offset)] = value & 0xffff;
                this.checkInterrupts();
                return;
            case MULTI_CHAN_TRIGGER:
                for (const chan of this.channels) {
                    if (value & (1 << chan.index)) {
                        chan.start();
                    }
                }
                return;
            case CHAN_ABORT:
                for (const chan of this.channels) {
                    if (value & (1 << chan.index)) {
                        chan.abort();
                    }
                }
                return;
            default:
                super.writeUint32(offset, value);
        }
    }
    setDREQ(dreqChannel) {
        const { dreq } = this;
        if (!dreq[dreqChannel]) {
            dreq[dreqChannel] = true;
            for (const channel of this.channels) {
                if (channel.treq === dreqChannel && channel.active) {
                    channel.scheduleTransfer();
                }
            }
        }
    }
    clearDREQ(dreqChannel) {
        this.dreq[dreqChannel] = false;
    }
    /**
     * Returns the number of microseconds for a cycle of the given DMA timer, or 0 if the timer is disabled.
     */
    getTimer(treq) {
        let dividend = 0, divisor = 1;
        switch (treq) {
            case TREQ.Permanent:
                dividend = 1;
                divisor = 1;
                break;
            case TREQ.Timer0:
                dividend = this.timer0 >>> 16;
                divisor = this.timer0 & 0xffff;
                break;
            case TREQ.Timer1:
                dividend = this.timer1 >>> 16;
                divisor = this.timer1 & 0xffff;
                break;
            case TREQ.Timer2:
                dividend = this.timer2 >>> 16;
                divisor = this.timer2 & 0xffff;
                break;
            case TREQ.Timer3:
                dividend = this.timer3 >>> 16;
                divisor = this.timer3 & 0xffff;
                break;
        }
        if (divisor === 0) {
            return 0;
        }
        return ((dividend / divisor) * 1e6) / this.rpchip.clkSys;
    }
    checkInterrupts() {
        this.rpchip.setInterrupt(this.dma_irq_base + 0, !!this.intStatus(0));
        this.rpchip.setInterrupt(this.dma_irq_base + 1, !!this.intStatus(1));
        this.rpchip.setInterrupt(this.dma_irq_base + 2, !!this.intStatus(2));
        this.rpchip.setInterrupt(this.dma_irq_base + 3, !!this.intStatus(3));
    }
}
