// Basic in-memory representation matching Python classes for validation and JSON mapping
const DataTypes = {
    uint8: 0, int8: 1, uint16: 2, int16: 3, uint32: 4, int32: 5, uint64: 6, int64: 7, float32: 8, float64: 9
};
const ManagementTypes = { Basic: 0, BackupCopy: 1, MultiProfile: 2, WearLeveling: 3 };
const ManagementTypeLabels = {
    [ManagementTypes.Basic]: 'Basic',
    [ManagementTypes.BackupCopy]: 'Backup copy',
    [ManagementTypes.MultiProfile]: 'Multi-profile',
    [ManagementTypes.WearLeveling]: 'Wear-leveling'
};
const DataRecoveryStrategyLabels = { 0: 'Recover defaults & repair', 1: 'Recover defaults' };
const DataTypeSizes = { 0: 1, 1: 1, 2: 2, 3: 2, 4: 4, 5: 4, 6: 8, 7: 8, 8: 4, 9: 8 };

const FieldDocs = {
    datamodel: {
        name: "Has to be a valid C-language identifier.",
        description: "Optional description.",
        checksum_size: "Checksum size in bytes. Applies to all blocks."
    },
    block: {
        name: "Has to be a valid C-language identifier.",
        description: "Optional description. If defined, will appear in the generated sources.",
        management_type: "Defines block's strategy for EEPROM area management.",
        instance_count: "Number of data instances in the EEPROM. Depends on the selected management type.",
        data_recovery_strategy: "Defines the behavior if the data integrity check fails on init. Choice between: load defaults and repair the EEPROM area (recommended) or just load defaults.",
        compress_defaults: "Tries to deduce the shortest possible pattern for default values. In many cases, you may end up using just a single byte for all your defaults."
    },
    parameter: {
        name: "Has to be a valid C-language identifier.",
        description: "Optional description. If defined, will appear in the generated sources.",
        data_type: "Can be any standard integer or floating-point type. If the block contains BitFields, use unsigned integer.",
        multiplicity: "Values > 1 define the parameter as an array.",
        default_value: "Number of elements must be equal to the 'multiplicity'."
    },
    bitfield: {
        name: "Has to be a valid C-language identifier.",
        description: "Optional description. If defined, will appear in the generated sources.",
        size_in_bits: "Size of the bitfield in [bits]."
    },
    platform: {
        endianness: "Endianness of the target CPU",
        eeprom_size: "Allocated EEPROM to the mEEM, in bytes. In some cases, it might not be the whole available EEPROM.",
        eeprom_page_size: "Size of the EEPROM's page, in bytes. Set to 0 for systems that can write only one byte at a time, like most on-chip EEPROMs or if 'Flash EEPROM emulation' driver is used.",
        page_aligned_blocks: "List of block names which you want aligned to EEPROM page boundaries. The names must be present in the datamodel. If not specified, defaults to ['*'] (align all blocks). Makes sense only if eeprom_page_size > 0.",
        external_headers: "External header files, containing forward declarations for 'enter_critical_section_operation' and 'exit_critical_section_operation'.",
        enter_critical_section_operation: "Function/macro for designating the start of an atomic code fragment in the mEEM.",
        exit_critical_section_operation: "Function/macro for designating the end of an atomic code fragment in the mEEM.",
        opening_pack_directive: "All caches and defaults are defined as packed structures with byte alignment. If provided, this directive will be placed at the start of block type definitions. Some compilers may not require this at all, like those for 8-bit CPUs. You can use either a pack directive or an attribute, but never both at the same time.",
        closing_pack_directive: "Counterpart of the opening pack directive, where applicable.",
        pack_attribute: "Has the same effect as 'opening_pack_directive', but will be inserted inline with the definitions of block data types.",
        directive_for_defaults: "Compiler-specific placement directive for the 'defaults' object. Will be placed just before the 'defaults' object definition. You either use a placement directive or an attribute, but not both at the same time.",
        attribute_for_defaults: "Compiler-specific placement attribute for the 'defaults' object. Will be added to the 'defaults' object definition. You either use a placement directive or an attribute, but not both at the same time.",
        directive_for_cache: "Compiler-specific placement directive for the cache object. Will be placed just before the 'cache' object definition. You either use a placement directive or an attribute, but not both at the same time.",
        attribute_for_cache: "Compiler-specific placement attribute for the 'cache' object. Will be added to the 'cache' object definition. You either use a placement directive or an attribute, but not both at the same time."
    }
};

// App state
let state = {
    dataModel: null, // DataModel object
    platformSettings: null, // PlatformSettings object
    checksumSettings: null, // Checksum settings object
    checksumTypeHints: {}, // Type hints for checksum parameters (to distinguish float from int)
    savedNames: { dataModel: null, platform: null, checksum: null },
    isSavedAs: false,
    lastStatus: '',
    // paths (JSON.stringify(path)) for which child containers are collapsed
    collapsedPaths: new Set(),
};

function nowTS() { const d = new Date(); return d.toTimeString().slice(0, 8) }
function setStatus(msg) { state.lastStatus = msg; document.getElementById('status').textContent = `${nowTS()} ${msg}` }

// Utility: deep clone
// Helper: Create context menu button
function createContextButton(text, onClick) {
    const btn = document.createElement('button');
    btn.textContent = text;
    btn.addEventListener('click', onClick);
    return btn;
}

// Helper: Create context menu separator
function createContextSeparator() {
    return document.createElement('hr');
}

// Helper: Create expand/collapse button pair
function createExpandCollapseButtons(path, type) {
    const sep = createContextSeparator();
    const expandBtn = createContextButton('➕ Expand All', () => expandNodeRecursively(path, type));
    const collapseBtn = createContextButton('➖ Collapse All', () => collapseNodeRecursively(path, type));
    return { sep, expandBtn, collapseBtn };
}

function deepClone(obj) { return JSON.parse(JSON.stringify(obj)) }

// Utility: DOM element creation helpers
function el(tag, props = {}) {
    const element = document.createElement(tag);
    if (props.className) element.className = props.className;
    if (props.textContent) element.textContent = props.textContent;
    if (props.title) element.title = props.title;
    if (props.style) Object.assign(element.style, props.style);
    if (props.dataset) Object.keys(props.dataset).forEach(key => element.dataset[key] = props.dataset[key]);
    if (props.type) element.type = props.type;
    if (props.value !== undefined) element.value = props.value;
    if (props.checked !== undefined) element.checked = props.checked;
    if (props.placeholder) element.placeholder = props.placeholder;
    if (props.readOnly) element.readOnly = props.readOnly;
    if (props.tabIndex !== undefined) element.tabIndex = props.tabIndex;
    return element;
}

function createButton(text, onClick, style = {}) {
    const btn = el('button', { textContent: text, style });
    if (onClick) btn.addEventListener('click', onClick);
    return btn;
}

function createInput(type, value, onChange, props = {}) {
    const inp = el('input', { type, value, ...props });
    if (onChange) inp.addEventListener('change', onChange);
    return inp;
}

function createLabel(text, title = '') {
    return el('label', { textContent: text, title });
}

function createProp(labelText, inputElement, titleText = '') {
    const prop = el('div', { className: 'prop' });
    const label = createLabel(labelText, titleText);
    prop.appendChild(label);
    prop.appendChild(inputElement);
    return prop;
}

// Utility: Create modal dialog
function createModal(title, onClose) {
    const overlay = el('div', {
        style: {
            position: 'fixed', top: 0, left: 0, right: 0, bottom: 0,
            background: 'rgba(0,0,0,0.7)', zIndex: 1000,
            display: 'flex', alignItems: 'center', justifyContent: 'center'
        }
    });

    const dialog = el('div', {
        style: {
            background: '#2d2d30', padding: '24px', borderRadius: '8px',
            boxShadow: '0 8px 32px rgba(0,0,0,0.8)', minWidth: '400px',
            border: '1px solid rgba(255,255,255,0.1)'
        }
    });

    const titleEl = el('h3', {
        textContent: title,
        style: { margin: '0 0 20px 0', color: '#5ca8d8' }
    });
    dialog.appendChild(titleEl);

    overlay.appendChild(dialog);
    overlay.addEventListener('click', (e) => {
        if (e.target === overlay) {
            document.body.removeChild(overlay);
            if (onClose) onClose();
        }
    });

    return { overlay, dialog };
}

function createDialogInput(labelText, placeholder = '', type = 'text') {
    const label = el('label', {
        textContent: labelText,
        style: { display: 'block', color: '#888', marginBottom: '6px' }
    });

    const input = el('input', {
        type,
        placeholder,
        style: {
            width: '100%', padding: '8px', borderRadius: '4px',
            border: '1px solid rgba(255,255,255,0.1)',
            background: 'rgba(255,255,255,0.05)', color: '#ddd',
            marginBottom: '16px', boxSizing: 'border-box'
        }
    });

    return { label, input };
}

function createDialogSelect(labelText, options) {
    const label = el('label', {
        textContent: labelText,
        style: { display: 'block', color: '#888', marginBottom: '6px' }
    });

    const select = el('select', {
        style: {
            width: '100%', padding: '8px', borderRadius: '4px',
            border: '1px solid rgba(255,255,255,0.1)',
            background: 'rgba(255,255,255,0.05)', color: '#ddd',
            marginBottom: '20px', boxSizing: 'border-box'
        }
    });

    options.forEach(optText => {
        const option = el('option', {
            value: optText,
            textContent: optText,
            style: { background: '#2d2d30', color: '#ddd' }
        });
        select.appendChild(option);
    });

    return { label, select };
}

function createDialogButtons(onCancel, onConfirm, confirmText = 'OK') {
    const container = el('div', {
        style: { display: 'flex', gap: '8px', justifyContent: 'flex-end' }
    });

    const cancelBtn = createButton('Cancel', onCancel, {
        padding: '8px 16px', borderRadius: '4px',
        border: '1px solid rgba(255,255,255,0.1)',
        background: 'rgba(255,255,255,0.05)', color: '#ddd', cursor: 'pointer'
    });

    const confirmBtn = createButton(confirmText, onConfirm, {
        padding: '8px 16px', borderRadius: '4px',
        border: '1px solid #007acc', background: 'rgba(0,122,204,0.2)',
        color: '#fff', cursor: 'pointer', fontWeight: '500'
    });

    container.appendChild(cancelBtn);
    container.appendChild(confirmBtn);
    return container;
}

// Utility: Create a custom-select dropdown to avoid repetition
function createCustomSelect(options, currentValue, onChange) {
    const wrap = el('div', { className: 'custom-select' });
    const cur = el('div', { className: 'current', textContent: options[currentValue] || String(currentValue) });
    wrap.appendChild(cur);
    const menu = el('div', { className: 'menu', style: { display: 'none' } });

    Object.keys(options).forEach(key => {
        const val = options[key];
        const it = el('div', {
            className: 'item',
            textContent: val,
            dataset: { val: key },
            style: key == currentValue ? { fontWeight: '600' } : {}
        });
        it.addEventListener('click', () => {
            const newVal = it.dataset.val;
            onChange(newVal, val);
            cur.textContent = val;
            menu.querySelectorAll('.item').forEach(i => i.style.fontWeight = '');
            it.style.fontWeight = '600';
            menu.style.display = 'none';
        });
        menu.appendChild(it);
    });

    wrap.appendChild(menu);
    cur.addEventListener('click', (e) => {
        e.stopPropagation();
        menu.style.display = (menu.style.display === 'none') ? 'block' : 'none';
    });
    return wrap;
}

// Number parsing helper (used throughout): accepts numbers, decimal strings, hex (0x...), and numeric strings
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

// Normalize a node name: replace invalid characters with underscore and ensure leading char is a letter or underscore
function normalizeName(s) {
    if (s === null || s === undefined) return '';
    let t = String(s);
    // Replace any non-alphanumeric/underscore with underscore
    t = t.replace(/[^a-zA-Z0-9_]/g, '_');
    // Ensure first char is letter or underscore
    if (!/^[a-zA-Z_]/.test(t)) t = '_' + t.replace(/^_+/, '');
    return t;
}

// Format label text by replacing underscores with spaces
function formatLabel(s) {
    if (s === null || s === undefined) return '';
    return String(s).replace(/_/g, ' ');
}

// Check if a block name already exists in the data model
function blockNameExists(name, excludePath) {
    if (!state.dataModel || !name) return false;
    const blocks = state.dataModel.children || [];
    for (let i = 0; i < blocks.length; i++) {
        // Skip the block at excludePath (when editing existing block)
        if (excludePath && excludePath.length === 1 && excludePath[0] === i) continue;
        if (blocks[i].name === name) return true;
    }
    return false;
}

// Restore numeric typed values after loading JSON: convert objects where values were serialized as strings
function restoreNumericTypesAfterLoad(obj) {
    if (!obj || typeof obj !== 'object') return;
    // Walk recursively and convert string-looking numbers into Number/BigInt when appropriate
    const stack = [obj];
    while (stack.length) {
        const cur = stack.pop();
        for (const k of Object.keys(cur)) {
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

// Default factories
function makeEmptyDataModel() { return { name: '', description: '', checksum_size: 1, children: [] } }
function makeEmptyBlock() { return { name: '', description: '', children: [], management_type: ManagementTypes.Basic, instance_count: 1, data_recovery_strategy: 0, compress_defaults: true } }
function makeEmptyParameter() { return { name: '', description: '', children: [], data_type: DataTypes.uint8, multiplicity: 1, default_value: [0] } }
function makeEmptyBitfield() { return { name: '', description: '', size_in_bits: 1 } }
function makeDefaultPlatform() { return { endianness: 'little', eeprom_size: 256, eeprom_page_size: 0, page_aligned_blocks: ['*'], external_headers: [], enter_critical_section_operation: null, exit_critical_section_operation: null, compiler_directives: { opening_pack_directive: null, closing_pack_directive: null, pack_attribute: null, block_placement_directives: {} } } }
function makeDefaultChecksum() { return { algo: 'crc' } }

// File menu
const openDataModelInput = document.getElementById('openDataModelInput')
const openPlatformInput = document.getElementById('openPlatformInput')
const openChecksumInput = document.getElementById('openChecksumInput')
openDataModelInput.addEventListener('change', handleOpenDataModel)
openPlatformInput.addEventListener('change', handleOpenPlatform)
openChecksumInput.addEventListener('change', handleOpenChecksum)

function handleOpenDataModel(e) {
    const f = e.target.files[0]; if (!f) return; if (!f.name.endsWith('.json')) { alert('Only .json allowed'); return }
    const reader = new FileReader(); reader.onload = () => {
        try {
            const raw = JSON.parse(reader.result);
            // restore numeric types (including BigInt markers) into the live model
            state.dataModel = raw;
            restoreNumericTypesAfterLoad(state.dataModel);
            validateDataModelLoad();
            // Collapse all blocks on load
            collapseAllBlocks();
            renderTree();
            setStatus('Loaded data model file: ' + f.name)
        } catch (err) { alert('Failed to deserialize data model JSON:\n' + err.message); setStatus('Failed loading data model') }
        finally {
            // Reset input value so the same file can be loaded again
            e.target.value = '';
        }
    }
    reader.readAsText(f)
}

function handleOpenPlatform(e) {
    const f = e.target.files[0]; if (!f) return; if (!f.name.endsWith('.json')) { alert('Only .json allowed'); return }
    const reader = new FileReader(); reader.onload = () => {
        try {
            const raw = JSON.parse(reader.result);
            state.platformSettings = raw;
            // coerce numeric-looking fields back to Number/BigInt
            restoreNumericTypesAfterLoad(state.platformSettings);
            validatePlatformLoad(); setStatus('Loaded platform settings file: ' + f.name); renderPlatformSettings()
        } catch (err) { alert('Failed to deserialize platform settings JSON:\n' + err.message); setStatus('Failed loading platform settings') }
        finally {
            // Reset input value so the same file can be loaded again
            e.target.value = '';
        }
    }
    reader.readAsText(f)
}

function handleOpenChecksum(e) {
    const f = e.target.files[0]; if (!f) return; if (!f.name.endsWith('.json')) { alert('Only .json allowed'); return }
    const reader = new FileReader(); reader.onload = () => {
        try {
            const raw = JSON.parse(reader.result);
            state.checksumSettings = raw;
            restoreNumericTypesAfterLoad(state.checksumSettings);
            setStatus('Loaded checksum settings file: ' + f.name); renderChecksum()
        } catch (err) { alert('Failed to deserialize checksum settings JSON:\n' + err.message); setStatus('Failed loading checksum settings') }
        finally {
            // Reset input value so the same file can be loaded again
            e.target.value = '';
        }
    }
    reader.readAsText(f)
}

function validateDataModelLoad() {
    try {
        if (!state.dataModel) throw new Error('No object'); // minimal structural checks
        if (!('children' in state.dataModel)) state.dataModel.children = []
        // ensure defaults for nested items
        for (const b of state.dataModel.children) { if (!('children' in b)) b.children = []; for (const p of b.children) { if (!('children' in p)) p.children = []; if (!('default_value' in p)) p.default_value = [0]; } }
        // Use JS validation mirroring Python validate() but simpler - major checks
        const errs = [];
        if ([1, 2, 4].indexOf(state.dataModel.checksum_size) === -1) errs.push('checksum_size must be 1,2 or 4');
        if (state.dataModel.children.length === 0) errs.push('datamodel must contain at least one block');
        if (errs.length) throw new Error(errs.join('\n'))
    } catch (err) { throw err }
}

function validatePlatformLoad() {
    try {
        if (!state.platformSettings) throw new Error('No object'); // minimal checks
        const eps = state.platformSettings.eeprom_page_size;
        if (typeof eps !== 'number' && typeof eps !== 'bigint') throw new Error('eeprom_page_size must be a number');
        // allow 0 meaning 'no page size' or a positive power of two up to 32768
        const epsNum = Number(eps);
        if (epsNum < 0) throw new Error('eeprom_page_size invalid');
        if (epsNum === 1) throw new Error('eeprom_page_size of 1 is not allowed; must be 0 or a power of two >=2 up to 32768');
        if (epsNum !== 0 && (epsNum > 32768 || (epsNum & (epsNum - 1)) !== 0)) throw new Error('eeprom_page_size must be 0 or a power of two (>=2) up to 32768');
    } catch (err) { throw err }
}

// Validation helpers ported from Python
function is_valid_identifier(str) { if (str === null || str === '') return false; if (!/^[a-zA-Z_]/.test(str)) return false; if (!/^[a-zA-Z0-9_]*$/.test(str.slice(1))) return false; return true }
function is_power_of_2(num) { if (num <= 1) return false; return (num & (num - 1)) === 0 }
function is_valid_filename(fname) { if (!fname) return false; if (fname.length > 255) return false; return /^[a-zA-Z0-9_.-]+$/.test(fname.trim().replace(/[<> \"]/g, '')) }

function get_duplicate_names(nodes) { const seen = new Set(); const dup = new Set(); for (const n of nodes) { if (seen.has(n.name)) dup.add(n.name); seen.add(n.name) } return Array.from(dup) }

function have_type_mismatch(val, param_type) {
    // BigInt values are considered integers. Floats are numbers with fractional part.
    // NaN is allowed for float types
    if (typeof val === 'number' && isNaN(val)) {
        return !(param_type === DataTypes.float32 || param_type === DataTypes.float64);
    }
    if (typeof val === 'bigint') {
        // bigint provided for float type -> allow it (floats can represent integers)
        return false;
    }
    if (typeof val === 'number') {
        const isFloat = !Number.isInteger(val);
        // Only reject if a float (with fractional part) is provided for an integer type
        // Integers are acceptable for float types
        if (isFloat && !(param_type === DataTypes.float32 || param_type === DataTypes.float64)) return true;
    }
    return false;
}

function is_out_of_range(val, param_type) {
    // handle BigInt or Number
    const toBig = (x) => (typeof x === 'bigint' ? x : BigInt(Math.trunc(Number(x) || 0)));
    if (param_type === DataTypes.float32 || param_type === DataTypes.float64) {
        if (typeof val === 'number' && !Number.isInteger(val)) {
            if (param_type === DataTypes.float32 && (val < -3.4e38 || val > 3.4e38)) return true;
        }
        // integers stored as bigint/number can be accepted for floats; no further range checks here
        return false;
    }

    // integer types
    const vBig = toBig(val);
    switch (param_type) {
        case DataTypes.uint8: if (vBig < 0n || vBig > 0xFFn) return true; break;
        case DataTypes.uint16: if (vBig < 0n || vBig > 0xFFFFn) return true; break;
        case DataTypes.uint32: if (vBig < 0n || vBig > 0xFFFFFFFFn) return true; break;
        case DataTypes.uint64: if (vBig < 0n || vBig > 0xFFFFFFFFFFFFFFFFn) return true; break;
        case DataTypes.int8: if (vBig < -128n || vBig > 127n) return true; break;
        case DataTypes.int16: if (vBig < -32768n || vBig > 32767n) return true; break;
        case DataTypes.int32: if (vBig < -2147483648n || vBig > 2147483647n) return true; break;
        case DataTypes.int64: if (vBig < -9223372036854775808n || vBig > 9223372036854775807n) return true; break;
    }
    return false;
}

// Clamp a parsed value to the allowed range for param_type.
// Returns a value of the same kind (Number or BigInt) when possible.
function clampValueToType(val, param_type) {
    // floats
    if (param_type === DataTypes.float32 || param_type === DataTypes.float64) {
        if (typeof val !== 'number') return val; // don't coerce bigint -> float implicitly
        if (param_type === DataTypes.float32) {
            const MIN = -3.4e38; const MAX = 3.4e38; if (val < MIN) return MIN; if (val > MAX) return MAX; return val;
        }
        return val; // float64: allow JS Number range
    }

    // integer types: operate in BigInt domain for correctness
    const toBig = (x) => (typeof x === 'bigint' ? x : BigInt(Math.trunc(Number(x) || 0)));
    let vBig = toBig(val);
    let minB = null, maxB = null;
    switch (param_type) {
        case DataTypes.uint8: minB = 0n; maxB = 0xFFn; break;
        case DataTypes.uint16: minB = 0n; maxB = 0xFFFFn; break;
        case DataTypes.uint32: minB = 0n; maxB = 0xFFFFFFFFn; break;
        case DataTypes.uint64: minB = 0n; maxB = 0xFFFFFFFFFFFFFFFFn; break;
        case DataTypes.int8: minB = -128n; maxB = 127n; break;
        case DataTypes.int16: minB = -32768n; maxB = 32767n; break;
        case DataTypes.int32: minB = -2147483648n; maxB = 2147483647n; break;
        case DataTypes.int64: minB = -9223372036854775808n; maxB = 9223372036854775807n; break;
        default: return val;
    }
    if (vBig < minB) vBig = minB; if (vBig > maxB) vBig = maxB;
    // prefer to return BigInt if original was BigInt or vBig is outside safe integer range
    const MAX_SAFE = BigInt(Number.MAX_SAFE_INTEGER);
    if (typeof val === 'bigint' || vBig > MAX_SAFE || vBig < -MAX_SAFE) return vBig;
    return Number(vBig);
}

function is_instance_count_valid(block) { if (block.instance_count < 1) return false; if (block.management_type === ManagementTypes.Basic && block.instance_count !== 1) return false; if (block.management_type === ManagementTypes.BackupCopy && block.instance_count !== 2) return false; if ((block.management_type === ManagementTypes.MultiProfile || block.management_type === ManagementTypes.WearLeveling) && (block.instance_count < 2 || block.instance_count > 15)) return false; return true }

// Helper to push validation error with structured info
function pushValidationError(errors, message, path) {
    errors.push({ message: message, path: path || null });
}

// Helper to validate block structure
function validateBlock(block, blockPath, errors) {
    if (!is_valid_identifier((block.name || '').trim())) {
        pushValidationError(errors, `Block '${block.name}' has invalid name!`, blockPath);
    }
    if (!is_instance_count_valid(block)) {
        pushValidationError(errors, `Block '${block.name}' has invalid 'instance_count': ${block.instance_count}`, blockPath);
    }
    if (!block.children || block.children.length === 0) {
        pushValidationError(errors, `Block '${block.name}' must contain at least 1 parameter!`, blockPath);
    }
    const dupParams = get_duplicate_names(block.children || []);
    if (dupParams.length > 0) {
        pushValidationError(errors, `Block '${block.name}' contains parameters with duplicate names: ${dupParams.join(',')}`, blockPath);
    }
}

// Helper to validate parameter structure
function validateParameter(param, block, paramPath, errors) {
    if (!is_valid_identifier((param.name || '').trim())) {
        pushValidationError(errors, `Parameter '${param.name}' in block '${block.name}' has invalid name!`, paramPath);
    }
    const dupBF = get_duplicate_names(param.children || []);
    if (dupBF.length > 0) {
        pushValidationError(errors, `Parameter '${param.name}' in block '${block.name}' contains bitfields with duplicate names: ${dupBF.join(',')}`, paramPath);
    }
    if (param.multiplicity < 1) {
        pushValidationError(errors, `Parameter '${param.name}' in block '${block.name}' must have multiplicity of at least 1!`, paramPath);
    }
    if ((param.default_value || []).length !== param.multiplicity) {
        pushValidationError(errors, `Parameter '${param.name}' in block '${block.name}' has count of default values, which differ from its multiplicity!`, paramPath);
    }
    if ((param.children || []).length > 0 && (param.data_type === DataTypes.float32 || param.data_type === DataTypes.float64)) {
        pushValidationError(errors, `Parameter '${param.name}' in block '${block.name}' must be integer. Floating point types can't be used as containers for bitfields!`, paramPath);
    }
}

// Helper to validate bitfield structure
function validateBitfield(bf, param, block, bfPath, errors) {
    if (!is_valid_identifier((bf.name || '').trim())) {
        pushValidationError(errors, `Bitfield '${bf.name}' in parameter '${param.name}' in block '${block.name}' has invalid name!`, bfPath);
    }
    if (bf.size_in_bits < 1 || bf.size_in_bits > ((DataTypeSizes[param.data_type] || 1) * 8)) {
        pushValidationError(errors, `Bitfield '${bf.name}' in parameter '${param.name}' in block '${block.name}' has invalid size: ${bf.size_in_bits} bits`, bfPath);
    }
}

function validateAll() {
    const errors = []; const dm = state.dataModel; const ps = state.platformSettings;
    const push = pushValidationError;

    if (ps) {
        if (ps.eeprom_page_size < 0 || (ps.eeprom_page_size > 0 && !is_power_of_2(ps.eeprom_page_size))) push(errors, 'EEPROM page size should be 0 or positive power of 2');
        if (ps.external_headers && ps.external_headers.some(h => !is_valid_filename(h))) push(errors, 'Some external headers have invalid file name');
        if (ps.enter_critical_section_operation && !is_valid_identifier(ps.enter_critical_section_operation)) push(errors, "'enter_critical_section_operation' is not a valid C-language identifier");
        if (ps.exit_critical_section_operation && !is_valid_identifier(ps.exit_critical_section_operation)) push(errors, "'exit_critical_section_operation' is not a valid C-language identifier");
        if (ps.compiler_directives && ps.compiler_directives.pack_attribute && (ps.compiler_directives.opening_pack_directive || ps.compiler_directives.closing_pack_directive)) push(errors, 'You cannot have both compiler pack directive and attr defined at the same time');
    }

    if (!dm) { push(errors, 'No datamodel defined'); return errors; }
    if ([1, 2, 4].indexOf(dm.checksum_size) === -1) push(errors, 'The checksum size can be: 1,2 or 4 bytes');
    if (!dm.children || dm.children.length === 0) push(errors, 'The datamodel must contain at least one block!');

    const all_blocks = dm.children || [];
    const duplicate_blocks = get_duplicate_names(all_blocks);
    if (duplicate_blocks.length > 0) push(errors, 'Found blocks with duplicate names: ' + duplicate_blocks.join(','));

    for (let bi = 0; bi < all_blocks.length; bi++) {
        const block = all_blocks[bi];
        const blockPath = [bi];

        validateBlock(block, blockPath, errors);

        for (let pi = 0; pi < (block.children || []).length; pi++) {
            const param = block.children[pi];
            const pPath = [bi, pi];

            validateParameter(param, block, pPath, errors);

            // check defaults types and ranges
            for (let di = 0; di < (param.default_value || []).length; di++) {
                const dv = param.default_value[di];
                const dvPath = [bi, pi, di];
                if (have_type_mismatch(dv, param.data_type)) push(errors, `Parameter '${param.name}' in block '${block.name}' contains default values that don't match its data type!`, dvPath);
                if (is_out_of_range(dv, param.data_type)) push(errors, `Parameter '${param.name}' in block '${block.name}' has default values that are out of range`, dvPath);
            }

            const totalBits = (param.children || []).reduce((s, bf) => s + (bf.size_in_bits || 0), 0);
            if (totalBits > ((DataTypeSizes[param.data_type] || 1) * 8)) push(errors, `Parameter '${param.name}' in block '${block.name}': total bitfield size exceeds parameter size`, pPath);

            for (let bi2 = 0; bi2 < (param.children || []).length; bi2++) {
                const bf = param.children[bi2];
                const bfPath = [bi, pi, bi2];
                validateBitfield(bf, param, block, bfPath, errors);
            }
        }
    }
    return errors;
}

// wire Validate button
function renderErrorPanel(errors) {
    const panel = document.getElementById('errorPanel'); panel.innerHTML = '';
    if (!errors || errors.length === 0) {
        panel.style.display = 'block'; panel.style.background = '#152b18'; panel.style.color = '#bff7c6'; panel.innerHTML = '<strong>Validation passed ?</strong>'; return
    }
    panel.style.display = 'block'; panel.style.background = '#2b1a1a'; panel.style.color = '#ffd6d6';
    const ul = document.createElement('ol'); ul.style.margin = 0; ul.style.padding = '6px 12px';
    for (let i = 0; i < errors.length; i++) {
        const e = errors[i]; const li = document.createElement('li'); li.style.cursor = 'pointer'; li.textContent = e.message;
        li.addEventListener('click', () => {
            if (!e.path || e.path.length === 0) { selectNode([], 'datamodel'); }
            else { selectNode(e.path, e.path.length === 1 ? 'block' : (e.path.length === 2 ? 'parameter' : 'bitfield')) }
            window.scrollTo({ top: 0, behavior: 'smooth' })
        }); ul.appendChild(li)
    }
    panel.appendChild(ul);
}

// Wire up new Load/Save buttons in panels
document.getElementById('loadDataModelBtn').addEventListener('click', () => {
    openDataModelInput.click();
});

document.getElementById('saveDataModelBtn').addEventListener('click', () => {
    if (!state.dataModel) { alert('No data model to save'); return; }
    saveDataModel();
});

document.getElementById('loadPlatformBtn').addEventListener('click', () => {
    openPlatformInput.click();
});

document.getElementById('savePlatformBtn').addEventListener('click', () => {
    savePlatformSettings();
});

document.getElementById('loadChecksumBtn').addEventListener('click', () => {
    openChecksumInput.click();
});

document.getElementById('saveChecksumBtn').addEventListener('click', () => {
    saveChecksumParameters();
});

// Split button dropdown handlers
function setupSplitButton(menuBtnId, saveFunc, saveAsFunc) {
    const menuBtn = document.getElementById(menuBtnId);
    const menu = document.createElement('div');
    menu.className = 'split-button-menu';

    const saveAsBtn = document.createElement('button');
    saveAsBtn.textContent = '💾 Save As...';
    saveAsBtn.addEventListener('click', () => {
        menu.style.display = 'none';
        saveAsFunc();
    });

    menu.appendChild(saveAsBtn);
    document.body.appendChild(menu);

    menuBtn.addEventListener('click', (e) => {
        e.stopPropagation();
        // Close all other split button menus
        document.querySelectorAll('.split-button-menu').forEach(m => {
            if (m !== menu) m.style.display = 'none';
        });

        // Toggle this menu
        if (menu.style.display === 'block') {
            menu.style.display = 'none';
        } else {
            const rect = menuBtn.getBoundingClientRect();
            menu.style.display = 'block';

            // Calculate position, ensuring menu stays within screen bounds
            const menuRect = menu.getBoundingClientRect();
            let left = rect.left;
            let top = rect.bottom + 2;

            // Check right boundary
            if (left + menuRect.width > window.innerWidth) {
                left = window.innerWidth - menuRect.width - 8;
            }

            // Check bottom boundary
            if (top + menuRect.height > window.innerHeight) {
                top = rect.top - menuRect.height - 2;
            }

            // Check left boundary
            if (left < 8) {
                left = 8;
            }

            // Check top boundary
            if (top < 8) {
                top = 8;
            }

            menu.style.left = left + 'px';
            menu.style.top = top + 'px';
        }
    });
}

setupSplitButton('saveDataModelBtnMenu', saveDataModel, saveDataModelAs);
setupSplitButton('savePlatformBtnMenu', savePlatformSettings, savePlatformSettingsAs);
setupSplitButton('saveChecksumBtnMenu', saveChecksumParameters, saveChecksumParametersAs);

// Close split button menus on window click
window.addEventListener('click', () => {
    document.querySelectorAll('.split-button-menu').forEach(m => m.style.display = 'none');
});


function bigintReplacer(key, value) {
    if (typeof value === 'bigint') return value.toString();
    if (typeof value === 'number' && isNaN(value)) return "NaN";
    return value;
}

function saveDataModel() {
    if (!state.savedNames.dataModel) {
        const dmName = prompt('File name for exported data model', 'data_model.json');
        if (!dmName) return;
        state.savedNames.dataModel = dmName;
    }
    const dmStr = JSON.stringify(state.dataModel, bigintReplacer, 4);
    const dmBlob = new Blob([dmStr], { type: 'application/json' });
    downloadBlob(dmBlob, state.savedNames.dataModel);
    setStatus('Saved data model: ' + state.savedNames.dataModel);
}

function savePlatformSettings() {
    if (!state.platformSettings) { alert('No platform settings to save'); return; }
    if (!state.savedNames.platform) {
        const psName = prompt('File name for exported platform settings', 'platform_settings.json');
        if (!psName) return;
        state.savedNames.platform = psName;
    }

    // Normalize page_aligned_blocks before saving
    const platformCopy = JSON.parse(JSON.stringify(state.platformSettings, bigintReplacer));

    // Get all block names from data model
    const allBlockNames = (state.dataModel && state.dataModel.children)
        ? state.dataModel.children.map(block => block.name || '').filter(name => name)
        : [];

    if (allBlockNames.length > 0) {
        const alignedBlocks = platformCopy.page_aligned_blocks || [];
        const hasWildcard = alignedBlocks.includes('*');

        // Build the actual list of aligned blocks (resolve wildcard if present)
        let actualAlignedBlocks = [];
        if (hasWildcard) {
            actualAlignedBlocks = [...allBlockNames];
        } else {
            actualAlignedBlocks = alignedBlocks.filter(name => allBlockNames.includes(name));
        }

        // Determine what to save
        if (actualAlignedBlocks.length === allBlockNames.length) {
            // All blocks are aligned - save as wildcard
            platformCopy.page_aligned_blocks = ['*'];
        } else {
            // Some blocks are not aligned - save explicit list
            platformCopy.page_aligned_blocks = actualAlignedBlocks;
        }
    }

    const psStr = JSON.stringify(platformCopy, null, 4);
    const psBlob = new Blob([psStr], { type: 'application/json' });
    downloadBlob(psBlob, state.savedNames.platform);
    setStatus('Saved platform settings: ' + state.savedNames.platform);
}

function saveChecksumParameters() {
    if (!state.checksumSettings) { alert('No checksum parameters to save'); return; }
    if (!state.savedNames.checksum) {
        const csName = prompt('File name for exported checksum parameters', 'checksum_parameters.json');
        if (!csName) return;
        state.savedNames.checksum = csName;
    }
    const csStr = JSON.stringify(state.checksumSettings, bigintReplacer, 4);
    const csBlob = new Blob([csStr], { type: 'application/json' });
    downloadBlob(csBlob, state.savedNames.checksum);
    setStatus('Saved checksum settings: ' + state.savedNames.checksum);
}

function saveDataModelAs() {
    if (!state.dataModel) { alert('No data model to save'); return; }
    const dmName = prompt('File name for exported data model', state.savedNames.dataModel || 'data_model.json');
    if (!dmName) return;
    state.savedNames.dataModel = dmName;
    saveDataModel();
}

function savePlatformSettingsAs() {
    if (!state.platformSettings) { alert('No platform settings to save'); return; }
    const psName = prompt('File name for exported platform settings', state.savedNames.platform || 'platform_settings.json');
    if (!psName) return;
    state.savedNames.platform = psName;
    savePlatformSettings();
}

function saveChecksumParametersAs() {
    if (!state.checksumSettings) { alert('No checksum parameters to save'); return; }
    const csName = prompt('File name for exported checksum parameters', state.savedNames.checksum || 'checksum_parameters.json');
    if (!csName) return;
    state.savedNames.checksum = csName;
    saveChecksumParameters();
}

function saveAs() { // create two files using download links
    const dmName = prompt('File name for exported data model', state.savedNames.dataModel || 'data_model.json');
    if (!dmName) return;
    const psName = prompt('File name for exported platform settings', state.savedNames.platform || 'platform_settings.json');
    if (!psName) return;
    state.savedNames.dataModel = dmName; state.savedNames.platform = psName; state.isSavedAs = true;
    save();
}

function save() {
    if (!state.isSavedAs) { alert('Use Save as... first'); return }
    // Use a replacer to serialize BigInt as decimal strings so JSON.stringify doesn't throw and round-trip is preserved.
    // Also serialize NaN as the string "NaN" for float parameters
    const dmStr = JSON.stringify(state.dataModel, bigintReplacer, 4);

    // Normalize page_aligned_blocks before saving
    const platformCopy = JSON.parse(JSON.stringify(state.platformSettings, bigintReplacer));

    // Get all block names from data model
    const allBlockNames = (state.dataModel && state.dataModel.children)
        ? state.dataModel.children.map(block => block.name || '').filter(name => name)
        : [];

    if (allBlockNames.length > 0) {
        const alignedBlocks = platformCopy.page_aligned_blocks || [];
        const hasWildcard = alignedBlocks.includes('*');

        // Build the actual list of aligned blocks (resolve wildcard if present)
        let actualAlignedBlocks = [];
        if (hasWildcard) {
            actualAlignedBlocks = [...allBlockNames];
        } else {
            actualAlignedBlocks = alignedBlocks.filter(name => allBlockNames.includes(name));
        }

        // Determine what to save
        if (actualAlignedBlocks.length === allBlockNames.length) {
            // All blocks are aligned - save as wildcard
            platformCopy.page_aligned_blocks = ['*'];
        } else {
            // Some blocks are not aligned - save explicit list
            platformCopy.page_aligned_blocks = actualAlignedBlocks;
        }
    }

    const psStr = JSON.stringify(platformCopy, null, 4);
    const dmBlob = new Blob([dmStr], { type: 'application/json' });
    const psBlob = new Blob([psStr], { type: 'application/json' });
    downloadBlob(dmBlob, state.savedNames.dataModel); downloadBlob(psBlob, state.savedNames.platform);
    setStatus('Saved configuration (BigInt serialized as strings)')
}
function downloadBlob(blob, name) { const a = document.createElement('a'); a.href = URL.createObjectURL(blob); a.download = name; document.body.appendChild(a); a.click(); a.remove(); }

// New actions: removed top toolbar creation buttons to rely on context menu per spec

// tree rendering
function renderTree() {
    const treeRoot = document.getElementById('treeRoot');
    const hint = document.getElementById('treeHint');

    if (!state.dataModel) {
        treeRoot.innerHTML = '';
        hint.style.display = 'block';
        return;
    }

    hint.style.display = 'none';

    // Use DocumentFragment for better performance
    const fragment = document.createDocumentFragment();
    const dmEl = createNodeElement('datamodel', state.dataModel, []);
    fragment.appendChild(dmEl);

    // Clear and append in one operation
    treeRoot.innerHTML = '';
    treeRoot.appendChild(fragment);
}

// Expand/collapse utilities
function collapseAllNodes() {
    state.collapsedPaths.clear();
    // collapse all blocks and parameters by adding their paths
    if (!state.dataModel || !state.dataModel.children) return;
    for (let bi = 0; bi < state.dataModel.children.length; bi++) {
        const bpath = JSON.stringify([bi]); state.collapsedPaths.add(bpath);
        const block = state.dataModel.children[bi];
        for (let pi = 0; block && block.children && pi < block.children.length; pi++) {
            const ppath = JSON.stringify([bi, pi]); state.collapsedPaths.add(ppath);
        }
    }
}

function collapseAllBlocks() {
    state.collapsedPaths.clear();
    // collapse only blocks (not parameters)
    if (!state.dataModel || !state.dataModel.children) return;
    for (let bi = 0; bi < state.dataModel.children.length; bi++) {
        const bpath = JSON.stringify([bi]); state.collapsedPaths.add(bpath);
    }
}

function expandAllNodes() {
    state.collapsedPaths.clear();
}

function expandNodeRecursively(path, type) {
    // Expand this node and all descendants recursively
    const pathKey = JSON.stringify(path);
    state.collapsedPaths.delete(pathKey);

    // Get the actual node
    const node = getNodeAtPath(path);
    if (!node || !node.children) { renderTree(); return; }

    // Recursively expand children
    if (type === 'datamodel') {
        // Expand all blocks
        for (let bi = 0; bi < node.children.length; bi++) {
            expandNodeRecursively([bi], 'block');
        }
    } else if (type === 'block') {
        // Expand all parameters
        for (let pi = 0; pi < node.children.length; pi++) {
            expandNodeRecursively(path.concat(pi), 'parameter');
        }
    } else if (type === 'parameter') {
        // Expand all bitfields (they don't have children, so just expand this node)
        for (let bfi = 0; bfi < node.children.length; bfi++) {
            const bfPath = path.concat(bfi);
            state.collapsedPaths.delete(JSON.stringify(bfPath));
        }
    }
    renderTree();
}

function collapseNodeRecursively(path, type) {
    // Collapse this node and all descendants recursively
    const pathKey = JSON.stringify(path);
    state.collapsedPaths.add(pathKey);

    // Get the actual node
    const node = getNodeAtPath(path);
    if (!node || !node.children) { renderTree(); return; }

    // Recursively collapse children
    if (type === 'datamodel') {
        // Collapse all blocks
        for (let bi = 0; bi < node.children.length; bi++) {
            collapseNodeRecursively([bi], 'block');
        }
    } else if (type === 'block') {
        // Collapse all parameters
        for (let pi = 0; pi < node.children.length; pi++) {
            collapseNodeRecursively(path.concat(pi), 'parameter');
        }
    } else if (type === 'parameter') {
        // Collapse all bitfields
        for (let bfi = 0; bfi < node.children.length; bfi++) {
            const bfPath = path.concat(bfi);
            state.collapsedPaths.add(JSON.stringify(bfPath));
        }
    }
    renderTree();
}

function createNodeElement(type, obj, path) {
    const wrapper = document.createElement('div'); wrapper.className = 'node-wrapper';
    const div = document.createElement('div'); div.className = 'node'; div.tabIndex = 0; div.dataset.path = JSON.stringify(path); div.dataset.type = type;
    const header = document.createElement('div'); header.className = 'node-header';
    // title/meta column
    const left = document.createElement('div'); left.style.display = 'flex'; left.style.flexDirection = 'column'; left.style.minWidth = '0';
    const title = document.createElement('div'); title.textContent = obj.name || `<unnamed ${type}>`; title.style.fontWeight = '600'; left.appendChild(title);
    const meta = document.createElement('div'); meta.className = 'meta'; meta.textContent = type + (type === 'parameter' ? ` (${Object.keys(DataTypes).find(k => DataTypes[k] === obj.data_type) || obj.data_type})` : ''); left.appendChild(meta);
    header.appendChild(left);
    const right = document.createElement('div'); right.style.display = 'flex'; right.style.alignItems = 'center'; right.style.gap = '6px';
    // If this node can have children, add a toggle button
    const canHaveChildren = (type === 'datamodel' || type === 'block' || type === 'parameter');
    if (canHaveChildren) {
        const toggle = document.createElement('button'); toggle.className = 'toggle-btn';
        const pathKey = JSON.stringify(path);
        const isCollapsed = state.collapsedPaths.has(pathKey);
        // caret: right-pointing when collapsed, down when expanded
        toggle.textContent = isCollapsed ? '\u25B6' : '\u25BC'; toggle.title = isCollapsed ? 'Expand children' : 'Collapse children';
        toggle.addEventListener('click', (ev) => { ev.stopPropagation(); animateToggle(path); });
        // Insert toggle at the start of the header
        header.insertBefore(toggle, header.firstChild);
    }
    else {
        // keep alignment by inserting a spacer where the toggle would be
        const sp = document.createElement('span'); sp.className = 'toggle-spacer'; header.insertBefore(sp, header.firstChild);
    }
    // attach header and right controls to the node
    // add small icon for node type
    const icon = document.createElement('span'); icon.className = 'node-icon';
    if (type === 'datamodel') icon.textContent = '📦';
    else if (type === 'block') icon.textContent = '📋';
    else if (type === 'parameter') icon.textContent = '🔧';
    else icon.textContent = '🔹';
    header.insertBefore(icon, header.firstChild);

    header.appendChild(right);
    div.appendChild(header);

    wrapper.appendChild(div);

    // children are rendered in a separate block below the header so they appear on their own lines
    if (type === 'datamodel' && obj.children) {
        // datamodel children are blocks; render inside a child-container so they display under the root
        const childContainer = document.createElement('div'); childContainer.className = 'child-container'; childContainer.dataset.path = JSON.stringify(path);
        childContainer.style.marginLeft = '12px'; childContainer.style.marginTop = '6px';
        if (state.collapsedPaths.has(JSON.stringify(path))) childContainer.style.display = 'none';
        for (let bi = 0; bi < obj.children.length; bi++) { childContainer.appendChild(createNodeElement('block', obj.children[bi], [bi])) }
        wrapper.appendChild(childContainer);
    }
    if (type === 'block' && obj.children) {
        const childContainer = document.createElement('div'); childContainer.className = 'child-container'; childContainer.dataset.path = JSON.stringify(path);
        childContainer.style.marginLeft = '12px'; childContainer.style.marginTop = '6px';
        if (state.collapsedPaths.has(JSON.stringify(path))) childContainer.style.display = 'none';
        for (let pi = 0; pi < obj.children.length; pi++) { childContainer.appendChild(createNodeElement('parameter', obj.children[pi], path.concat(pi))) }
        wrapper.appendChild(childContainer);
    }
    if (type === 'parameter' && obj.children) {
        const childContainer = document.createElement('div'); childContainer.className = 'child-container'; childContainer.dataset.path = JSON.stringify(path);
        childContainer.style.marginLeft = '24px'; childContainer.style.marginTop = '6px';
        if (state.collapsedPaths.has(JSON.stringify(path))) childContainer.style.display = 'none';
        for (let bi = 0; bi < obj.children.length; bi++) { childContainer.appendChild(createNodeElement('bitfield', obj.children[bi], path.concat(bi))) }
        wrapper.appendChild(childContainer);
    }
    return wrapper
}

let selectedPath = null; let selectedType = null;
function selectNode(path, type) {
    // auto-expand ancestors so the selected node is visible
    const ancestors = ancestorPaths(path);
    for (const a of ancestors) state.collapsedPaths.delete(JSON.stringify(a));

    selectedPath = path; selectedType = type;
    // re-render so expanded ancestors become visible, then re-apply selection
    renderTree();
    document.querySelectorAll('.node').forEach(n => n.classList.remove('selected'));
    const nodes = document.querySelectorAll('.node');
    for (const n of nodes) {
        if (n.dataset.path === JSON.stringify(path)) {
            n.classList.add('selected'); // ensure it's scrolled into view
            n.scrollIntoView({ block: 'nearest', behavior: 'smooth' }); break
        }
    }
    renderProps();
}

function ancestorPaths(path) {
    const res = [];
    for (let i = 0; i < path.length; i++) {
        res.push(path.slice(0, i + 1));
    }
    if (res.length) res.pop();
    return res;
}

// animate expand/collapse of the node's child container at path
function animateToggle(path) {
    const pathKey = JSON.stringify(path);
    const wasCollapsed = state.collapsedPaths.has(pathKey);
    // find node element by dataset.path
    const nodes = document.querySelectorAll('.node');
    let nodeEl = null;
    for (const n of nodes) { if (n.dataset.path === pathKey) { nodeEl = n; break } }
    if (!nodeEl) {
        if (wasCollapsed) state.collapsedPaths.delete(pathKey); else state.collapsedPaths.add(pathKey);
        renderTree(); return;
    }
    const wrapper = nodeEl.parentElement; if (!wrapper) { if (wasCollapsed) state.collapsedPaths.delete(pathKey); else state.collapsedPaths.add(pathKey); renderTree(); return }
    const child = wrapper.querySelector('.child-container');
    if (!child) { if (wasCollapsed) state.collapsedPaths.delete(pathKey); else state.collapsedPaths.add(pathKey); renderTree(); return }

    // prepare transition
    child.style.transition = 'height 180ms ease';
    if (wasCollapsed) {
        // expand
        child.style.display = 'block';
        const target = child.scrollHeight;
        child.style.height = '0px';
        requestAnimationFrame(() => { child.style.height = target + 'px'; });
        child.addEventListener('transitionend', function te() { child.style.height = ''; child.style.transition = ''; child.removeEventListener('transitionend', te); }, { once: true });
        state.collapsedPaths.delete(pathKey);
    } else {
        // collapse
        const from = child.scrollHeight;
        child.style.height = from + 'px';
        requestAnimationFrame(() => { child.style.height = '0px'; });
        child.addEventListener('transitionend', function te() { child.style.display = 'none'; child.style.height = ''; child.style.transition = ''; child.removeEventListener('transitionend', te); }, { once: true });
        state.collapsedPaths.add(pathKey);
    }
    // update caret glyph
    const btn = wrapper.querySelector('.toggle-btn'); if (btn) btn.textContent = wasCollapsed ? '\u25BC' : '\u25B6';
}

function getNodeAtPath(path) { if (!state.dataModel) return null; if (!path || path.length === 0) return state.dataModel; let cur = state.dataModel; for (let i = 0; i < path.length; i++) { cur = cur.children[path[i]]; } return cur }

function deleteNodeAtPath(path) {
    if (!confirm('Delete node?')) return; // determine parent
    if (path.length === 1) { state.dataModel.children.splice(path[0], 1); setStatus(`Block deleted`); renderTree(); renderProps(); return }
    if (path.length === 2) { const block = state.dataModel.children[path[0]]; block.children.splice(path[1], 1); setStatus('Parameter deleted'); renderTree(); renderProps(); return }
    if (path.length === 3) { const param = state.dataModel.children[path[0]].children[path[1]]; param.children.splice(path[2], 1); setStatus('Bitfield deleted'); renderTree(); renderProps(); return }
}

function moveNodeUp(path) {
    if (!path || path.length === 0) return;
    const idx = path[path.length - 1];
    if (idx === 0) { setStatus('Already at the top'); return; }

    // Get parent array
    let parent;
    if (path.length === 1) {
        parent = state.dataModel.children;
    } else if (path.length === 2) {
        parent = state.dataModel.children[path[0]].children;
    } else if (path.length === 3) {
        parent = state.dataModel.children[path[0]].children[path[1]].children;
    }

    if (!parent) return;

    // Swap with previous item
    [parent[idx - 1], parent[idx]] = [parent[idx], parent[idx - 1]];

    // Update selected path to follow the moved node
    const newPath = [...path];
    newPath[newPath.length - 1] = idx - 1;

    setStatus('Node moved up');
    selectNode(newPath, selectedType);
}

function moveNodeDown(path) {
    if (!path || path.length === 0) return;
    const idx = path[path.length - 1];

    // Get parent array
    let parent;
    if (path.length === 1) {
        parent = state.dataModel.children;
    } else if (path.length === 2) {
        parent = state.dataModel.children[path[0]].children;
    } else if (path.length === 3) {
        parent = state.dataModel.children[path[0]].children[path[1]].children;
    }

    if (!parent || idx >= parent.length - 1) { setStatus('Already at the bottom'); return; }

    // Swap with next item
    [parent[idx], parent[idx + 1]] = [parent[idx + 1], parent[idx]];

    // Update selected path to follow the moved node
    const newPath = [...path];
    newPath[newPath.length - 1] = idx + 1;

    setStatus('Node moved down');
    selectNode(newPath, selectedType);
}

// Context menu logic
const contextMenu = document.getElementById('contextMenu'); window.addEventListener('click', () => { contextMenu.style.display = 'none' })

// Show context menu when user right-clicks on the tree background (not on a node)
const treeEl = document.getElementById('tree');

// Use event delegation for better performance
treeEl.addEventListener('click', (e) => {
    const node = e.target.closest('.node');
    if (!node) return;

    // Check if clicking on a button within the node
    if (e.target.closest('button')) return;

    const path = JSON.parse(node.dataset.path || '[]');
    const type = node.dataset.type;
    selectNode(path, type);
});

treeEl.addEventListener('keydown', (e) => {
    const node = e.target.closest('.node');
    if (!node) return;

    const path = JSON.parse(node.dataset.path || '[]');

    if (e.key === 'Delete') {
        deleteNodeAtPath(path);
    } else if (e.shiftKey && e.key === 'ArrowUp') {
        e.preventDefault();
        moveNodeUp(path);
    } else if (e.shiftKey && e.key === 'ArrowDown') {
        e.preventDefault();
        moveNodeDown(path);
    }
});

treeEl.addEventListener('contextmenu', (e) => {
    e.preventDefault();
    const node = e.target.closest('.node');
    if (node) {
        const p = node.dataset.path ? JSON.parse(node.dataset.path) : null;
        const t = node.dataset.type || null;
        showContextMenuFor(p, t, e.clientX, e.clientY);
    } else {
        showContextMenuFor([], null, e.clientX, e.clientY);
    }
});
function showContextMenuFor(path, type, x, y) {
    contextMenu.innerHTML = '';
    contextMenu.style.left = x + 'px';
    contextMenu.style.top = y + 'px';
    contextMenu.style.display = 'block';

    // If tree is completely empty (no datamodel), allow creating a new datamodel
    if (!state.dataModel) {
        const btn = createContextButton('📦 Create new datamodel', () => {
            state.dataModel = makeEmptyDataModel();
            state.dataModel.children.push(makeEmptyBlock());
            renderTree();
            setStatus('Created new datamodel');
        });
        contextMenu.appendChild(btn);
        return;
    }

    // If user right-clicked on datamodel node or tree background, allow creating a block
    if (!path || path.length === 0 || type === 'datamodel') {
        const addBlock = createContextButton('📋 Create block', () => {
            state.dataModel.children.push(makeEmptyBlock());
            // Expand the datamodel node to show the new block
            state.collapsedPaths.delete(JSON.stringify([]));
            renderTree();
            setStatus('Created block');
        });
        contextMenu.appendChild(addBlock);

        // Add separator and expand/collapse options for datamodel
        if (state.dataModel && state.dataModel.children && state.dataModel.children.length > 0) {
            const { sep, expandBtn, collapseBtn } = createExpandCollapseButtons(path || [], type);
            contextMenu.appendChild(sep);
            contextMenu.appendChild(expandBtn);
            contextMenu.appendChild(collapseBtn);
        }
        return;
    }

    // otherwise based on type
    if (type === 'block') {
        const addParam = createContextButton('🔧 Create parameter', () => {
            const bidx = path[0];
            const block = state.dataModel.children[bidx];
            block.children.push(makeEmptyParameter());
            // Expand the block node to show the new parameter
            state.collapsedPaths.delete(JSON.stringify(path));
            renderTree();
            setStatus('Created parameter');
        });
        contextMenu.appendChild(addParam);

        const del = createContextButton('🗑️ Delete block (Del)', () => {
            if (confirm('Delete block?')) {
                state.dataModel.children.splice(path[0], 1);
                renderTree();
                setStatus('Block deleted');
            }
        });
        contextMenu.appendChild(del);

        // Add move up/down options
        contextMenu.appendChild(createContextSeparator());
        const moveUp = createContextButton('⬆️ Move up (Shift+↑)', () => {
            contextMenu.style.display = 'none';
            moveNodeUp(path);
        });
        const moveDown = createContextButton('⬇️ Move down (Shift+↓)', () => {
            contextMenu.style.display = 'none';
            moveNodeDown(path);
        });

        // Disable buttons if at first/last position or only one item
        const idx = path[0];
        const totalBlocks = state.dataModel.children.length;
        if (idx === 0 || totalBlocks <= 1) {
            moveUp.disabled = true;
            moveUp.style.opacity = '0.5';
            moveUp.style.cursor = 'not-allowed';
        }
        if (idx >= totalBlocks - 1 || totalBlocks <= 1) {
            moveDown.disabled = true;
            moveDown.style.opacity = '0.5';
            moveDown.style.cursor = 'not-allowed';
        }

        contextMenu.appendChild(moveUp);
        contextMenu.appendChild(moveDown);

        // Add separator and expand/collapse options
        const { sep, expandBtn, collapseBtn } = createExpandCollapseButtons(path, type);
        contextMenu.appendChild(sep);
        contextMenu.appendChild(expandBtn);
        contextMenu.appendChild(collapseBtn);
    }

    if (type === 'parameter') {
        const addBF = createContextButton('🔹 Create bitfield', () => {
            const [bi, pi] = path;
            const param = state.dataModel.children[bi].children[pi];
            // only allow if not float
            if (param.data_type === DataTypes.float32 || param.data_type === DataTypes.float64) {
                alert('Cannot create bitfield for float parameter');
                return;
            }
            // recompute remaining to be safe
            const capacity = (DataTypeSizes[(param && param.data_type) || 0] || 1) * 8;
            const used = (param.children || []).reduce((s, bf) => s + (Number(bf.size_in_bits || 0)), 0);
            const remaining = Math.max(0, capacity - used);
            if (remaining <= 0) {
                alert('No remaining bits available in this parameter');
                return;
            }
            param.children.push(makeEmptyBitfield());
            // Expand the parameter node to show the new bitfield
            state.collapsedPaths.delete(JSON.stringify(path));
            renderTree();
            setStatus('Created bitfield');
        });

        // compute remaining bits for this parameter
        (function () {
            const [bi, pi] = path;
            const param = state.dataModel.children[bi].children[pi];
            const capacity = (DataTypeSizes[(param && param.data_type) || 0] || 1) * 8;
            const used = (param.children || []).reduce((s, bf) => s + (Number(bf.size_in_bits || 0)), 0);
            const remaining = Math.max(0, capacity - used);
            if (param.data_type === DataTypes.float32 || param.data_type === DataTypes.float64) {
                addBF.disabled = true;
                addBF.title = 'Cannot create bitfield for float parameter';
            } else if (remaining <= 0) {
                addBF.disabled = true;
                addBF.title = 'No remaining bits available in this parameter';
            }
        })();
        contextMenu.appendChild(addBF);

        const del = createContextButton('🗑️ Delete parameter (Del)', () => {
            if (confirm('Delete parameter?')) {
                state.dataModel.children[path[0]].children.splice(path[1], 1);
                renderTree();
                setStatus('Parameter deleted');
            }
        });
        contextMenu.appendChild(del);

        // Add move up/down options
        contextMenu.appendChild(createContextSeparator());
        const moveUp = createContextButton('⬆️ Move up (Shift+↑)', () => {
            contextMenu.style.display = 'none';
            moveNodeUp(path);
        });
        const moveDown = createContextButton('⬇️ Move down (Shift+↓)', () => {
            contextMenu.style.display = 'none';
            moveNodeDown(path);
        });

        // Disable buttons if at first/last position or only one item
        const idx = path[1];
        const totalParams = state.dataModel.children[path[0]].children.length;
        if (idx === 0 || totalParams <= 1) {
            moveUp.disabled = true;
            moveUp.style.opacity = '0.5';
            moveUp.style.cursor = 'not-allowed';
        }
        if (idx >= totalParams - 1 || totalParams <= 1) {
            moveDown.disabled = true;
            moveDown.style.opacity = '0.5';
            moveDown.style.cursor = 'not-allowed';
        }

        contextMenu.appendChild(moveUp);
        contextMenu.appendChild(moveDown);

        // Add separator and expand/collapse options
        const { sep, expandBtn, collapseBtn } = createExpandCollapseButtons(path, type);
        contextMenu.appendChild(sep);
        contextMenu.appendChild(expandBtn);
        contextMenu.appendChild(collapseBtn);
    }

    if (type === 'bitfield') {
        const del = createContextButton('🗑️ Delete bitfield (Del)', () => {
            if (confirm('Delete bitfield?')) {
                state.dataModel.children[path[0]].children[path[1]].children.splice(path[2], 1);
                renderTree();
                setStatus('Bitfield deleted');
            }
        });
        contextMenu.appendChild(del);

        // Add move up/down options
        contextMenu.appendChild(createContextSeparator());
        const moveUp = createContextButton('⬆️ Move up (Shift+↑)', () => {
            contextMenu.style.display = 'none';
            moveNodeUp(path);
        });
        const moveDown = createContextButton('⬇️ Move down (Shift+↓)', () => {
            contextMenu.style.display = 'none';
            moveNodeDown(path);
        });

        // Disable buttons if at first/last position or only one item
        const idx = path[2];
        const totalBitfields = state.dataModel.children[path[0]].children[path[1]].children.length;
        if (idx === 0 || totalBitfields <= 1) {
            moveUp.disabled = true;
            moveUp.style.opacity = '0.5';
            moveUp.style.cursor = 'not-allowed';
        }
        if (idx >= totalBitfields - 1 || totalBitfields <= 1) {
            moveDown.disabled = true;
            moveDown.style.opacity = '0.5';
            moveDown.style.cursor = 'not-allowed';
        }

        contextMenu.appendChild(moveUp);
        contextMenu.appendChild(moveDown);
    }
}

// Render properties panel for selection
function renderProps() {
    const container = document.getElementById('propsContent'); container.innerHTML = '';
    if (!selectedPath) { container.innerHTML = '<p class="doc-hint">Select a node to edit its properties</p>'; return }
    const node = getNodeAtPath(selectedPath); const type = selectedType; if (!node) return;

    // Create title with node name (escaped to handle angle brackets)
    const title = document.createElement('h3');
    const nodeName = node.name || `<unnamed ${type}>`;

    // Build full path for parameters and bitfields
    let pathText = '';
    if (type === 'parameter' && selectedPath.length >= 2) {
        // Get parent block name
        const blockPath = selectedPath.slice(0, 1);
        const blockNode = getNodeAtPath(blockPath);
        const blockName = (blockNode && blockNode.name) || '<unnamed block>';
        pathText = `${blockName}/${nodeName}`;
    } else if (type === 'bitfield' && selectedPath.length >= 3) {
        // Get parent block and parameter names
        const blockPath = selectedPath.slice(0, 1);
        const paramPath = selectedPath.slice(0, 2);
        const blockNode = getNodeAtPath(blockPath);
        const paramNode = getNodeAtPath(paramPath);
        const blockName = (blockNode && blockNode.name) || '<unnamed block>';
        const paramName = (paramNode && paramNode.name) || '<unnamed parameter>';
        pathText = `${blockName}/${paramName}/${nodeName}`;
    } else {
        pathText = nodeName;
    }

    // Create the title with styled parts
    const editLabel = document.createElement('span');
    editLabel.textContent = `Edit ${type}: `;
    editLabel.style.fontWeight = 'normal';
    editLabel.style.color = '#888';

    const pathLabel = document.createElement('span');
    pathLabel.textContent = pathText;
    // Keep the default h3 styling (color and bold) for the path

    title.appendChild(editLabel);
    title.appendChild(pathLabel);
    container.appendChild(title);

    // Special-case: datamodel root
    if (type === 'datamodel') {
        // name
        const nameInp = createInput('text', node.name || '', () => {
            const norm = normalizeName(nameInp.value);
            node.name = norm;
            nameInp.value = norm;
            setStatus('datamodel name changed');
            renderTree();
            selectNode([], 'datamodel');
        });
        nameInp.addEventListener('keydown', (ev) => {
            if (ev.key === 'Enter') {
                ev.preventDefault();
                const norm = normalizeName(nameInp.value);
                node.name = norm;
                nameInp.value = norm;
                setStatus('datamodel name changed');
                renderTree();
                selectNode([], 'datamodel');
            }
        });
        const nameProp = createProp('name', nameInp, FieldDocs.datamodel.name || '');
        container.appendChild(nameProp);

        // description
        const descInp = createInput('text', node.description || '', () => {
            node.description = descInp.value;
            setStatus('datamodel description changed');
        });
        const descProp = createProp(formatLabel('description'), descInp, FieldDocs.datamodel.description || '');
        container.appendChild(descProp);

        // checksum_size (custom dropdown to avoid native white popup)
        const csOptions = { '1': '1', '2': '2', '4': '4' };
        const csWrap = createCustomSelect(csOptions, String(node.checksum_size || 1), (newVal) => {
            node.checksum_size = Number(newVal);
            setStatus('datamodel checksum_size changed');
        });
        const csProp = createProp(formatLabel('checksum_size'), csWrap, FieldDocs.datamodel.checksum_size || '');
        container.appendChild(csProp);

        return;
    }
    // For each property except children - show inputs
    for (const key in node) {
        if (key === 'children' || key === 'default_pattern' || key === 'offset_in_eeprom' || key === 'size_in_eeprom' || key === 'multiplicity') continue;
        const val = node[key]; const prop = document.createElement('div'); prop.className = 'prop'; const label = document.createElement('label'); label.textContent = formatLabel(key);
        // Add tooltip from FieldDocs based on node type
        if (FieldDocs[type] && FieldDocs[type][key]) {
            label.title = FieldDocs[type][key];
        } else {
            label.title = key;
        }
        prop.appendChild(label);
        // doc hints are not available from Python here - skip
        // Ensure data_type is handled before generic numeric inputs so it doesn't become a spin-box
        if (key === 'data_type' && !Array.isArray(val)) {
            // Render as an editable dark custom-select for data_type
            const wrap = document.createElement('div'); wrap.className = 'custom-select';
            const cur = document.createElement('div'); cur.className = 'current';
            const dtName = Object.keys(DataTypes).find(k => DataTypes[k] === val) || String(val);
            cur.textContent = dtName; wrap.appendChild(cur);
            const menu = document.createElement('div'); menu.className = 'menu'; menu.style.display = 'none';
            for (const k of Object.keys(DataTypes)) {
                const v = DataTypes[k]; const it = document.createElement('div'); it.className = 'item'; it.textContent = k; it.dataset.val = v;
                if (v === val) it.style.fontWeight = '600';
                it.addEventListener('click', (e) => {
                    // update model
                    node.data_type = Number(it.dataset.val);
                    // when switching to float types, drop bitfields (they are invalid)
                    if (node.data_type === DataTypes.float32 || node.data_type === DataTypes.float64) {
                        node.children = [];
                    }
                    cur.textContent = k; menu.style.display = 'none'; setStatus(`Parameter data_type changed to ${k}`);
                    // re-render both tree and props to reflect new structure
                    renderTree(); renderProps();
                });
                menu.appendChild(it);
            }
            wrap.appendChild(menu);
            cur.addEventListener('click', (e) => { e.stopPropagation(); menu.style.display = (menu.style.display === 'none') ? 'block' : 'none' });
            prop.appendChild(wrap);
        }
        else if (typeof val === 'boolean') { const cb = document.createElement('input'); cb.type = 'checkbox'; cb.checked = val; cb.addEventListener('change', () => { node[key] = cb.checked; setStatus(`${type} ${key} changed`) }); prop.appendChild(cb) }
        // Special-case integer fields which need custom widgets for blocks
        else if (Number.isInteger(val) && !Array.isArray(val) && key !== 'name' && key !== 'description') {
            // management_type -> human-readable select
            if (type === 'block' && key === 'management_type') {
                const wrap = createCustomSelect(ManagementTypeLabels, String(val), (newVal) => {
                    node.management_type = Number(newVal);
                    // enforce instance_count defaults
                    if (node.management_type === ManagementTypes.Basic) node.instance_count = 1;
                    if (node.management_type === ManagementTypes.BackupCopy) node.instance_count = 2;
                    setStatus('block management_type changed');
                    renderTree(); renderProps();
                });
                prop.appendChild(wrap);
            }
            // instance_count -> bounded number input
            else if (type === 'block' && key === 'instance_count') {
                const num = document.createElement('input'); num.type = 'number'; num.className = 'inline-number';
                // determine allowed range based on management_type
                const mt = node.management_type;
                if (mt === ManagementTypes.Basic) { num.min = 1; num.max = 1; num.value = 1; num.disabled = true; }
                else if (mt === ManagementTypes.BackupCopy) { num.min = 2; num.max = 2; num.value = 2; num.disabled = true; }
                else { num.min = 2; num.max = 15; num.value = Math.max(2, Math.min(15, node.instance_count || 2)); num.disabled = false; }
                num.addEventListener('change', () => {
                    let nv = Number(num.value);
                    if (!Number.isFinite(nv)) nv = Number(num.defaultValue) || 1;
                    nv = Math.trunc(nv);
                    if (mt === ManagementTypes.Basic) nv = 1;
                    else if (mt === ManagementTypes.BackupCopy) nv = 2;
                    else { if (nv < 2) nv = 2; if (nv > 15) nv = 15; }
                    node.instance_count = nv; num.value = nv; setStatus('block instance_count changed');
                });
                prop.appendChild(num);
            }
            // data_recovery_strategy -> read-only select with labels
            else if (type === 'block' && key === 'data_recovery_strategy') {
                const wrap = createCustomSelect(DataRecoveryStrategyLabels, String(val), (newVal) => {
                    node.data_recovery_strategy = Number(newVal);
                    setStatus('data_recovery_strategy changed');
                });
                prop.appendChild(wrap);
            }
            else if (key !== 'multiplicity') {
                const num = document.createElement('input'); num.type = 'number'; num.className = 'inline-number'; num.value = val;
                // mark the input with the selected path and key so we can restore focus after re-render
                try { num.dataset.path = JSON.stringify(selectedPath || []); } catch (e) { num.dataset.path = '[]'; }
                num.dataset.key = key;
                // If this is a bitfield size, set min/max according to parent parameter capacity and sibling bitfields
                if (selectedType === 'bitfield' && key === 'size_in_bits' && Array.isArray(selectedPath) && selectedPath.length === 3) {
                    const paramPath = selectedPath.slice(0, 2);
                    const paramNode = getNodeAtPath(paramPath);
                    const capacity = (DataTypeSizes[(paramNode && paramNode.data_type) || 0] || 1) * 8;
                    // sum sizes of siblings excluding this bitfield
                    const idx = selectedPath[2];
                    let sumOthers = 0;
                    for (let k = 0; paramNode && paramNode.children && k < paramNode.children.length; k++) {
                        if (k === idx) continue;
                        sumOthers += Number(paramNode.children[k].size_in_bits || 0);
                    }
                    const maxAllowed = Math.max(1, capacity - sumOthers);
                    num.min = 1; num.max = maxAllowed; num.value = Math.max(1, Math.min(Number(num.value) || 1, maxAllowed));
                }
                num.addEventListener('change', () => {
                    let nv = Number(num.value);
                    if (!Number.isFinite(nv)) nv = Number(num.defaultValue) || 0;
                    nv = Math.trunc(nv);
                    if (num.hasAttribute('min')) {
                        const mn = Number(num.getAttribute('min'));
                        if (Number.isFinite(mn) && nv < mn) nv = mn;
                    }
                    if (num.hasAttribute('max')) {
                        const mx = Number(num.getAttribute('max'));
                        if (Number.isFinite(mx) && nv > mx) nv = mx;
                    }
                    node[key] = nv; num.value = nv; setStatus(`${type} ${key} changed`);
                    if (key === 'data_type') renderTree();
                    // if bitfield size changed, re-render props so sibling maxima update
                    if (selectedType === 'bitfield' && key === 'size_in_bits') {
                        // preserve caret/selection
                        const caretStart = num.selectionStart || 0;
                        const caretEnd = num.selectionEnd || caretStart;
                        renderTree(); renderProps();
                        // find recreated input and restore focus/selection
                        const pathStr = num.dataset.path;
                        const selector = `input[data-path="${pathStr}"][data-key="${key}"]`;
                        const recreated = document.querySelector(selector);
                        if (recreated) {
                            try { recreated.focus(); recreated.setSelectionRange(caretStart, caretEnd); } catch (e) { recreated.focus(); }
                        }
                    }
                }); prop.appendChild(num)
            }
        }

        else if (Array.isArray(val)) {
            // default_value handling - for parameters
            const wrapper = document.createElement('div'); wrapper.style.display = 'flex'; wrapper.style.flexDirection = 'column';
            const rows = document.createElement('div'); rows.className = 'defaults-column';

            // Track the last focused input for "Apply to all" functionality
            let lastFocusedInput = null;

            function parseNumberToken(s) {
                if (typeof s !== 'string') return s; const str = s.trim();
                // Handle NaN for float types
                if ((str.toLowerCase() === 'nan') && (node.data_type === DataTypes.float32 || node.data_type === DataTypes.float64)) {
                    return NaN;
                }
                if (str.startsWith('0x') || str.startsWith('0X')) { try { return BigInt(str) } catch (e) { return Number(str) } } if (str.includes('.') || node.data_type === DataTypes.float32 || node.data_type === DataTypes.float64) { const f = parseFloat(str); return isNaN(f) ? 0 : f } try { // prefer BigInt if large
                    const bi = BigInt(str); return bi
                } catch (e) { const n = Number(str); return isNaN(n) ? 0 : n }
            }
            function renderDefaults() {
                rows.innerHTML = '';
                for (let i = 0; i < node.default_value.length; i++) {
                    const dv = node.default_value[i];
                    // row container so we can prefix index and have the input fill remaining space
                    const row = document.createElement('div');
                    row.style.display = 'flex';
                    row.style.alignItems = 'center';
                    row.style.gap = '8px';

                    const showIndex = (Number(node.multiplicity) > 1);
                    if (showIndex) {
                        const idx = document.createElement('div');
                        idx.className = 'defaults-index';
                        idx.textContent = `[${i}]`;
                        idx.style.color = 'var(--muted)';
                        idx.style.width = '30px';
                        idx.style.flex = '0 0 30px';
                        row.appendChild(idx);
                    }

                    const inp = document.createElement('input');
                    inp.type = 'text';
                    inp.value = dv;
                    inp.style.flex = '1';
                    inp.style.maxWidth = '200px';  // Limit width to fit uint64/double
                    // filter allowed characters depending on data_type
                    function filterAllowedChars() {
                        const isFloat = (node.data_type === DataTypes.float32 || node.data_type === DataTypes.float64);
                        const old = inp.value;
                        let filtered;
                        if (isFloat) {
                            // allow digits, decimal point, exponent marker, plus/minus, and NaN
                            filtered = old.replace(/[^0-9eEnNaA+\-\.]/g, '');
                        } else {
                            // allow decimal digits, hex letters, x/X for 0x, and sign
                            filtered = old.replace(/[^0-9a-fA-FxX+\-]/g, '');
                        }
                        if (filtered !== old) {
                            // preserve approximate caret position
                            const pos = inp.selectionStart || 0;
                            // compute removed chars before caret
                            let removedBefore = 0;
                            for (let i = 0, j = 0; i < pos && j < old.length; i++, j++) {
                                if (old[j] !== filtered.charAt(i)) removedBefore++;
                            }
                            inp.value = filtered;
                            try { const newPos = Math.max(0, (pos - removedBefore)); inp.setSelectionRange(newPos, newPos); } catch (e) { }
                        }
                    }
                    function validateInputAndMark() {
                        const parsed = parseNumberToken(inp.value);
                        // determine mismatch or out-of-range using existing helpers
                        const mismatch = have_type_mismatch(parsed, node.data_type);
                        const out = is_out_of_range(parsed, node.data_type);
                        if (mismatch) {
                            // type mismatch: mark invalid but don't auto-coerce
                            node.default_value[i] = parsed;
                            inp.classList.add('invalid');
                            inp.title = 'Type mismatch for current data_type';
                        } else if (out) {
                            // out of range: clamp to allowed bounds and update display
                            const clamped = clampValueToType(parsed, node.data_type);
                            node.default_value[i] = clamped;
                            // preserve hex display mode if the input was in hex mode
                            if (inp.dataset.mode === 'hex') {
                                try {
                                    if (typeof clamped === 'bigint') inp.value = '0x' + clamped.toString(16).toUpperCase();
                                    else if (Number.isInteger(clamped) && clamped >= 0) inp.value = '0x' + (clamped >>> 0).toString(16).toUpperCase();
                                    else inp.value = String(clamped);
                                } catch (e) { inp.value = String(clamped); }
                            } else {
                                inp.value = (typeof clamped === 'bigint') ? clamped.toString() : String(clamped);
                            }
                            inp.classList.remove('invalid');
                            inp.title = 'Value clamped to allowed range';
                        } else {
                            node.default_value[i] = parsed;
                            inp.classList.remove('invalid');
                            inp.title = '';
                        }
                        validateParamDefaults(node);
                        setStatus('Default value changed');
                    }
                    inp.addEventListener('input', (e) => { filterAllowedChars(); validateInputAndMark(); });
                    inp.addEventListener('change', validateInputAndMark);
                    inp.addEventListener('focus', () => { lastFocusedInput = inp; });

                    row.appendChild(inp);
                    rows.appendChild(row);
                }
            }
            // multiplicity edits
            if (key === 'default_value') {
                const mm = document.createElement('div'); mm.className = 'prop'; const multLabel = document.createElement('label'); multLabel.textContent = 'multiplicity'; multLabel.title = FieldDocs.parameter.multiplicity || ''; mm.appendChild(multLabel); const multInput = document.createElement('input'); multInput.type = 'number'; multInput.className = 'inline-number'; multInput.value = node.multiplicity; multInput.min = 1; multInput.addEventListener('change', () => {
                    let nv = Number(multInput.value);
                    if (!Number.isFinite(nv)) nv = 1;
                    nv = Math.trunc(nv);
                    if (nv < 1) nv = 1;
                    multInput.value = nv; node.multiplicity = nv;
                    // resize default array
                    while (node.default_value.length < nv) node.default_value.push(0);
                    while (node.default_value.length > nv) node.default_value.pop();
                    renderDefaults(); setStatus('Multiplicity changed')
                }); mm.appendChild(multInput);
                container.appendChild(mm);

                // Add the wrapper with rows first
                wrapper.appendChild(rows); renderDefaults(); prop.appendChild(wrapper);

                // Create a vertical container for the buttons
                const buttonContainer = document.createElement('div');
                buttonContainer.style.display = 'flex';
                buttonContainer.style.flexDirection = 'column';
                buttonContainer.style.gap = '4px';

                // Add "Apply to all" button (only visible when multiplicity > 1)
                const applyToAllBtn = document.createElement('button');
                applyToAllBtn.className = 'hex-toggle';  // Use same style as Hex button
                applyToAllBtn.textContent = 'Apply to all';
                applyToAllBtn.title = 'Apply the currently focused input value to all other inputs';
                applyToAllBtn.style.display = node.multiplicity > 1 ? '' : 'none';
                applyToAllBtn.addEventListener('click', () => {
                    const inputs = rows.querySelectorAll('input');
                    let sourceInput = null;

                    // First, check if any input is currently focused
                    const activeInput = document.activeElement;
                    for (let i = 0; i < inputs.length; i++) {
                        if (inputs[i] === activeInput) {
                            sourceInput = inputs[i];
                            break;
                        }
                    }

                    // If no input is focused, use the last focused input
                    if (!sourceInput && lastFocusedInput) {
                        sourceInput = lastFocusedInput;
                    }

                    // If still no source input, use the first one
                    if (!sourceInput && inputs.length > 0) {
                        sourceInput = inputs[0];
                    }

                    if (sourceInput) {
                        const valueToApply = sourceInput.value;
                        const modeToApply = sourceInput.dataset.mode;

                        // Apply to all inputs
                        for (let i = 0; i < inputs.length; i++) {
                            inputs[i].value = valueToApply;
                            inputs[i].dataset.mode = modeToApply || 'dec';

                            // Parse and update the actual value in the model
                            const parsed = parseNumberToken(valueToApply);
                            node.default_value[i] = parsed;

                            // Trigger validation
                            inputs[i].dispatchEvent(new Event('change'));
                        }

                        setStatus('Applied value to all default values');
                    }
                });
                buttonContainer.appendChild(applyToAllBtn);

                // Add Hex/Dec button to the existing prop (after the wrapper, on the right side)
                const hexToggle = document.createElement('button'); hexToggle.className = 'hex-toggle'; hexToggle.textContent = 'Hex';
                // Disable for float types
                const isFloat = (node.data_type === DataTypes.float32 || node.data_type === DataTypes.float64);
                if (isFloat) {
                    hexToggle.disabled = true;
                    hexToggle.title = 'Hex/Dec toggle is only available for integer types';
                }
                hexToggle.addEventListener('click', () => { // toggle display between hex and dec for integer types
                    const inputs = rows.querySelectorAll('input');
                    const isCurrentlyHex = inputs.length > 0 && inputs[0].dataset.mode === 'hex';
                    for (let i = 0; i < inputs.length; i++) {
                        const v = Number(inputs[i].value);
                        if (!isCurrentlyHex) {
                            inputs[i].value = '0x' + (v >>> 0).toString(16).toUpperCase();
                            inputs[i].dataset.mode = 'hex';
                        } else {
                            inputs[i].value = (v >>> 0).toString(10);
                            inputs[i].dataset.mode = 'dec';
                        }
                    }
                    // Toggle the hex-mode class and text
                    if (!isCurrentlyHex) {
                        hexToggle.classList.add('hex-mode');
                        hexToggle.textContent = 'Dec';
                    } else {
                        hexToggle.classList.remove('hex-mode');
                        hexToggle.textContent = 'Hex';
                    }
                });
                buttonContainer.appendChild(hexToggle);

                // Append the button container to prop
                prop.appendChild(buttonContainer);

                // Store reference to the button so we can update its visibility when multiplicity changes
                const updateApplyToAllVisibility = () => {
                    applyToAllBtn.style.display = node.multiplicity > 1 ? '' : 'none';
                };

                // Update the multiplicity change handler to also update button visibility
                const originalMultChange = multInput.onchange;
                multInput.addEventListener('change', () => {
                    updateApplyToAllVisibility();
                });
            }
        }
        else {
            const inp = document.createElement('input'); inp.type = 'text'; inp.value = val === null ? '' : val;
            // Special handling for name normalization on Enter
            if (key === 'name') {
                inp.addEventListener('keydown', (ev) => {
                    if (ev.key === 'Enter') {
                        ev.preventDefault();
                        const norm = normalizeName(inp.value);
                        // Check for duplicate block names
                        if (type === 'block' && blockNameExists(norm, selectedPath)) {
                            alert(`Block name "${norm}" already exists. Please choose a different name.`);
                            inp.focus();
                            return;
                        }
                        inp.value = norm; node[key] = norm; setStatus(`${type} ${key} changed`);
                        renderTree(); if (selectedPath) selectNode(selectedPath, selectedType);
                    }
                });
                // also handle blur (commit)
                inp.addEventListener('blur', () => {
                    const norm = normalizeName(inp.value);
                    // Check for duplicate block names
                    if (type === 'block' && blockNameExists(norm, selectedPath)) {
                        alert(`Block name "${norm}" already exists. Please choose a different name.`);
                        inp.value = node[key]; // Revert to original name
                        inp.focus();
                        return;
                    }
                    inp.value = norm; node[key] = norm; setStatus(`${type} ${key} changed`); renderTree(); if (selectedPath) selectNode(selectedPath, selectedType);
                });
            } else {
                inp.addEventListener('change', () => {
                    node[key] = inp.value; setStatus(`${type} ${key} changed`); renderTree(); // keep the selection after re-render
                    if (selectedPath) selectNode(selectedPath, selectedType);
                });
            }
            prop.appendChild(inp)
        }
        container.appendChild(prop)
    }

    // Per-block platform settings: page_aligned and placement directives
    if (type === 'block') {
        // ensure platform settings exist
        if (!state.platformSettings) state.platformSettings = makeDefaultPlatform();
        const bsName = node.name || '';
        const paDiv = document.createElement('div'); paDiv.className = 'prop'; const paLabel = document.createElement('label'); paLabel.textContent = formatLabel('page_aligned'); paDiv.appendChild(paLabel);
        const paCb = document.createElement('input'); paCb.type = 'checkbox';
        paCb.checked = (state.platformSettings.page_aligned_blocks || []).includes(bsName) || (state.platformSettings.page_aligned_blocks || []).includes('*');
        paCb.addEventListener('change', () => {
            let list = state.platformSettings.page_aligned_blocks || [];
            const hasWildcard = list.includes('*');

            // Get all block names
            const allBlockNames = (state.dataModel && state.dataModel.children)
                ? state.dataModel.children.map(block => block.name || '').filter(name => name)
                : [];

            if (paCb.checked) {
                // Checking a block
                if (hasWildcard) {
                    // Already has wildcard, nothing to do
                } else {
                    // Add this block if not already in list
                    if (!list.includes(bsName)) {
                        list.push(bsName);
                    }
                    // Check if all blocks are now aligned
                    if (list.length === allBlockNames.length && allBlockNames.length > 0) {
                        list = ['*'];
                    }
                }
            } else {
                // Unchecking a block
                if (hasWildcard) {
                    // Expand wildcard to all blocks except this one
                    list = allBlockNames.filter(name => name !== bsName);
                } else {
                    // Remove this block from list
                    const idx = list.indexOf(bsName);
                    if (idx !== -1) {
                        list.splice(idx, 1);
                    }
                }
            }

            state.platformSettings.page_aligned_blocks = list;
            setStatus('page_aligned changed for block ' + bsName);
        }); paDiv.appendChild(paCb); container.appendChild(paDiv);

        // Placement directives: separate labeled fields for each placement directive
        const pdDiv = document.createElement('div'); pdDiv.className = 'prop'; const pdLabel = document.createElement('label'); pdLabel.textContent = formatLabel('placement_directives'); pdDiv.appendChild(pdLabel);
        const pdWrap = document.createElement('div'); pdWrap.style.display = 'flex'; pdWrap.style.flexDirection = 'column'; pdWrap.style.gap = '6px';
        const currentPlacement = (state.platformSettings.compiler_directives && state.platformSettings.compiler_directives.block_placement_directives && state.platformSettings.compiler_directives.block_placement_directives[bsName]) || {};

        function ensureBlockPlacement() {
            if (!state.platformSettings.compiler_directives) state.platformSettings.compiler_directives = { opening_pack_directive: null, closing_pack_directive: null, pack_attribute: null, block_placement_directives: {} };
            if (!state.platformSettings.compiler_directives.block_placement_directives) state.platformSettings.compiler_directives.block_placement_directives = {};
            if (!state.platformSettings.compiler_directives.block_placement_directives[bsName]) state.platformSettings.compiler_directives.block_placement_directives[bsName] = {};
            return state.platformSettings.compiler_directives.block_placement_directives[bsName];
        }

        // directive_for_defaults
        const dfDiv = document.createElement('div'); dfDiv.style.display = 'flex'; dfDiv.style.gap = '8px'; dfDiv.style.alignItems = 'center'; const dfLabel = document.createElement('div'); dfLabel.style.width = '180px'; dfLabel.style.flexShrink = '0'; dfLabel.textContent = formatLabel('directive_for_defaults'); dfLabel.title = FieldDocs.platform.directive_for_defaults || ''; dfDiv.appendChild(dfLabel);
        const dirInp = document.createElement('input'); dirInp.type = 'text'; dirInp.style.flex = '1'; dirInp.value = currentPlacement.directive_for_defaults || '';
        dirInp.addEventListener('change', () => { const target = ensureBlockPlacement(); target.directive_for_defaults = dirInp.value || null; setStatus('directive_for_defaults changed for block ' + bsName); }); dfDiv.appendChild(dirInp); pdWrap.appendChild(dfDiv);

        // attribute_for_defaults
        const adDiv = document.createElement('div'); adDiv.style.display = 'flex'; adDiv.style.gap = '8px'; adDiv.style.alignItems = 'center'; const adLabel = document.createElement('div'); adLabel.style.width = '180px'; adLabel.style.flexShrink = '0'; adLabel.textContent = formatLabel('attribute_for_defaults'); adLabel.title = FieldDocs.platform.attribute_for_defaults || ''; adDiv.appendChild(adLabel);
        const attrInp = document.createElement('input'); attrInp.type = 'text'; attrInp.style.flex = '1'; attrInp.value = currentPlacement.attribute_for_defaults || '';
        attrInp.addEventListener('change', () => { const target = ensureBlockPlacement(); target.attribute_for_defaults = attrInp.value || null; setStatus('attribute_for_defaults changed for block ' + bsName); }); adDiv.appendChild(attrInp); pdWrap.appendChild(adDiv);

        // directive_for_cache
        const dcDiv = document.createElement('div'); dcDiv.style.display = 'flex'; dcDiv.style.gap = '8px'; dcDiv.style.alignItems = 'center'; const dcLabel = document.createElement('div'); dcLabel.style.width = '180px'; dcLabel.style.flexShrink = '0'; dcLabel.textContent = formatLabel('directive_for_cache'); dcLabel.title = FieldDocs.platform.directive_for_cache || ''; dcDiv.appendChild(dcLabel);
        const cacheInp = document.createElement('input'); cacheInp.type = 'text'; cacheInp.style.flex = '1'; cacheInp.value = currentPlacement.directive_for_cache || '';
        cacheInp.addEventListener('change', () => { const target = ensureBlockPlacement(); target.directive_for_cache = cacheInp.value || null; setStatus('directive_for_cache changed for block ' + bsName); }); dcDiv.appendChild(cacheInp); pdWrap.appendChild(dcDiv);

        // attribute_for_cache
        const acDiv = document.createElement('div'); acDiv.style.display = 'flex'; acDiv.style.gap = '8px'; acDiv.style.alignItems = 'center'; const acLabel = document.createElement('div'); acLabel.style.width = '180px'; acLabel.style.flexShrink = '0'; acLabel.textContent = formatLabel('attribute_for_cache'); acLabel.title = FieldDocs.platform.attribute_for_cache || ''; acDiv.appendChild(acLabel);
        const cacheAttr = document.createElement('input'); cacheAttr.type = 'text'; cacheAttr.style.flex = '1'; cacheAttr.value = currentPlacement.attribute_for_cache || '';
        cacheAttr.addEventListener('change', () => { const target = ensureBlockPlacement(); target.attribute_for_cache = cacheAttr.value || null; setStatus('attribute_for_cache changed for block ' + bsName); }); acDiv.appendChild(cacheAttr); pdWrap.appendChild(acDiv);

        pdDiv.appendChild(pdWrap);
        container.appendChild(pdDiv);
    }
}

function validateParamDefaults(param) { // check types
    if (param.default_value.length !== param.multiplicity) { setStatus('Default values length mismatch'); }
    const errs = [];
    for (const raw of param.default_value) {
        let v = raw; // convert BigInt to Number when needed
        if (typeof v === 'bigint') { // for checks other than float we accept bigint
        }
        const dtSize = DataTypeSizes[param.data_type];
        if (dtSize === 1) { // int8/uint8 range approx
            // allow BigInt or Number
            const n = (typeof v === 'bigint') ? v : BigInt(Math.trunc(Number(v) || 0)); if (n < BigInt(-128) || n > BigInt(255)) errs.push('out of range')
        }
    }
    if (errs.length) alert('Validation warning: ' + errs.join(','))
}

// platform rendering
function renderPlatformSettings() {
    const grid = document.getElementById('platformGrid');
    const hint = document.getElementById('platformHint');

    grid.innerHTML = '';

    if (!state.platformSettings) {
        hint.style.display = 'block';
        grid.style.display = 'none';
        return;
    }

    hint.style.display = 'none';
    grid.style.display = 'grid';

    const ps = state.platformSettings;

    // Ensure compiler_directives exists
    if (!ps.compiler_directives) {
        ps.compiler_directives = {
            opening_pack_directive: null,
            closing_pack_directive: null,
            pack_attribute: null,
            block_placement_directives: {}
        };
    }

    for (const key of ['endianness', 'eeprom_size', 'eeprom_page_size', 'external_headers', 'enter_critical_section_operation', 'exit_critical_section_operation', 'opening_pack_directive', 'closing_pack_directive', 'pack_attribute']) {
        const label = document.createElement('div');

        // Handle compiler directive fields specially
        const isCompilerDirective = ['opening_pack_directive', 'closing_pack_directive', 'pack_attribute'].includes(key);
        label.textContent = formatLabel(key);

        // Add tooltip from FieldDocs
        if (FieldDocs.platform && FieldDocs.platform[key]) {
            label.title = FieldDocs.platform[key];
        }
        const valWrap = document.createElement('div');
        if (key === 'endianness') {
            const endOptions = { 'little': 'little', 'big': 'big' };
            const wrap = createCustomSelect(endOptions, ps.endianness || 'little', (newVal) => {
                ps.endianness = newVal;
                setStatus('Platform endianness changed');
            });
            valWrap.appendChild(wrap);
        }
        else if (Array.isArray(ps[key])) { const t = document.createElement('textarea'); t.style.width = '100%'; t.style.height = '80px'; t.value = ps[key].join('\n'); t.addEventListener('change', () => { ps[key] = t.value.split(/\r?\n/).filter(r => r.trim()); setStatus(key + ' changed') }); valWrap.appendChild(t) }
        else {
            if (key === 'eeprom_size') {
                const inp = document.createElement('input'); inp.type = 'number'; inp.min = 64; inp.max = 65536; inp.value = Number(ps[key] || 256);
                inp.addEventListener('change', () => {
                    let nv = Number(inp.value);
                    if (!Number.isFinite(nv)) nv = 64;
                    nv = Math.trunc(nv);
                    if (nv < 64) nv = 64;
                    if (nv > 65536) nv = 65536;
                    inp.value = nv;
                    ps[key] = nv; setStatus(key + ' changed');
                });
                valWrap.appendChild(inp);
            }
            else if (key === 'eeprom_page_size') {
                // Build options: 0, 2,4,8,...,32768
                const pageOptions = { '0': '0' };
                for (let p = 2; p <= 32768; p *= 2) pageOptions[String(p)] = String(p);
                const currentVal = Number(ps[key] || 0);
                const wrap = createCustomSelect(pageOptions, String(currentVal), (newVal) => {
                    ps[key] = Number(newVal);
                    setStatus(key + ' changed');
                });
                valWrap.appendChild(wrap);
            }
            else {
                const inp = document.createElement('input');
                inp.type = 'text';

                // Get value from compiler_directives for compiler directive fields
                if (isCompilerDirective) {
                    inp.value = ps.compiler_directives[key] || '';
                    inp.addEventListener('change', () => {
                        ps.compiler_directives[key] = inp.value || null;
                        setStatus(key + ' changed');
                    });
                } else {
                    inp.value = ps[key] || '';
                    inp.addEventListener('change', () => {
                        ps[key] = inp.value;
                        setStatus(key + ' changed');
                    });
                }

                valWrap.appendChild(inp);
            }
        }
        const d1 = document.createElement('div'); d1.appendChild(label); grid.appendChild(d1); const d2 = document.createElement('div'); d2.appendChild(valWrap); grid.appendChild(d2);
    }
}

// checksum rendering
function renderChecksum() {
    const grid = document.getElementById('checksumGrid');
    const hint = document.getElementById('checksumHint');

    grid.innerHTML = '';

    if (!state.checksumSettings) {
        hint.style.display = 'block';
        grid.style.display = 'none';
        return;
    }

    hint.style.display = 'none';
    grid.style.display = 'grid';

    const cs = state.checksumSettings;

    // algo field is always required and rendered first
    const algoLabel = document.createElement('div'); algoLabel.textContent = 'algo';
    const algoVal = document.createElement('div');
    const algoInp = document.createElement('input');
    algoInp.type = 'text';
    algoInp.value = cs.algo || 'crc';
    algoInp.addEventListener('change', () => {
        cs.algo = algoInp.value;
        setStatus('algo changed');
    });
    algoVal.appendChild(algoInp);
    grid.appendChild(algoLabel); grid.appendChild(algoVal);

    // Render all other fields dynamically
    for (const key in cs) {
        if (key === 'algo') continue; // already handled

        const label = document.createElement('div');
        label.textContent = key.replace(/_/g, ' ');
        label.dataset.paramKey = key; // Store the actual key for deletion
        label.style.cursor = 'context-menu';
        label.title = 'Right-click to delete';

        const valueDiv = document.createElement('div');
        const value = cs[key];

        // Handle different value types
        if (typeof value === 'boolean') {
            // Boolean field - use checkbox
            const cb = document.createElement('input');
            cb.type = 'checkbox';
            cb.checked = value;
            cb.addEventListener('change', () => {
                cs[key] = cb.checked;
                setStatus(`${key} changed`);
            });
            valueDiv.appendChild(cb);
        } else if (typeof value === 'number') {
            // Number field - check type hint first, then if it's an integer or float
            const inp = document.createElement('input');
            inp.type = 'text';

            // Check if we have a type hint for this parameter
            const typeHint = state.checksumTypeHints[key];
            const isFloat = typeHint === 'float' || (!Number.isInteger(value) && typeHint !== 'number');

            if (isFloat) {
                // Float - show as decimal
                inp.value = value.toString();
                inp.addEventListener('change', () => {
                    const parsed = parseFloat(inp.value);
                    if (isNaN(parsed)) {
                        alert('Invalid floating-point value');
                        inp.value = cs[key].toString();
                    } else {
                        cs[key] = parsed;
                        setStatus(`${key} changed`);
                    }
                });
            } else {
                // Integer - show as hex
                inp.value = '0x' + value.toString(16).toUpperCase();
                inp.addEventListener('change', () => {
                    try {
                        const parsed = parseInt(inp.value, inp.value.startsWith('0x') ? 16 : 10);
                        cs[key] = parsed;
                        inp.value = '0x' + cs[key].toString(16).toUpperCase();
                        setStatus(`${key} changed`);
                    } catch (e) {
                        alert('Invalid number value');
                        inp.value = '0x' + cs[key].toString(16).toUpperCase();
                    }
                });
            }
            valueDiv.appendChild(inp);
        } else {
            // String or other type - use text input
            const inp = document.createElement('input');
            inp.type = 'text';
            inp.value = String(value);
            inp.addEventListener('change', () => {
                cs[key] = inp.value;
                setStatus(`${key} changed`);
            });
            valueDiv.appendChild(inp);
        }

        grid.appendChild(label);
        grid.appendChild(valueDiv);
    }
}

// Context menu for checksum parameters
const checksumGrid = document.getElementById('checksumGrid');
checksumGrid.addEventListener('contextmenu', (e) => {
    e.preventDefault();
    const contextMenu = document.getElementById('contextMenu');
    contextMenu.innerHTML = '';
    contextMenu.style.left = e.clientX + 'px';
    contextMenu.style.top = e.clientY + 'px';
    contextMenu.style.display = 'block';

    // Check if clicked on a parameter label
    const label = e.target.closest('[data-param-key]');
    const paramKey = label ? label.dataset.paramKey : null;

    // Add new parameter option (always available)
    const addBtn = createContextButton('➕ Add new parameter', () => {
        contextMenu.style.display = 'none';
        showAddParameterDialog();
    });
    contextMenu.appendChild(addBtn);

    // Delete parameter option (only if a parameter label was clicked)
    if (paramKey) {
        const delBtn = createContextButton('🗑️ Delete parameter', () => {
            contextMenu.style.display = 'none';
            if (confirm(`Delete parameter "${paramKey}"?`)) {
                delete state.checksumSettings[paramKey];
                delete state.checksumTypeHints[paramKey]; // Also remove type hint
                renderChecksum();
                setStatus(`Deleted parameter: ${paramKey}`);
            }
        });
        contextMenu.appendChild(delBtn);
    }
});

// Platform settings hint context menu
const platformHint = document.getElementById('platformHint');
platformHint.addEventListener('contextmenu', (e) => {
    e.preventDefault();
    const contextMenu = document.getElementById('contextMenu');
    contextMenu.innerHTML = '';
    contextMenu.style.left = e.clientX + 'px';
    contextMenu.style.top = e.clientY + 'px';
    contextMenu.style.display = 'block';

    const createBtn = createContextButton('📦 Create new', () => {
        contextMenu.style.display = 'none';
        state.platformSettings = makeDefaultPlatform();
        renderPlatformSettings();
        setStatus('Created new platform settings');
    });
    contextMenu.appendChild(createBtn);
});

// Checksum parameters hint context menu
const checksumHint = document.getElementById('checksumHint');
checksumHint.addEventListener('contextmenu', (e) => {
    e.preventDefault();
    const contextMenu = document.getElementById('contextMenu');
    contextMenu.innerHTML = '';
    contextMenu.style.left = e.clientX + 'px';
    contextMenu.style.top = e.clientY + 'px';
    contextMenu.style.display = 'block';

    const createBtn = createContextButton('📦 Create new', () => {
        contextMenu.style.display = 'none';
        state.checksumSettings = makeDefaultChecksum();
        renderChecksum();
        setStatus('Created new checksum parameters');
    });
    contextMenu.appendChild(createBtn);
});

function showAddParameterDialog() {
    const { overlay, dialog } = createModal('Add New Parameter');

    // Name input
    const { label: nameLabel, input: nameInput } = createDialogInput('Parameter name:', 'e.g., polynomial');
    dialog.appendChild(nameLabel);
    dialog.appendChild(nameInput);

    // Type dropdown
    const { label: typeLabel, select: typeSelect } = createDialogSelect('Parameter type:', ['string', 'number', 'float', 'boolean']);
    dialog.appendChild(typeLabel);
    dialog.appendChild(typeSelect);

    // Handle add action
    const handleAdd = () => {
        const name = nameInput.value.trim();
        if (!name) {
            alert('Please enter a parameter name');
            return;
        }

        // Validate C identifier
        const cIdentifierPattern = /^[a-zA-Z_][a-zA-Z0-9_]*$/;
        if (!cIdentifierPattern.test(name)) {
            alert('Parameter name must be a valid C identifier:\n- Start with a letter or underscore\n- Contain only letters, digits, and underscores');
            return;
        }

        // Check if parameter already exists
        if (name in state.checksumSettings) {
            alert(`Parameter "${name}" already exists!`);
            return;
        }

        const type = typeSelect.value;
        let defaultValue;
        switch (type) {
            case 'string':
                defaultValue = '';
                break;
            case 'number':
                defaultValue = 0;
                break;
            case 'float':
                defaultValue = 0.0;
                break;
            case 'boolean':
                defaultValue = false;
                break;
        }

        // Add the new parameter
        state.checksumSettings[name] = defaultValue;
        // Store type hint to distinguish float from integer
        if (type === 'float' || type === 'number') {
            state.checksumTypeHints[name] = type;
        }
        renderChecksum();
        setStatus(`Added parameter: ${name}`);
        document.body.removeChild(overlay);
    };

    // Buttons
    const buttons = createDialogButtons(
        () => document.body.removeChild(overlay),
        handleAdd,
        'Add'
    );
    dialog.appendChild(buttons);

    document.body.appendChild(overlay);

    // Focus name input and handle keyboard
    nameInput.focus();
    const handleKey = (e) => {
        if (e.key === 'Enter') handleAdd();
        if (e.key === 'Escape') document.body.removeChild(overlay);
    };
    nameInput.addEventListener('keydown', handleKey);
    typeSelect.addEventListener('keydown', handleKey);
}

// resizing
const resizer = document.getElementById('resizer'); let isResizing = false; resizer.addEventListener('mousedown', (e) => { isResizing = true; document.body.style.cursor = 'col-resize' }); window.addEventListener('mousemove', (e) => { if (!isResizing) return; const left = document.getElementById('leftPanel'); const rect = document.getElementById('dataArea').getBoundingClientRect(); let newW = e.clientX - rect.left - 8; if (newW < 200) newW = 200; if (newW > 600) newW = 600; left.style.width = newW + 'px'; }); window.addEventListener('mouseup', () => { isResizing = false; document.body.style.cursor = 'default' });

// tabs
document.getElementById('tabData').addEventListener('click', () => {
    document.getElementById('dataArea').style.display = 'flex';
    document.getElementById('platformArea').style.display = 'none';
    document.getElementById('checksumArea').style.display = 'none';
    document.getElementById('tabData').classList.add('active');
    document.getElementById('tabPlatform').classList.remove('active');
    document.getElementById('tabChecksum').classList.remove('active');
})
document.getElementById('tabPlatform').addEventListener('click', () => {
    document.getElementById('platformArea').style.display = 'flex';
    document.getElementById('dataArea').style.display = 'none';
    document.getElementById('checksumArea').style.display = 'none';
    document.getElementById('tabPlatform').classList.add('active');
    document.getElementById('tabData').classList.remove('active');
    document.getElementById('tabChecksum').classList.remove('active');
    renderPlatformSettings();
})
document.getElementById('tabChecksum').addEventListener('click', () => {
    document.getElementById('checksumArea').style.display = 'flex';
    document.getElementById('dataArea').style.display = 'none';
    document.getElementById('platformArea').style.display = 'none';
    document.getElementById('tabChecksum').classList.add('active');
    document.getElementById('tabData').classList.remove('active');
    document.getElementById('tabPlatform').classList.remove('active');
    renderChecksum();
})

// keyboard shortcuts
window.addEventListener('keydown', (e) => {
    if (e.ctrlKey && e.key === 'o') {
        e.preventDefault();
        // Open based on active tab
        if (document.getElementById('tabPlatform').classList.contains('active')) {
            openPlatformInput.click();
        } else {
            openDataModelInput.click();
        }
    }
    if (e.ctrlKey && e.key === 's' && e.shiftKey) { e.preventDefault(); saveAs(); }
    else if (e.ctrlKey && e.key === 's') {
        e.preventDefault();
        // Save based on active tab
        if (document.getElementById('tabPlatform').classList.contains('active')) {
            savePlatformSettings();
        } else {
            if (state.dataModel) saveDataModel();
        }
    }
    if (e.key === 'Delete' && selectedPath) { deleteNodeAtPath(selectedPath); renderTree(); }
    // Move node up/down with Shift+Arrow keys
    if (e.shiftKey && e.key === 'ArrowUp' && selectedPath) {
        e.preventDefault();
        moveNodeUp(selectedPath);
    }
    if (e.shiftKey && e.key === 'ArrowDown' && selectedPath) {
        e.preventDefault();
        moveNodeDown(selectedPath);
    }
    // Close file menu when Escape is pressed
    if (e.key === 'Escape') {
        const fm = document.getElementById('fileMenu'); if (fm && fm.style.display === 'block') { fm.style.display = 'none'; }
    }
})

// initial
renderTree(); renderPlatformSettings(); renderChecksum(); setStatus('Ready')

// Close open custom-select menus on global click
window.addEventListener('click', () => {
    document.querySelectorAll('.custom-select .menu').forEach(m => { m.style.display = 'none' });
});