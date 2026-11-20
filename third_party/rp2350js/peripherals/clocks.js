import { BasePeripheral } from './peripheral.js';
const CLK_REF_CTRL = 0x30;
const CLK_REF_SELECTED = 0x38;
const CLK_SYS_CTRL = 0x3c;
const CLK_SYS_SELECTED = 0x44;
export class RPClocks extends BasePeripheral {
    constructor(rpchip, name) {
        super(rpchip, name);
        this.refCtrl = 0;
        this.sysCtrl = 0;
        this.clkFc0StatusOffset = 0;
        // Dynamic switch case offset — use if/else instead for C compatibility
        if (rpchip.identifier === 'rp2350') {
            this.clkFc0StatusOffset = 0xa4;
        }
        else {
            this.clkFc0StatusOffset = 0x98;
        }
    }
    readUint32(offset) {
        if (offset === this.clkFc0StatusOffset) {
            return 0b10001; // done, passed
        }
        switch (offset) {
            case CLK_REF_CTRL:
                return this.refCtrl;
            case CLK_REF_SELECTED:
                return 1 << (this.refCtrl & 0x03);
            case CLK_SYS_CTRL:
                return this.sysCtrl;
            case CLK_SYS_SELECTED:
                return 1 << (this.sysCtrl & 0x01);
        }
        return super.readUint32(offset);
    }
    writeUint32(offset, value) {
        switch (offset) {
            case CLK_REF_CTRL:
                this.refCtrl = value;
                break;
            case CLK_SYS_CTRL:
                this.sysCtrl = value;
                break;
            default:
                super.writeUint32(offset, value);
                break;
        }
    }
}
