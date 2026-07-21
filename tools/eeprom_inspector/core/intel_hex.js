/*
 * mEEM EEPROM Inspector - Intel HEX parser.
 *
 * Parses Intel HEX text into a contiguous Uint8Array. Supports record types
 * 0x00 (data), 0x01 (EOF), 0x02 (extended segment address) and 0x04 (extended
 * linear address); start-address records (0x03/0x05) are ignored. Verifies each
 * record's checksum.
 *
 * Part of the split `core/` business logic. Classic (non-module) script that
 * contributes to the shared global `EEPROM` namespace. Uses
 * EEPROM.sparseToContiguous (from image_loader.js, loaded earlier).
 */
(function (EEPROM) {
    'use strict';

    function parseIntelHex(text) {
        const sparse = new Map();
        let baseAddress = 0; // from type 02 / 04 records
        const lines = text.split(/\r?\n/);

        for (let ln = 0; ln < lines.length; ln++) {
            const line = lines[ln].trim();
            if (line === '') continue;
            if (line[0] !== ':') {
                throw new Error('Intel HEX: line ' + (ln + 1) + ' does not start with ":"');
            }
            const bytes = [];
            for (let i = 1; i + 1 < line.length; i += 2) {
                bytes.push(parseInt(line.substr(i, 2), 16));
            }
            if (bytes.length < 5) {
                throw new Error('Intel HEX: line ' + (ln + 1) + ' is too short');
            }
            const count = bytes[0];
            const addr = (bytes[1] << 8) | bytes[2];
            const type = bytes[3];

            // Checksum verification (two's complement of the sum)
            let sum = 0;
            for (let i = 0; i < bytes.length; i++) sum = (sum + bytes[i]) & 0xFF;
            if (sum !== 0) {
                throw new Error('Intel HEX: checksum error on line ' + (ln + 1));
            }

            if (type === 0x00) {
                for (let i = 0; i < count; i++) {
                    sparse.set(baseAddress + addr + i, bytes[4 + i]);
                }
            } else if (type === 0x01) {
                break; // EOF
            } else if (type === 0x02) {
                // Extended Segment Address
                baseAddress = ((bytes[4] << 8) | bytes[5]) << 4;
            } else if (type === 0x04) {
                // Extended Linear Address
                baseAddress = ((bytes[4] << 8) | bytes[5]) << 16;
            }
            // types 0x03 / 0x05 (start address) are ignored
        }
        return EEPROM.sparseToContiguous(sparse);
    }

    EEPROM.parseIntelHex = parseIntelHex;

})(globalThis.EEPROM = globalThis.EEPROM || {});
