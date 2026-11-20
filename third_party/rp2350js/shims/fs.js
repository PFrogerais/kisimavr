// Node "fs" is not available in QuickJS: the bridge loads the firmware from bytes.
function unavailable() {
  throw new Error('fs is not available in the co-simulation');
}
export const existsSync = () => false;
export const readFileSync = unavailable;
export const writeFileSync = unavailable;
