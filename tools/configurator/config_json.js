// JSON (de)serialization of the configuration files, shared by the configurator app and its tests.
// Classic (non-module) script, so it works from file://. Its functions become globals for app.js.

const FLOAT_DATA_TYPES = [8, 9]; // DataTypes.float32, DataTypes.float64

// Free-text fields, which must never be converted to numbers, even if they look like ones (e.g. a description "2024").
// Contains also the platform settings' subtrees, consisting of text only.
const TEXT_FIELDS = new Set([
    'name', 'description', 'algo', 'endianness', 'external_headers', 'page_aligned_blocks',
    'enter_critical_section_operation', 'exit_critical_section_operation', 'compiler_directives', 'code_gen_settings'
]);

// Number parsing helper: accepts numbers, decimal strings, hex (0x...), and numeric strings
function parseNumberTokenForModel(token, preferredTypeHint) {
    // preferredTypeHint may be 'float' or 'int' or a DataTypes value
    if (token === null || token === undefined) return token;
    if (typeof token === 'number' || typeof token === 'bigint') return token;
    if (typeof token !== 'string') return token;
    const s = token.trim(); if (s === '') return 0;
    // hex
    if (/^[-+]?0x[0-9a-fA-F]+$/.test(s)) {
        try { return BigInt(s); } catch (e) { return Number(s); }
    }
    // float
    if (s.includes('.') || preferredTypeHint === 'float') { const f = parseFloat(s); return isNaN(f) ? 0 : f }
    // integer decimal - try BigInt, fallback to Number
    try { return BigInt(s); } catch (e) { const n = Number(s); return isNaN(n) ? 0 : n }
}

// Parse a configuration file. Unlike JSON.parse(), keeps the full precision of integers beyond
// Number.MAX_SAFE_INTEGER (e.g. 64-bit default values) by loading them as BigInt. Before parsing, each such
// literal is replaced by a NUL-delimited sentinel string - the same technique as in stringifyConfig().
function parseConfigJson(text) {
    const bigints = [];
    const json = text.replace(/("(?:[^"\\]|\\.)*")|(-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?)/g, (m, str, num) => {
        if (str !== undefined) return str; // String literals are copied verbatim
        if (/^-?\d+$/.test(num) && !Number.isSafeInteger(Number(num))) {
            bigints.push(BigInt(num));
            return `"\\u0000BIGINT${bigints.length - 1}\\u0000"`;
        }
        return num;
    });
    return JSON.parse(json, (key, value) => {
        const match = (typeof value === 'string') ? /^\u0000BIGINT(\d+)\u0000$/.exec(value) : null;
        return match ? bigints[Number(match[1])] : value;
    });
}

// Restore numeric typed values after loading JSON: convert objects where values were serialized as strings
function restoreNumericTypesAfterLoad(obj) {
    if (!obj || typeof obj !== 'object') return;
    // Walk recursively and convert string-looking numbers into Number/BigInt when appropriate
    const stack = [obj];
    while (stack.length) {
        const cur = stack.pop();
        for (const k of Object.keys(cur)) {
            if (TEXT_FIELDS.has(k)) continue;
            const v = cur[k];
            if (v && typeof v === 'object') { stack.push(v); continue }
            if (typeof v === 'string') {
                // Handle NaN string conversion
                if (v.trim().toLowerCase() === 'nan') {
                    cur[k] = NaN;
                    continue;
                }
                // Heuristic: if string looks like integer or hex, convert to BigInt/Number
                if (/^[-+]?(0x[0-9a-fA-F]+|\d+)$/.test(v.trim())) {
                    cur[k] = parseNumberTokenForModel(v);
                }
            }
        }
    }
}

// Serialize an object to JSON while emitting BigInt values as bare number
// literals (e.g. 186) instead of quoted strings. Integer values are kept as
// BigInt internally to preserve full 64-bit precision, but JSON.stringify cannot
// output BigInt directly, so each one is wrapped in a NUL-delimited sentinel and
// the quotes the serializer adds around it are stripped afterwards. NUL chars can
// never appear unescaped in JSON output, so the sentinel cannot collide with real
// string data. NaN floats are still emitted as the string "NaN" (JSON has no NaN).
// Integral defaults of floating-point parameters are emitted with a fraction (1.0, not 1):
// the Python tools would read '1' as an integer, which doesn't match the parameter's type.
function stringifyConfig(obj, space) {
    const literals = [];
    const toLiteral = (text) => {
        literals.push(text);
        return `\u0000LITERAL${literals.length - 1}\u0000`;
    };
    const toFloatLiteral = (value) => {
        const text = String(value);
        return /^-?\d+$/.test(text) ? toLiteral(text + '.0') : value;
    };
    const json = JSON.stringify(obj, function (key, value) {
        if (typeof value === 'bigint') return toLiteral(value.toString());
        if (typeof value === 'number' && isNaN(value)) return "NaN";
        if (key === 'default_value' && Array.isArray(value) && FLOAT_DATA_TYPES.includes(Number(this.data_type))) {
            return value.map((v) => ((typeof v === 'bigint') || (typeof v === 'number' && Number.isInteger(v))) ? toFloatLiteral(v) : v);
        }
        return value;
    }, space);
    return json.replace(/"\\u0000LITERAL(\d+)\\u0000"/g, (m, i) => literals[Number(i)]);
}

if (typeof module !== 'undefined') {
    module.exports = { parseNumberTokenForModel, parseConfigJson, restoreNumericTypesAfterLoad, stringifyConfig };
}
