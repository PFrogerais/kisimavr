const ATOMIC_NORMAL = 0;
const ATOMIC_XOR = 1;
const ATOMIC_SET = 2;
const ATOMIC_CLEAR = 3;
export function atomicUpdate(currentValue, atomicType, newValue) {
    switch (atomicType) {
        case ATOMIC_XOR:
            return currentValue ^ newValue;
        case ATOMIC_SET:
            return currentValue | newValue;
        case ATOMIC_CLEAR:
            return currentValue & ~newValue;
        default:
            console.warn('Atomic update called with invalid writeType', atomicType);
            return newValue;
    }
}
export class BasePeripheral {
    constructor(rpchip, name) {
        this.rpchip = rpchip;
        this.name = name;
        this.rawWriteValue = 0;
    }
    byteAddressable() {
        return false;
    }
    readUint32(offset) {
        this.warn(`Unimplemented peripheral read from ${offset.toString(16)}`);
        if (offset > 0x1000) {
            this.warn('Unimplemented read from peripheral in the atomic operation region');
        }
        return 0xffffffff;
    }
    readUint32ViaCore(offset, core) {
        this.warn(`Unimplemented peripheral readvia ${core} from 0x${offset.toString(16)}`);
        return 0xffffffff;
    }
    writeUint32(offset, value) {
        this.warn(`Unimplemented peripheral write to 0x${offset.toString(16)}: ${value}`);
    }
    writeUint32ViaCore(offset, value, core) {
        this.warn(`Unimplemented peripheral write via ${core} to 0x${offset.toString(16)}: ${value}`);
    }
    writeUint32Atomic(offset, value, atomicType) {
        this.rawWriteValue = value;
        const newValue = atomicType != ATOMIC_NORMAL
            ? atomicUpdate(this.readUint32(offset), atomicType, value)
            : value;
        this.writeUint32(offset, newValue);
    }
    debug(msg) {
        this.rpchip.logger.debug(this.name, msg);
    }
    info(msg) {
        this.rpchip.logger.info(this.name, msg);
    }
    warn(msg) {
        this.rpchip.logger.warn(this.name, msg);
    }
    error(msg) {
        this.rpchip.logger.error(this.name, msg);
    }
}
export class UnimplementedPeripheral extends BasePeripheral {
}
