/*
 * mEEM EEPROM Inspector - CRC / checksum.
 *
 * Exact port of the Python `crcmod` (mkCrcFun + table build), using BigInt so
 * that all supported widths (8/16/24/32/64) are computed precisely, plus the
 * ChecksumAlgorithm wrapper (mirror common/checksum_algo.py). Only the "crc"
 * algorithm is implemented, as in the original.
 *
 * Part of the split `core/` business logic. Classic (non-module) script that
 * contributes to the shared global `EEPROM` namespace. No dependencies.
 */
(function (EEPROM) {
    'use strict';

    function verifyPoly(poly) {
        // poly is a BigInt. Degree must be 8, 16, 24, 32 or 64.
        for (const n of [8, 16, 24, 32, 64]) {
            const low = 1n << BigInt(n);
            const high = low * 2n;
            if (low <= poly && poly < high) return n;
        }
        throw new Error('The degree of the polynomial must be 8, 16, 24, 32 or 64');
    }

    function bitrev(x, n) {
        let y = 0n;
        for (let i = 0; i < n; i++) {
            y = (y << 1n) | (x & 1n);
            x = x >> 1n;
        }
        return y;
    }

    function byteCrc(crc, poly, n) {
        const topBit = 1n << BigInt(n - 1);
        const mask = (1n << BigInt(n)) - 1n;
        for (let i = 0; i < 8; i++) {
            if (crc & topBit) crc = (crc << 1n) ^ poly;
            else crc = crc << 1n;
        }
        return crc & mask;
    }

    function byteCrcR(crc, poly, n) {
        const mask = (1n << BigInt(n)) - 1n;
        for (let i = 0; i < 8; i++) {
            if (crc & 1n) crc = (crc >> 1n) ^ poly;
            else crc = crc >> 1n;
        }
        return crc & mask;
    }

    function mkTable(poly, n) {
        const mask = (1n << BigInt(n)) - 1n;
        poly = poly & mask;
        const table = new Array(256);
        for (let i = 0; i < 256; i++) {
            table[i] = byteCrc(BigInt(i) << BigInt(n - 8), poly, n);
        }
        return table;
    }

    function mkTableR(poly, n) {
        const mask = (1n << BigInt(n)) - 1n;
        poly = bitrev(poly & mask, n);
        const table = new Array(256);
        for (let i = 0; i < 256; i++) {
            table[i] = byteCrcR(BigInt(i), poly, n);
        }
        return table;
    }

    /**
     * Build a CRC function equivalent to crcmod.mkCrcFun(poly, initCrc, rev, xorOut).
     * Returns a function(bytes, [crc]) -> BigInt CRC value.
     */
    function makeCrcFun(polyNum, initCrcNum, rev, xorOutNum) {
        const poly = BigInt(polyNum);
        const n = verifyPoly(poly);
        const mask = (1n << BigInt(n)) - 1n;
        const shift = BigInt(n - 8);

        const initCrc = BigInt(initCrcNum) & mask;
        const xorOut = BigInt(xorOutNum) & mask;

        const table = rev ? mkTableR(poly, n) : mkTable(poly, n);

        function core(bytes, crc) {
            if (rev) {
                for (let k = 0; k < bytes.length; k++) {
                    const x = BigInt(bytes[k]);
                    crc = table[Number((crc ^ x) & 0xFFn)] ^ (crc >> 8n);
                }
            } else {
                for (let k = 0; k < bytes.length; k++) {
                    const x = BigInt(bytes[k]);
                    crc = table[Number(((crc >> shift) ^ x) & 0xFFn)] ^ ((crc << 8n) & mask);
                }
            }
            return crc & mask;
        }

        return function (bytes, crc) {
            const start = (crc === undefined ? initCrc : (BigInt(crc) & mask));
            if (xorOut === 0n) return core(bytes, start);
            return xorOut ^ core(bytes, xorOut ^ start);
        };
    }

    /**
     * ChecksumAlgorithm - mirror common/checksum_algo.py.
     * Only the "crc" algorithm is implemented (as in the original).
     */
    function ChecksumAlgorithm(params) {
        if (!params || typeof params.algo !== 'string') {
            throw new Error("checksum parameters: missing or invalid 'algo'");
        }
        if (params.algo.toLowerCase() !== 'crc') {
            throw new Error('Not implemented checksum algo: ' + params.algo);
        }
        for (const key of ['polynomial', 'initial_value', 'reverse_input', 'final_xor_value']) {
            if (!(key in params)) throw new Error("checksum parameters: missing '" + key + "'");
        }

        const fn = makeCrcFun(
            params.polynomial,
            params.initial_value,
            !!params.reverse_input,
            params.final_xor_value
        );

        this.parameters = params;
        this.calculate = function (bytes) {
            // Widths used for checksums (1/2/4 bytes) fit into a JS Number.
            return Number(fn(bytes));
        };
    }

    EEPROM.makeCrcFun = makeCrcFun;
    EEPROM.ChecksumAlgorithm = ChecksumAlgorithm;

})(globalThis.EEPROM = globalThis.EEPROM || {});
