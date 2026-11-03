// Tests of the report builder. Run with: node --test
const test = require('node:test');
const assert = require('node:assert');
const fs = require('node:fs');
const path = require('node:path');

// The modules are classic scripts, contributing to the global namespaces MeemCommon and EEPROM
for (const name of ['data-model', 'crc', 'identity']) {
    require(`../../common/js/${name}.js`);
}
for (const name of ['config-loader', 'wear-leveling', 'image-loader', 'intel-hex', 'report-builder']) {
    require(`../core/${name}.js`);
}
const MeemCommon = globalThis.MeemCommon;
const EEPROM = globalThis.EEPROM;

const ROOT = path.join(__dirname, '..', '..', '..');
const IMAGE_VECTORS = JSON.parse(fs.readFileSync(path.join(ROOT, 'test', 'vectors', 'image_vectors.json'), 'utf-8')).images;
const IDENTITY_VECTORS = JSON.parse(fs.readFileSync(path.join(ROOT, 'test', 'vectors', 'identity_vectors.json'), 'utf-8'));

function readText(relativePath) {
    return fs.readFileSync(path.join(ROOT, relativePath), 'utf-8');
}

function formatAddress(offset) {
    return '0x' + offset.toString(16).toUpperCase().padStart(4, '0');
}

/** The inputs of an image of the image generator (test/vectors/image_vectors.json), parsed as the UI parses them */
function loadImageVector(vector) {
    const settings = EEPROM.parsePlatformSettings(readText(vector.settings));
    if (vector.endianness) settings.endianness = vector.endianness;

    const file = fs.readFileSync(path.join(ROOT, vector.image));
    const image = EEPROM.loadEepromImage(path.basename(vector.image), file.buffer.slice(file.byteOffset, file.byteOffset + file.byteLength));
    return { datamodel: EEPROM.parseDataModel(readText(vector.datamodel)), settings, image };
}

function buildReport(inputs, image) {
    return EEPROM.buildReport(inputs.datamodel, inputs.settings, image || inputs.image);
}

function findImageVector(image) {
    return IMAGE_VECTORS.find((vector) => vector.image.endsWith(image));
}

/** The image generator writes every instance but those of a wear-leveling block after the first, which stay erased */
function isWrittenByImageGenerator(block, instance) {
    return !block.is_wear_leveling || instance === 0;
}

// ---- Images of the image generator -------------------------------------

for (const vector of IMAGE_VECTORS) {
    test(`the image's header matches its data model (${vector.image})`, () => {
        const inputs = loadImageVector(vector);
        const header = buildReport(inputs).header.identity_header;

        assert.strictEqual(header.status, EEPROM.HeaderStatus.match);
        assert.strictEqual(header.stored, header.expected);
        assert.strictEqual(header.stored_layout_identity, header.layout_identity);
        assert.strictEqual(header.stored_crc, header.calculated_crc);
    });

    test(`every written instance passes its check, the others fail (${vector.image})`, () => {
        const report = buildReport(loadImageVector(vector));

        for (const block of report.blocks) {
            block.instance_status.forEach((status, instance) => {
                assert.strictEqual(status.is_valid, isWrittenByImageGenerator(block, instance), `${block.name}[${instance}]`);
                assert.strictEqual(status.is_most_recent, block.is_wear_leveling && instance === 0, `${block.name}[${instance}]`);
            });
        }
    });

    test(`the parameters hold their defaults (${vector.image})`, () => {
        const inputs = loadImageVector(vector);
        const model = JSON.parse(readText(vector.datamodel));
        const report = buildReport(inputs);

        report.blocks.forEach((block, b) => {
            const params = block.params.filter((p) => p.kind === 'param');
            assert.deepStrictEqual(params.map((p) => p.name), model.children[b].children.map((p) => p.name));

            params.forEach((param, p) => {
                const defaults = model.children[b].children[p].default_value.map(String);
                param.instances.forEach((instance, i) => {
                    if (!isWrittenByImageGenerator(block, i)) return;
                    assert.deepStrictEqual(instance.data.map((n) => n.value_dec), defaults, `${block.name}[${i}].${param.name}`);
                });
            });
        });
    });
}

test('the blocks are decoded at the offsets and with the identities of the shared vectors', () => {
    const modelVectors = IDENTITY_VECTORS.models[0];

    for (const layout of modelVectors.layouts) {
        const vector = IMAGE_VECTORS.find((v) => v.datamodel === modelVectors.datamodel && v.endianness === layout.endianness);
        const report = buildReport(loadImageVector(vector));

        assert.strictEqual(report.header.identity_header.stored, layout.identity_header, layout.endianness);
        assert.strictEqual(report.header.identity_header.layout_identity, layout.eeprom_layout_identity, layout.endianness);

        assert.strictEqual(report.blocks.length, modelVectors.blocks.length);
        report.blocks.forEach((block, b) => {
            const expected = modelVectors.blocks[b];
            assert.strictEqual(block.name, expected.name);
            assert.strictEqual(block.address, formatAddress(expected.offset), block.name);
            assert.strictEqual(block.params[0].instances[0].data[0].address, formatAddress(expected.offset), block.name);
            assert.strictEqual(block.block_identity, expected.block_identity, block.name);
            assert.strictEqual(block.data_identity, expected.data_identity, block.name);
        });
    }
});

test('a page-aligned first block starts at the first page boundary after the header', () => {
    const inputs = loadImageVector(findImageVector('meem_config_page_aligned.hex'));
    const pageSize = inputs.settings.eeprom_page_size;
    const report = buildReport(inputs);

    assert.ok(pageSize > MeemCommon.IDENTITY_HEADER_SIZE); // The check below is meaningful only if the header doesn't fill the page
    assert.strictEqual(report.blocks[0].address, formatAddress(pageSize));
    for (const block of report.blocks) {
        assert.strictEqual(parseInt(block.address, 16) % pageSize, 0, block.name);
    }
});

// ---- The identity header -----------------------------------------------

test("an intact header of another EEPROM layout is told apart from the model's", () => {
    // The image of the neutral model, against the model of the unit tests
    const neutral = loadImageVector(findImageVector('production_like_little.hex'));
    const other = loadImageVector(findImageVector('meem_config_page_aligned.hex'));
    const header = buildReport(other, neutral.image).header.identity_header;

    assert.strictEqual(header.status, EEPROM.HeaderStatus.other);
    assert.strictEqual(header.stored, IDENTITY_VECTORS.models[0].layouts.find((l) => l.endianness === 'little').identity_header);
    assert.strictEqual(header.stored_crc, header.calculated_crc);
    assert.notStrictEqual(header.stored, header.expected);
});

test('the layout of another endianness is another EEPROM layout', () => {
    const big = loadImageVector(findImageVector('production_like_big.hex'));
    const little = loadImageVector(findImageVector('production_like_little.hex'));

    assert.strictEqual(buildReport(little, big.image).header.identity_header.status, EEPROM.HeaderStatus.other);
});

test('a header that fails its CRC-16 is garbage', () => {
    const inputs = loadImageVector(findImageVector('production_like_little.hex'));

    for (let offset = 0; offset < MeemCommon.IDENTITY_HEADER_SIZE; offset++) {
        const image = inputs.image.slice();
        image[offset] ^= 0x01;

        const header = buildReport(inputs, image).header.identity_header;
        assert.strictEqual(header.status, EEPROM.HeaderStatus.garbage, `byte ${offset}`);
        assert.notStrictEqual(header.stored_crc, header.calculated_crc, `byte ${offset}`);
    }
});

test('a blank or zeroed EEPROM has a garbage header and no valid instance', () => {
    const inputs = loadImageVector(findImageVector('meem_config_page_aligned.hex'));

    for (const fill of [0xFF, 0x00]) {
        const report = buildReport(inputs, new Uint8Array(inputs.image.length).fill(fill));

        assert.strictEqual(report.header.identity_header.status, EEPROM.HeaderStatus.garbage, `fill ${fill}`);
        for (const block of report.blocks) {
            assert.ok(block.instance_status.every((s) => !s.is_valid), `${block.name}, fill ${fill}`);
        }
    }
});

// ---- Instance checks ----------------------------------------------------

test("the instance checksum is the CRC-16 of the data in the target's byte order", () => {
    // The image of a big-endian target, read as if the target were little-endian, and the reverse
    const big = loadImageVector(findImageVector('production_like_big.hex'));
    const little = loadImageVector(findImageVector('production_like_little.hex'));

    for (const [inputs, image] of [[little, big.image], [big, little.image]]) {
        for (const block of buildReport(inputs, image).blocks) {
            const stored = block.params[0].instances[0].data[0].value_hex;
            const isPalindrome = stored.slice(2, 4) === stored.slice(4, 6);
            assert.ok(isPalindrome || !block.instance_status[0].is_valid, `${block.name}, stored ${stored}`);
        }
    }
});

test('a changed data byte fails the check of its instance only', () => {
    const inputs = loadImageVector(findImageVector('meem_config_page_aligned.hex'));
    const block = buildReport(inputs).blocks.find((b) => b.name === 'Block_BackupCopy_0');
    const secondInstanceData = parseInt(block.params[1].instances[1].data[0].address, 16);

    const image = inputs.image.slice();
    image[secondInstanceData] ^= 0x80;

    const status = buildReport(inputs, image).blocks.find((b) => b.name === 'Block_BackupCopy_0').instance_status;
    assert.deepStrictEqual(status.map((s) => s.is_valid), [true, false]);
});

// ---- Inputs ---------------------------------------------------------------

test("a data model's checksum_size is ignored", () => {
    const vector = findImageVector('meem_config_page_aligned.hex');
    const withChecksumSize = Object.assign(JSON.parse(readText(vector.datamodel)), { checksum_size: 1 });

    const expected = buildReport(loadImageVector(vector));
    const inputs = Object.assign(loadImageVector(vector), { datamodel: EEPROM.parseDataModel(JSON.stringify(withChecksumSize)) });
    const report = buildReport(inputs);

    assert.strictEqual(report.header.identity_header.status, EEPROM.HeaderStatus.match);
    assert.deepStrictEqual(report.blocks, expected.blocks);
});

test('an image smaller than the data model is rejected', () => {
    const inputs = loadImageVector(findImageVector('meem_config_page_aligned.hex'));
    const lastBlock = buildReport(inputs).blocks.at(-1);
    const usedSize = parseInt(lastBlock.address, 16) + lastBlock.total_size;

    assert.doesNotThrow(() => buildReport(inputs, inputs.image.subarray(0, usedSize)));
    assert.throws(() => buildReport(inputs, inputs.image.subarray(0, usedSize - 1)), /insufficient/);
});

// ---- Values ---------------------------------------------------------------

/** Builds the report of a single basic block with a uint16 and a uint32 parameter */
function buildBasicBlockReport(endianness) {
    const datamodel = {
        children: [{
            name: 'block', management_type: MeemCommon.ManagementTypes.Basic, instance_count: 1, children: [
                { name: 'u16', data_type: MeemCommon.DataTypes.uint16, multiplicity: 1 },
                { name: 'u32', data_type: MeemCommon.DataTypes.uint32, multiplicity: 1 },
            ]
        }]
    };

    const offset = MeemCommon.IDENTITY_HEADER_SIZE;
    const image = new Uint8Array(offset + MeemCommon.CHECKSUM_SIZE + 2 + 4);
    const view = new DataView(image.buffer);
    view.setUint16(offset + 2, 0x1234, endianness === 'little');
    view.setUint32(offset + 4, 0x89ABCDEF, endianness === 'little');
    view.setUint16(offset, MeemCommon.crc16(image.subarray(offset + MeemCommon.CHECKSUM_SIZE)), endianness === 'little');

    const settings = EEPROM.parsePlatformSettings(JSON.stringify({ endianness }));
    return EEPROM.buildReport(datamodel, settings, image);
}

for (const endianness of ['little', 'big']) {
    test(`hex values show the most significant byte first (${endianness}-endian image)`, () => {
        const block = buildBasicBlockReport(endianness).blocks[0];
        const valueOf = (name) => block.params.find((p) => p.name === name).instances[0].data[0];

        assert.ok(block.instance_status[0].is_valid);
        assert.strictEqual(valueOf('u16').value_dec, String(0x1234));
        assert.strictEqual(valueOf('u16').value_hex, '0x1234');
        assert.strictEqual(valueOf('u32').value_dec, String(0x89ABCDEF));
        assert.strictEqual(valueOf('u32').value_hex, '0x89ABCDEF');
    });
}
