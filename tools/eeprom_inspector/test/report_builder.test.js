// Tests of the report builder. Run with: node --test
const test = require('node:test');
const assert = require('node:assert');

// The core modules are classic scripts, contributing to the global EEPROM namespace
for (const name of ['data_model', 'wear_leveling', 'crc', 'report_builder']) {
    require(`../core/${name}.js`);
}
const EEPROM = globalThis.EEPROM;

const checksumParams = { algo: 'crc', polynomial: 0x11D, initial_value: 0, reverse_input: false, final_xor_value: 0 };

/** Builds the report of a single basic block with a uint16 and a uint32 parameter */
function buildReport(endianness) {
    const datamodel = {
        checksum_size: 1,
        children: [{
            name: 'block', management_type: EEPROM.ManagementType.Basic, instance_count: 1, children: [
                { name: 'u16', data_type: EEPROM.DataType.uint16, multiplicity: 1 },
                { name: 'u32', data_type: EEPROM.DataType.uint32, multiplicity: 1 },
            ]
        }]
    };

    const image = new Uint8Array(1 + 2 + 4);
    const view = new DataView(image.buffer);
    view.setUint16(1, 0x1234, endianness === 'little');
    view.setUint32(3, 0x89ABCDEF, endianness === 'little');
    image[0] = new EEPROM.ChecksumAlgorithm(checksumParams).calculate(image.subarray(1));

    const settings = EEPROM.parsePlatformSettings(JSON.stringify({ endianness }));
    return EEPROM.buildReport(datamodel, settings, image, checksumParams);
}

for (const endianness of ['little', 'big']) {
    test(`hex values show the most significant byte first (${endianness}-endian image)`, () => {
        const block = buildReport(endianness).blocks[0];
        const valueOf = (name) => block.params.find((p) => p.name === name).instances[0].data[0];

        assert.ok(block.instance_status[0].is_valid);
        assert.strictEqual(valueOf('u16').value_dec, String(0x1234));
        assert.strictEqual(valueOf('u16').value_hex, '0x1234');
        assert.strictEqual(valueOf('u32').value_dec, String(0x89ABCDEF));
        assert.strictEqual(valueOf('u32').value_hex, '0x89ABCDEF');
    });
}
