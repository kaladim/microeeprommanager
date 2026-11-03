// Tests of the identities, on the vectors shared with the Python tests. Run with: node --test
const test = require('node:test');
const assert = require('node:assert');
const fs = require('node:fs');
const path = require('node:path');

// The common modules are classic scripts, contributing to the global MeemCommon namespace
for (const name of ['data-model', 'crc', 'identity']) {
    require(`../${name}.js`);
}
const MeemCommon = globalThis.MeemCommon;

const ROOT = path.join(__dirname, '..', '..', '..', '..');
const VECTORS_FILE = path.join(ROOT, 'test', 'vectors', 'identity_vectors.json');
const NEUTRAL_SETTINGS = 'test/footprint/production_like/platform_settings_xc8.json';

function loadJson(relativePath) {
    return JSON.parse(fs.readFileSync(path.join(ROOT, relativePath), 'utf-8'));
}

function loadVectors() {
    return JSON.parse(fs.readFileSync(VECTORS_FILE, 'utf-8'));
}

function makeBlock(overrides = {}) {
    return {
        name: 'block', management_type: MeemCommon.ManagementTypes.Basic, instance_count: 1,
        children: [{ name: 'p1', data_type: MeemCommon.DataTypes.uint16, multiplicity: 1, children: [] }],
        ...overrides,
    };
}

// ---- The values of the shared vector file, which the Python tests read too

test('block offsets, descriptors and identities match the shared vectors', () => {
    for (const modelVectors of loadVectors().models) {
        const datamodel = loadJson(modelVectors.datamodel);

        assert.deepStrictEqual(MeemCommon.blockOffsets(datamodel, loadJson(NEUTRAL_SETTINGS)), modelVectors.blocks.map((b) => b.offset), modelVectors.datamodel);
        datamodel.children.forEach((block, index) => {
            const expected = modelVectors.blocks[index];
            const where = modelVectors.datamodel + ', ' + block.name;

            assert.strictEqual(MeemCommon.dataDescriptor(block), expected.data_descriptor.join('\n'), where);
            assert.strictEqual(MeemCommon.dataIdentity(block), parseInt(expected.data_identity, 16), where);
            assert.strictEqual(MeemCommon.blockDescriptor(block), expected.block_descriptor.join('\n'), where);
            assert.strictEqual(MeemCommon.blockIdentity(block), parseInt(expected.block_identity, 16), where);
        });
    }
});

test('layout descriptors, identities and headers match the shared vectors', () => {
    for (const modelVectors of loadVectors().models) {
        const datamodel = loadJson(modelVectors.datamodel);

        for (const expected of modelVectors.layouts) {
            const where = modelVectors.datamodel + ', ' + expected.endianness;
            const settings = { ...loadJson(NEUTRAL_SETTINGS), endianness: expected.endianness };
            const layoutIdentity = MeemCommon.eepromLayoutIdentity(datamodel, settings);

            assert.strictEqual(MeemCommon.layoutDescriptor(datamodel, settings), expected.descriptor.join('\n'), where);
            assert.strictEqual(layoutIdentity, parseInt(expected.eeprom_layout_identity, 16), where);
            assert.strictEqual(MeemCommon.formatBytes(MeemCommon.identityHeader(layoutIdentity)), expected.identity_header, where);
        }
    }
});

test('the neutral model gives its golden values', () => {
    const modelVectors = loadVectors().models.find((m) => m.datamodel === 'test/footprint/production_like/eeprom_datamodel.json');
    const datamodel = loadJson(modelVectors.datamodel);
    const settings = { ...loadJson(NEUTRAL_SETTINGS), endianness: 'little' };

    assert.strictEqual(MeemCommon.eepromLayoutIdentity(datamodel, settings), 0x6C987786);
    assert.deepStrictEqual(MeemCommon.identityHeader(0x6C987786), new Uint8Array([0x91, 0x1F, 0x86, 0x77, 0x98, 0x6C]));
    assert.strictEqual(modelVectors.blocks[0].offset, MeemCommon.IDENTITY_HEADER_SIZE);
});

test('identity headers match the shared vectors', () => {
    for (const expected of loadVectors().headers) {
        const header = MeemCommon.identityHeader(parseInt(expected.eeprom_layout_identity, 16));

        assert.strictEqual(MeemCommon.formatBytes(header), expected.identity_header, expected.eeprom_layout_identity);
    }
});

test('page-aligned blocks are laid out as common/py/utils.py lays them out', () => {
    // Expected values from common/py/utils.py and common/py/identity.py
    const datamodel = loadJson('test/meem_config/eeprom_datamodel.json');
    const settings = loadJson('test/meem_config/platform_settings_gcc.json');

    assert.deepStrictEqual(MeemCommon.blockOffsets(datamodel, settings), [32, 224, 256, 288, 352, 384, 416, 448]);
    assert.strictEqual(MeemCommon.formatHex(MeemCommon.eepromLayoutIdentity(datamodel, settings), 8), '0x2B924356');

    settings.page_aligned_blocks = ['Block_Basic_0', 'Block_WearLeveling_1'];
    assert.deepStrictEqual(MeemCommon.blockOffsets(datamodel, settings), [6, 192, 205, 223, 288, 318, 336, 363]);
    assert.strictEqual(MeemCommon.formatHex(MeemCommon.eepromLayoutIdentity(datamodel, settings), 8), '0x37EEE1BE');
});

// ---- Descriptors

test('the data descriptor lists the bitfields directly after their parameter', () => {
    const block = makeBlock({
        children: [
            { name: 'flags', data_type: MeemCommon.DataTypes.uint8, multiplicity: 1, children: [{ name: 'a', size_in_bits: 1 }, { name: 'b', size_in_bits: 3 }] },
            { name: 'values', data_type: MeemCommon.DataTypes.float32, multiplicity: 4, children: [] },
        ]
    });

    assert.strictEqual(MeemCommon.dataDescriptor(block), 'mEEM-data-v2\nparam=flags,uint8,1\nbitfield=a,1\nbitfield=b,3\nparam=values,float32,4');
});

test('the block descriptor', () => {
    const block = makeBlock({ name: 'Settings', management_type: MeemCommon.ManagementTypes.WearLeveling, instance_count: 4 });
    const data = MeemCommon.dataIdentity(block).toString(16).toUpperCase().padStart(4, '0');

    assert.strictEqual(MeemCommon.blockDescriptor(block), 'mEEM-block-v2\nblock=Settings\nmgmt=WearLeveling\ncount=4\ndata=' + data);
});

test('names are stripped as Python strips them, and hashed as UTF-8', () => {
    // Expected values from common/py/identity.py. U+001C and U+0085 are whitespace to Python but not to trim(), U+FEFF the other way round.
    const block = makeBlock({
        name: '\x1C Größe\x85', management_type: MeemCommon.ManagementTypes.BackupCopy, instance_count: 2,
        children: [{ name: '﻿µ', data_type: MeemCommon.DataTypes.int16, multiplicity: 3, children: [{ name: '　ä ', size_in_bits: 4 }] }],
    });

    assert.strictEqual(MeemCommon.dataDescriptor(block), 'mEEM-data-v2\nparam=﻿µ,int16,3\nbitfield=ä,4');
    assert.strictEqual(MeemCommon.dataIdentity(block), 0x5C2F);
    assert.strictEqual(MeemCommon.blockDescriptor(block), 'mEEM-block-v2\nblock=Größe\nmgmt=BackupCopy\ncount=2\ndata=5C2F');
    assert.strictEqual(MeemCommon.blockIdentity(block), 0x125B);
});

test('missing attributes take the defaults of the Python loader', () => {
    const sparse = { name: 'block', management_type: MeemCommon.ManagementTypes.Basic, children: [{ name: 'p1', data_type: MeemCommon.DataTypes.uint16 }] };

    assert.strictEqual(MeemCommon.blockDescriptor(sparse), MeemCommon.blockDescriptor(makeBlock()));
    assert.deepStrictEqual(MeemCommon.blockOffsets({ children: [sparse, sparse] }, {}), [6, 10]);
});

test('integers can be BigInts or text, as the configurator keeps them', () => {
    const typed = makeBlock({ instance_count: 2n, management_type: '1', children: [{ name: 'p1', data_type: 2n, multiplicity: '3', children: [{ name: 'b', size_in_bits: 4n }] }] });
    const plain = makeBlock({ instance_count: 2, management_type: 1, children: [{ name: 'p1', data_type: 2, multiplicity: 3, children: [{ name: 'b', size_in_bits: 4 }] }] });

    assert.strictEqual(MeemCommon.blockDescriptor(typed), MeemCommon.blockDescriptor(plain));
    assert.strictEqual(MeemCommon.dataDescriptor(typed), MeemCommon.dataDescriptor(plain));
    assert.strictEqual(
        MeemCommon.eepromLayoutIdentity({ children: [typed] }, { eeprom_page_size: 8n }),
        MeemCommon.eepromLayoutIdentity({ children: [plain] }, { eeprom_page_size: 8 }));
});

test('an unknown data or management type is an error', () => {
    assert.throws(() => MeemCommon.dataDescriptor(makeBlock({ children: [{ name: 'p1', data_type: 10, multiplicity: 1 }] })), /data type 10/);
    assert.throws(() => MeemCommon.dataDescriptor(makeBlock({ children: [{ name: 'p1', data_type: null, multiplicity: 1 }] })), /data type null/);
    assert.throws(() => MeemCommon.blockDescriptor(makeBlock({ management_type: 4 })), /management type 4/);
});

test('the layout descriptor needs a known endianness', () => {
    assert.throws(() => MeemCommon.layoutDescriptor({ children: [makeBlock()] }, { endianness: 'middle' }), /endianness/);
});

// ---- What the identities cover

test('the block identity ignores the offset', () => {
    assert.strictEqual(MeemCommon.blockIdentity(makeBlock()), MeemCommon.blockIdentity(makeBlock({ offset_in_eeprom: 100 })));
});

test('a wear-leveling block holds its sequence counter, the layout identity covers offsets and endianness', () => {
    const wearLeveling = makeBlock({ name: 'wl', management_type: MeemCommon.ManagementTypes.WearLeveling, instance_count: 3 });
    const datamodel = { children: [wearLeveling, makeBlock()] };

    assert.deepStrictEqual(MeemCommon.blockOffsets(datamodel, {}), [6, 6 + 3 * (2 + 1 + 2)]);

    const little = MeemCommon.eepromLayoutIdentity(datamodel, { endianness: 'little' });
    assert.notStrictEqual(MeemCommon.eepromLayoutIdentity(datamodel, { endianness: 'big' }), little);
    assert.notStrictEqual(MeemCommon.eepromLayoutIdentity(datamodel, { endianness: 'little', eeprom_page_size: 8 }), little);
});

test('computing the identities attaches nothing to the data model', () => {
    const datamodel = { children: [makeBlock()] };
    const before = JSON.stringify(datamodel);

    MeemCommon.eepromLayoutIdentity(datamodel, {});
    assert.strictEqual(JSON.stringify(datamodel), before);
});
