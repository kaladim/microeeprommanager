// Tests of the identities on a data model as the configurator keeps it, on the vectors shared with the Python and the common tests. Run with: node --test
const test = require('node:test');
const assert = require('node:assert');
const fs = require('node:fs');
const path = require('node:path');

// The common modules are classic scripts, contributing to the global MeemCommon namespace
for (const name of ['data-model', 'crc', 'identity']) {
    require(`../../common/js/${name}.js`);
}
const MeemCommon = globalThis.MeemCommon;
const { parseConfigJson, restoreNumericTypesAfterLoad } = require('../config-json.js');

const ROOT = path.join(__dirname, '..', '..', '..');
const VECTORS = JSON.parse(fs.readFileSync(path.join(ROOT, 'test', 'vectors', 'identity_vectors.json'), 'utf-8'));
const NEUTRAL_SETTINGS = 'test/footprint/production_like/platform_settings_xc8.json';

// Loads a configuration file the same way as the configurator
function load(relativePath) {
    const obj = parseConfigJson(fs.readFileSync(path.join(ROOT, relativePath), 'utf-8'));
    restoreNumericTypesAfterLoad(obj);
    return obj;
}

for (const modelVectors of VECTORS.models) {
    test(`the block offsets and identities of the shared vectors (${modelVectors.datamodel})`, () => {
        const datamodel = load(modelVectors.datamodel);

        assert.deepStrictEqual(MeemCommon.blockOffsets(datamodel, load(NEUTRAL_SETTINGS)), modelVectors.blocks.map((b) => b.offset));
        datamodel.children.forEach((block, i) => {
            const expected = modelVectors.blocks[i];
            assert.strictEqual(MeemCommon.dataDescriptor(block), expected.data_descriptor.join('\n'), block.name);
            assert.strictEqual(MeemCommon.formatHex(MeemCommon.dataIdentity(block), 4), expected.data_identity, block.name);
            assert.strictEqual(MeemCommon.blockDescriptor(block), expected.block_descriptor.join('\n'), block.name);
            assert.strictEqual(MeemCommon.formatHex(MeemCommon.blockIdentity(block), 4), expected.block_identity, block.name);
        });
    });

    for (const layout of modelVectors.layouts) {
        test(`the layout identity and header of the shared vectors (${modelVectors.datamodel}, ${layout.endianness}-endian)`, () => {
            const settings = { ...load(NEUTRAL_SETTINGS), endianness: layout.endianness };
            const datamodel = load(modelVectors.datamodel);
            const identity = MeemCommon.eepromLayoutIdentity(datamodel, settings);

            assert.strictEqual(MeemCommon.layoutDescriptor(datamodel, settings), layout.descriptor.join('\n'));
            assert.strictEqual(MeemCommon.formatHex(identity, 8), layout.eeprom_layout_identity);
            assert.strictEqual(MeemCommon.formatBytes(MeemCommon.identityHeader(identity)), layout.identity_header);
        });
    }
}
