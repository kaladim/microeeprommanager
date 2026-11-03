// Tests of the CRCs. Run with: node --test
const test = require('node:test');
const assert = require('node:assert');
const zlib = require('node:zlib');

// The common modules are classic scripts, contributing to the global MeemCommon namespace
require('../crc.js');
const MeemCommon = globalThis.MeemCommon;

const CHECK_INPUT = new TextEncoder().encode('123456789');

/** Bitwise CRC-16/IBM-3740, the reference for the table-driven one */
function referenceCrc16(bytes) {
    let crc = 0xFFFF;
    for (const byte of bytes) {
        crc ^= byte << 8;
        for (let i = 0; i < 8; i++) {
            crc = (crc & 0x8000) ? (((crc << 1) ^ 0x1021) & 0xFFFF) : ((crc << 1) & 0xFFFF);
        }
    }
    return crc;
}

/** Deterministic pseudo-random bytes (xorshift32), so that a failure reproduces */
function randomBytes(length, seed) {
    const bytes = new Uint8Array(length);
    let x = seed;
    for (let i = 0; i < length; i++) {
        x ^= x << 13;
        x ^= x >>> 17;
        x ^= x << 5;
        bytes[i] = x & 0xFF;
    }
    return bytes;
}

test('CRC-16/IBM-3740 gives its check value', () => {
    assert.strictEqual(MeemCommon.crc16(CHECK_INPUT), 0x29B1);
});

test('CRC-32/ISO-HDLC gives its check value', () => {
    assert.strictEqual(MeemCommon.crc32(CHECK_INPUT), 0xCBF43926);
});

test('the CRCs of no data are their initial values, after the final XOR', () => {
    assert.strictEqual(MeemCommon.crc16(new Uint8Array(0)), 0xFFFF);
    assert.strictEqual(MeemCommon.crc32(new Uint8Array(0)), 0);
});

test('the CRCs take arrays of byte values as well as Uint8Arrays', () => {
    assert.strictEqual(MeemCommon.crc16(Array.from(CHECK_INPUT)), 0x29B1);
    assert.strictEqual(MeemCommon.crc32(Array.from(CHECK_INPUT)), 0xCBF43926);
});

test('CRC-16 continues a calculation from a previous result', () => {
    const data = randomBytes(100, 1);

    assert.strictEqual(MeemCommon.crc16(data.subarray(37), MeemCommon.crc16(data.subarray(0, 37))), MeemCommon.crc16(data));
});

test('CRC-16 agrees with a bitwise calculation on random data', () => {
    for (let length = 0; length <= 300; length++) {
        const data = randomBytes(length, length + 1);
        assert.strictEqual(MeemCommon.crc16(data), referenceCrc16(data), 'length ' + length);
    }
});

test('CRC-32 agrees with zlib on random data', { skip: typeof zlib.crc32 !== 'function' && 'zlib.crc32() needs Node.js 22.2' }, () => {
    for (let length = 0; length <= 300; length++) {
        const data = randomBytes(length, length + 1);
        assert.strictEqual(MeemCommon.crc32(data), zlib.crc32(data), 'length ' + length);
    }
});
