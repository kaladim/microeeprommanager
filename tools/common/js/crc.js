/*
 * mEEM web tools - CRCs.
 *
 * The CRCs of mEEM v2. Port of common/py/crc.py:
 * - CRC-16/IBM-3740: the instance checksum and the hash of the data and block
 *   identities. Poly 0x1021, init 0xFFFF, no reflection, no final XOR, check
 *   value 0x29B1 for "123456789".
 * - CRC-32/ISO-HDLC: the hash of the EEPROM layout identity, as Python's
 *   zlib.crc32(). Poly 0x04C11DB7, init 0xFFFFFFFF, reflected input and output,
 *   final XOR 0xFFFFFFFF, check value 0xCBF43926.
 *
 * Classic (non-module) script, so it works from file://, that contributes to
 * the global `MeemCommon` namespace. No dependencies.
 */
(function (MeemCommon) {
    'use strict';

    // ---- CRC-16/IBM-3740 (mirror common/py/crc.py) -------------------------

    const CRC16_POLYNOMIAL = 0x1021;
    const CRC16_INITIAL_VALUE = 0xFFFF;

    // The CRC register after shifting each byte value through it
    function makeCrc16Table() {
        const table = new Uint16Array(256);
        for (let byte = 0; byte < 256; byte++) {
            let crc = byte << 8;
            for (let i = 0; i < 8; i++) {
                crc = (crc & 0x8000) ? ((crc << 1) ^ CRC16_POLYNOMIAL) : (crc << 1);
            }
            table[byte] = crc & 0xFFFF;
        }
        return table;
    }

    const CRC16_TABLE = makeCrc16Table();

    /**
     * CRC-16/IBM-3740 of the bytes (a Uint8Array or an array of byte values).
     * Pass a previous result as `crc` to continue a calculation over more data.
     */
    function crc16(bytes, crc) {
        if (crc === undefined) crc = CRC16_INITIAL_VALUE;
        for (let i = 0; i < bytes.length; i++) {
            crc = ((crc << 8) & 0xFFFF) ^ CRC16_TABLE[(crc >> 8) ^ bytes[i]];
        }
        return crc;
    }

    // ---- CRC-32/ISO-HDLC (mirror zlib.crc32()) -----------------------------

    const CRC32_POLYNOMIAL_REFLECTED = 0xEDB88320; // 0x04C11DB7, bit-reversed

    function makeCrc32Table() {
        const table = new Uint32Array(256);
        for (let byte = 0; byte < 256; byte++) {
            let crc = byte;
            for (let i = 0; i < 8; i++) {
                crc = (crc & 1) ? ((crc >>> 1) ^ CRC32_POLYNOMIAL_REFLECTED) : (crc >>> 1);
            }
            table[byte] = crc;
        }
        return table;
    }

    const CRC32_TABLE = makeCrc32Table();

    /** CRC-32/ISO-HDLC of the bytes (a Uint8Array or an array of byte values) */
    function crc32(bytes) {
        let crc = 0xFFFFFFFF;
        for (let i = 0; i < bytes.length; i++) {
            crc = CRC32_TABLE[(crc ^ bytes[i]) & 0xFF] ^ (crc >>> 8);
        }
        return (crc ^ 0xFFFFFFFF) >>> 0;
    }

    MeemCommon.crc16 = crc16;
    MeemCommon.crc32 = crc32;

})(globalThis.MeemCommon = globalThis.MeemCommon || {});
