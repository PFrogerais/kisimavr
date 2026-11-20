import { BasePeripheral } from './peripheral.js';
const VOLTAGE_SELECT = 0;
const BANK0_FIRST = 0x4;
const BANK0_LAST = 0xc0;
const QSPI_FIRST = 0x4;
const QSPI_LAST = 0x18;
export class RP2350PADS extends BasePeripheral {
    constructor(rpchip, name, bank) {
        super(rpchip, name);
        this.bank = bank;
        this.voltageSelect = 0;
        this.firstPadRegister = this.bank === 'qspi' ? QSPI_FIRST : BANK0_FIRST;
        this.lastPadRegister = this.bank === 'qspi' ? QSPI_LAST : BANK0_LAST;
    }
    getPinFromOffset(offset) {
        const gpioIndex = (offset - this.firstPadRegister) >>> 2;
        if (this.bank === 'qspi') {
            return this.rpchip.qspi[gpioIndex];
        }
        else {
            return this.rpchip.gpio[gpioIndex];
        }
    }
    readUint32(offset) {
        if (offset >= this.firstPadRegister && offset <= this.lastPadRegister) {
            const gpio = this.getPinFromOffset(offset);
            return gpio.padValue;
        }
        switch (offset) {
            case VOLTAGE_SELECT:
                return this.voltageSelect;
        }
        return super.readUint32(offset);
    }
    writeUint32(offset, value) {
        if (offset >= this.firstPadRegister && offset <= this.lastPadRegister) {
            const gpio = this.getPinFromOffset(offset);
            const oldInputEnable = gpio.inputEnable;
            gpio.padValue = value;
            gpio.checkForUpdates();
            if (oldInputEnable !== gpio.inputEnable) {
                gpio.refreshInput();
            }
            return;
        }
        switch (offset) {
            case VOLTAGE_SELECT:
                this.voltageSelect = value & 1;
                break;
            default:
                super.writeUint32(offset, value);
        }
    }
}
