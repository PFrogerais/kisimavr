import { BasePeripheral } from './peripheral.js';
const BOOTLOCK_ST = 0x808;
const BOOTLOCK0 = 0x80c;
const BOOTLOCK7 = 0x828;
const WRITE_ONCE0 = 0x800;
const WRITE_ONCE1 = 0x804;
export class RPBootRAM extends BasePeripheral {
    constructor() {
        super(...arguments);
        this.bootram = Array(256).fill(0);
        this.write_once = [0, 0];
        this.spinLock = 0;
    }
    byteAddressable() {
        return true;
    }
    readUint32(offset) {
        if (offset >= BOOTLOCK0 && offset <= BOOTLOCK7) {
            const bitIndexMask = 1 << ((offset - BOOTLOCK0) / 4);
            if (this.spinLock & bitIndexMask) {
                return 0;
            }
            else {
                this.spinLock |= bitIndexMask;
                return bitIndexMask;
            }
        }
        if (offset == BOOTLOCK_ST)
            return this.spinLock;
        if (offset >= WRITE_ONCE0 && offset <= WRITE_ONCE1) {
            return this.write_once[(offset - WRITE_ONCE0) >>> 2];
        }
        return this.bootram[offset >>> 2];
    }
    writeUint32(offset, value) {
        if (offset >= BOOTLOCK0 && offset <= BOOTLOCK7) {
            const bitIndexMask = ~(1 << ((offset - BOOTLOCK0) / 4));
            this.spinLock &= bitIndexMask;
            return;
        }
        if (offset >= WRITE_ONCE0 && offset <= WRITE_ONCE1) {
            this.write_once[(offset - WRITE_ONCE0) >>> 2] |= value;
            return;
        }
        this.bootram[offset >>> 2] = value;
    }
}
