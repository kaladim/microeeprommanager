// Tests of the configuration files' (de)serialization. Run with: node --test
const test = require('node:test');
const assert = require('node:assert');
const { parseConfigJson, restoreNumericTypesAfterLoad, stringifyConfig } = require('../config_json.js');

const DataTypes = { uint8: 0, uint64: 6, int64: 7, float32: 8, float64: 9 };

// Loads a configuration file the same way as the configurator
function load(text) {
    const obj = parseConfigJson(text);
    restoreNumericTypesAfterLoad(obj);
    return obj;
}

function makeParameter(data_type, default_value, description = '') {
    return { name: 'param', description, children: [], data_type, multiplicity: default_value.length, default_value };
}

test('64-bit integers survive a save/load round trip', () => {
    const block = {
        name: 'block', children: [
            makeParameter(DataTypes.uint64, [0x1000000000000001n, 0xFFFFFFFFFFFFFFFFn]),
            makeParameter(DataTypes.int64, [-0x8000000000000000n, 0x7FFFFFFFFFFFFFFFn]),
        ]
    };

    const loaded = load(stringifyConfig(block, 4));

    assert.deepStrictEqual(loaded.children[0].default_value, block.children[0].default_value);
    assert.deepStrictEqual(loaded.children[1].default_value, block.children[1].default_value);
});

test('numeric-looking text stays text after loading', () => {
    const datamodel = { name: 'datamodel', description: '2024', children: [{ name: 'block', description: '0x10', children: [makeParameter(DataTypes.uint8, [1], 'NaN')] }] };

    const loaded = load(stringifyConfig(datamodel, 4));

    assert.strictEqual(loaded.description, '2024');
    assert.strictEqual(loaded.children[0].description, '0x10');
    assert.strictEqual(loaded.children[0].children[0].description, 'NaN');
});

test('numbers saved as strings by older versions are still loaded as numbers', () => {
    const loaded = load('{"data_type": 6, "default_value": ["186", "0xFF"]}');

    assert.deepStrictEqual(loaded.default_value, [186n, 255n]);
});

test('integral defaults of floating-point parameters are saved with a fraction', () => {
    for (const data_type of [DataTypes.float32, DataTypes.float64]) {
        const json = stringifyConfig(makeParameter(data_type, [1, 2.5, -3, 4n]), 4);

        // The Python tools would read '1' as an integer, which doesn't match the parameter type
        assert.match(json.replace(/\s/g, ''), /"default_value":\[1\.0,2\.5,-3\.0,4\.0\]/);
        assert.deepStrictEqual(JSON.parse(json).default_value, [1, 2.5, -3, 4]);
    }
});

test('defaults of integer parameters are saved without a fraction', () => {
    const json = stringifyConfig(makeParameter(DataTypes.uint8, [1, 2n]), 4);

    assert.match(json.replace(/\s/g, ''), /"default_value":\[1,2\]/);
});
