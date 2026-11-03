/*
 * mEEM web tools - identities.
 *
 * The canonical descriptors of mEEM v2 and their hashes, as the code generator
 * computes them. Port of common/py/identity.py:
 * - Data identity: CRC-16/IBM-3740 of the data descriptor, the ordered
 *   parameters and bitfields of a block.
 * - Block identity: CRC-16/IBM-3740 of the block descriptor: block name,
 *   management type, instance count and data identity.
 * - EEPROM layout identity: CRC-32/ISO-HDLC of the layout descriptor: the
 *   endianness, and every block identity with its offset.
 * - Identity header: the 6 bytes at EEPROM offset 0, laid out like an instance:
 *   the CRC-16 over the identity, then the identity, both little-endian on
 *   every target.
 *
 * Descriptors are UTF-8, with their lines joined by '\n', and names stripped as
 * Python's str.strip() strips them. Its tests run on the shared vectors of
 * test/vectors/identity_vectors.json, as the Python tests do.
 *
 * Classic (non-module) script, so it works from file://, that contributes to
 * the global `MeemCommon` namespace. Depends on data-model.js and crc.js (both
 * loaded earlier).
 */
(function (MeemCommon) {
    'use strict';

    const IDENTITY_HEADER_SIZE = MeemCommon.IDENTITY_HEADER_SIZE;
    const dataTypeName = MeemCommon.dataTypeName;
    const managementTypeName = MeemCommon.managementTypeName;
    const multiplicityOf = MeemCommon.multiplicityOf;
    const instanceCountOf = MeemCommon.instanceCountOf;
    const blockOffsets = MeemCommon.blockOffsets;
    const crc16 = MeemCommon.crc16;
    const crc32 = MeemCommon.crc32;

    const DATA_DESCRIPTOR_TAG = 'mEEM-data-v2';
    const BLOCK_DESCRIPTOR_TAG = 'mEEM-block-v2';
    const LAYOUT_DESCRIPTOR_TAG = 'mEEM-layout-v2';

    // The characters for which Python's str.isspace() is true. String.prototype.trim()
    // differs: it keeps U+001C-U+001F and U+0085, and strips U+FEFF.
    const PYTHON_WHITESPACE = '\\t\\n\\x0B\\x0C\\r\\x1C-\\x1F \\x85\\xA0\\u1680\\u2000-\\u200A\\u2028\\u2029\\u202F\\u205F\\u3000';
    const STRIP_PATTERN = new RegExp('^[' + PYTHON_WHITESPACE + ']+|[' + PYTHON_WHITESPACE + ']+$', 'g');

    const utf8 = new TextEncoder();

    function strip(name) {
        return String(name ?? '').replace(STRIP_PATTERN, '');
    }

    function toHex(value, digits) {
        return value.toString(16).toUpperCase().padStart(digits, '0');
    }

    // ---- Data and block identity -------------------------------------------

    /** The canonical descriptor of the block's parameters: each parameter, directly followed by its bitfields */
    function dataDescriptor(block) {
        const lines = [DATA_DESCRIPTOR_TAG];

        for (const param of block.children ?? []) {
            lines.push('param=' + strip(param.name) + ',' + dataTypeName(param.data_type) + ',' + multiplicityOf(param));
            for (const bitfield of param.children ?? []) {
                lines.push('bitfield=' + strip(bitfield.name) + ',' + Number(bitfield.size_in_bits));
            }
        }
        return lines.join('\n');
    }

    /** 16-bit hash of the data descriptor. Blocks with the same data identity hold the same parameters. */
    function dataIdentity(block) {
        return crc16(utf8.encode(dataDescriptor(block)));
    }

    /** The canonical descriptor of the block. Not its offset: the location doesn't change the content. */
    function blockDescriptor(block) {
        return [
            BLOCK_DESCRIPTOR_TAG,
            'block=' + strip(block.name),
            'mgmt=' + managementTypeName(block.management_type),
            'count=' + instanceCountOf(block),
            'data=' + toHex(dataIdentity(block), 4),
        ].join('\n');
    }

    /** 16-bit hash of the block descriptor. Blocks with the same block identity hold the same bytes in the same way. */
    function blockIdentity(block) {
        return crc16(utf8.encode(blockDescriptor(block)));
    }

    // ---- EEPROM layout identity and identity header ------------------------

    /**
     * The canonical descriptor of the EEPROM layout: every block identity with its offset, in definition order.
     * Unlike Python's, it takes the platform settings and computes the offsets itself, so nothing is attached to
     * the data model: the configurator saves the object that it computes the identities of.
     */
    function layoutDescriptor(datamodel, settings) {
        const endianness = settings.endianness ?? 'little';
        if (endianness !== 'little' && endianness !== 'big') {
            throw new Error("Unknown endianness '" + endianness + "': 'little' or 'big' expected");
        }

        const offsets = blockOffsets(datamodel, settings);
        const lines = [LAYOUT_DESCRIPTOR_TAG, 'endianness=' + endianness];

        (datamodel.children ?? []).forEach((block, index) => {
            lines.push('block=' + toHex(blockIdentity(block), 4) + ',' + offsets[index]);
        });
        return lines.join('\n');
    }

    /** 32-bit hash of the layout descriptor. It changes with any change that moves or reshapes a byte, and with the endianness. */
    function eepromLayoutIdentity(datamodel, settings) {
        return crc32(utf8.encode(layoutDescriptor(datamodel, settings)));
    }

    /** The 6 bytes of the identity header: the CRC-16 over the identity's 4 bytes, then the identity, both little-endian */
    function identityHeader(layoutIdentity) {
        const header = new Uint8Array(IDENTITY_HEADER_SIZE);
        const view = new DataView(header.buffer);

        view.setUint32(2, layoutIdentity, true);
        view.setUint16(0, crc16(header.subarray(2)), true);
        return header;
    }

    // ---- Formatting: 0xAD91, 0x0D7B6EEF, A0 51 EF 6E 7B 0D

    function formatHex(value, digits) {
        return '0x' + toHex(value, digits);
    }

    function formatBytes(bytes) {
        return Array.from(bytes, (b) => toHex(b, 2)).join(' ');
    }

    MeemCommon.dataDescriptor = dataDescriptor;
    MeemCommon.dataIdentity = dataIdentity;
    MeemCommon.blockDescriptor = blockDescriptor;
    MeemCommon.blockIdentity = blockIdentity;
    MeemCommon.layoutDescriptor = layoutDescriptor;
    MeemCommon.eepromLayoutIdentity = eepromLayoutIdentity;
    MeemCommon.identityHeader = identityHeader;
    MeemCommon.formatHex = formatHex;
    MeemCommon.formatBytes = formatBytes;

})(globalThis.MeemCommon = globalThis.MeemCommon || {});
