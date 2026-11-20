// The "uf2" npm package is not vendored: the bridge decodes UF2 files itself.
export function decodeBlock() {
  throw new Error('uf2 is not available in the co-simulation');
}
