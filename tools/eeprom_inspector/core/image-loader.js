/*
 * mEEM EEPROM Inspector - EEPROM image loading (replaces the Python `bincopy`).
 *
 * Detects the image format by file extension and dispatches to the Intel HEX
 * or Motorola S-record parser, or reads raw binary. Also provides the shared
 * sparse-to-contiguous helper used by both text parsers: for hex formats,
 * bincopy produced a contiguous binary from the lowest to the highest
 * populated address, padding gaps with 0xFF - this reproduces that.
 *
 * Part of the split `core/` business logic. Classic (non-module) script that
 * contributes to the shared global `EEPROM` namespace. Loaded before the
 * Intel HEX / S-record parsers (which use sparseToContiguous); the dispatcher
 * references those parsers lazily, at call time.
 */
(function (EEPROM) {
    'use strict';

    /**
     * Convert a sparse address->byte map to a contiguous Uint8Array spanning
     * the lowest..highest populated address, padding gaps with 0xFF.
     */
    function sparseToContiguous(sparse) {
        // sparse: Map<address:number, byte:number>
        if (sparse.size === 0) return new Uint8Array(0);
        let min = Infinity;
        let max = -Infinity;
        for (const addr of sparse.keys()) {
            if (addr < min) min = addr;
            if (addr > max) max = addr;
        }
        const out = new Uint8Array(max - min + 1).fill(0xFF);
        for (const [addr, byte] of sparse) {
            out[addr - min] = byte;
        }
        return out;
    }

    /**
     * Load an EEPROM image. `name` is used to detect the format by extension.
     * `arrayBuffer` holds the file bytes (binary or text-format source).
     * Returns a Uint8Array.
     */
    function loadEepromImage(name, arrayBuffer) {
        const ext = (name.split('.').pop() || '').toLowerCase();
        const textExts = ['hex', 'ihex', 'srec', 's19', 's28', 's37', 's', 'mot', 'srg'];
        if (textExts.indexOf(ext) !== -1) {
            const text = new TextDecoder('utf-8').decode(new Uint8Array(arrayBuffer));
            if (ext === 'hex' || ext === 'ihex') return EEPROM.parseIntelHex(text);
            return EEPROM.parseSRecord(text);
        }
        // Raw binary
        return new Uint8Array(arrayBuffer);
    }

    EEPROM.sparseToContiguous = sparseToContiguous;
    EEPROM.loadEepromImage = loadEepromImage;

})(globalThis.EEPROM = globalThis.EEPROM || {});
