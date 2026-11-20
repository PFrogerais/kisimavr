import { BasePeripheral } from './peripheral.js';
export class RP2350SysCfg extends BasePeripheral {
    readUint32(offset) {
        if (offset === 0)
            return 0;
        return super.readUint32(offset);
    }
    writeUint32(offset, value) {
        super.writeUint32(offset, value);
    }
}
