/*
 * mEEM EEPROM Inspector - Motorola S-record parser.
 *
 * Parses Motorola S-record text (S19/S28/S37) into a contiguous Uint8Array.
 * Data-carrying records S1/S2/S3 (2/3/4-byte addresses) are used; header (S0),
 * count (S5/S6) and start-address (S7/S8/S9) records are ignored. Verifies each
 * record's checksum.
 *
 * Part of the split `core/` business logic. Classic (non-module) script that
 * contributes to the shared global `EEPROM` namespace. Uses
 * EEPROM.sparseToContiguous (from image_loader.js, loaded earlier).
 */
(function (EEPROM) {
    'use strict';

    function parseSRecord(text) {
        const sparse = new Map();
        const lines = text.split(/\r?\n/);

        for (let ln = 0; ln < lines.length; ln++) {
            const line = lines[ln].trim();
            if (line === '') continue;
            if (line[0] !== 'S') {
                throw new Error('S-record: line ' + (ln + 1) + ' does not start with "S"');
            }
            const type = line[1];
            const bytes = [];
            for (let i = 2; i + 1 < line.length; i += 2) {
                bytes.push(parseInt(line.substr(i, 2), 16));
            }
            if (bytes.length < 1) continue;

            // Checksum verification (one's complement of the sum of count..data)
            let sum = 0;
            for (let i = 0; i < bytes.length; i++) sum = (sum + bytes[i]) & 0xFF;
            if (sum !== 0xFF) {
                throw new Error('S-record: checksum error on line ' + (ln + 1));
            }

            let addrLen;
            if (type === '1' || type === '9') addrLen = 2;
            else if (type === '2' || type === '8') addrLen = 3;
            else if (type === '3' || type === '7') addrLen = 4;
            else continue; // S0 (header), S5/S6 (count) - ignored

            // data-carrying records only (S1/S2/S3)
            if (type !== '1' && type !== '2' && type !== '3') continue;

            let addr = 0;
            for (let i = 0; i < addrLen; i++) addr = (addr * 256) + bytes[1 + i];
            const dataStart = 1 + addrLen;
            const dataEnd = bytes.length - 1; // exclude trailing checksum
            for (let i = dataStart; i < dataEnd; i++) {
                sparse.set(addr + (i - dataStart), bytes[i]);
            }
        }
        return EEPROM.sparseToContiguous(sparse);
    }

    EEPROM.parseSRecord = parseSRecord;

})(globalThis.EEPROM = globalThis.EEPROM || {});
