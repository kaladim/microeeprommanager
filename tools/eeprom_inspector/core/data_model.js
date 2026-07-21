/*
 * mEEM EEPROM Inspector - data model & platform settings.
 *
 * Data type / management enums, size helpers, JSON parsing of the datamodel and
 * platform settings, and the EEPROM-layout computation (attach_block_metadata).
 * Faithful port of common/data_model.py, common/platform_settings.py and the
 * relevant parts of common/utils.py.
 *
 * Part of the split `core/` business logic. Classic (non-module) script that
 * contributes to the shared global `EEPROM` namespace. No dependencies on the
 * other core modules.
 */
(function (EEPROM) {
    'use strict';

    // ---- Enums (mirror common/data_model.py) ------------------------------

    const DataType = {
        uint8: 0, int8: 1, uint16: 2, int16: 3,
        uint32: 4, int32: 5, uint64: 6, int64: 7,
        float32: 8, float64: 9,
    };

    const DataTypeName = {
        0: 'uint8', 1: 'int8', 2: 'uint16', 3: 'int16',
        4: 'uint32', 5: 'int32', 6: 'uint64', 7: 'int64',
        8: 'float32', 9: 'float64',
    };

    function dataTypeSize(dt) {
        switch (dt) {
            case DataType.uint8:
            case DataType.int8:
                return 1;
            case DataType.uint16:
            case DataType.int16:
                return 2;
            case DataType.uint32:
            case DataType.int32:
            case DataType.float32:
                return 4;
            default:
                return 8;
        }
    }

    const ManagementType = {
        Basic: 0, BackupCopy: 1, MultiProfile: 2, WearLeveling: 3,
    };

    const ManagementTypeName = {
        0: 'Basic', 1: 'Backup copy', 2: 'Multi-profile', 3: 'Wear-leveling',
    };

    // ---- Size helpers (mirror Parameter.size / Block.data_size) ------------

    function parameterSize(param) {
        return param.multiplicity * dataTypeSize(param.data_type);
    }

    function blockDataSize(block) {
        /* Aggregate size of all parameters in the block, in bytes.
         * For wear-leveling blocks the size is +1, because of the extra
         * sequence counter. */
        let sum = 0;
        for (const p of block.children) sum += parameterSize(p);
        return sum + (block.management_type === ManagementType.WearLeveling ? 1 : 0);
    }

    // ---- Parsing ----------------------------------------------------------

    /**
     * Parse a datamodel.json string into a plain object.
     * The Python loader reconstructs typed objects, but the report only needs
     * the plain nested structure plus the enum integer fields (which JSON
     * already stores as integers).
     */
    function parseDataModel(text) {
        const dm = JSON.parse(text);
        if (typeof dm.checksum_size !== 'number') {
            throw new Error("datamodel: missing or invalid 'checksum_size'");
        }
        if (!Array.isArray(dm.children)) {
            throw new Error("datamodel: missing 'children' (blocks) array");
        }
        return dm;
    }

    function parsePlatformSettings(text) {
        const s = JSON.parse(text);
        if (s.endianness !== 'little' && s.endianness !== 'big') {
            s.endianness = 'little';
        }
        if (typeof s.eeprom_page_size !== 'number') s.eeprom_page_size = 0;
        if (!Array.isArray(s.page_aligned_blocks)) s.page_aligned_blocks = ['*'];
        return s;
    }

    // ---- EEPROM layout (mirror common/utils.py attach_block_metadata) ------
    //
    // Only offset_in_eeprom / size_in_eeprom are needed for the report, so the
    // default-pattern deduction (used solely by the code generator) is omitted.

    function attachBlockMetadata(datamodel, settings) {
        const alignAll = settings.page_aligned_blocks.indexOf('*') !== -1;
        let offset = 0;

        for (let index = 0; index < datamodel.children.length; index++) {
            const block = datamodel.children[index];
            block.offset_in_eeprom = offset;
            block.data_size = blockDataSize(block);
            block.size_in_eeprom = (datamodel.checksum_size + block.data_size) * block.instance_count;

            offset += block.size_in_eeprom;

            if (
                settings.eeprom_page_size > 0 &&
                (offset % settings.eeprom_page_size) !== 0 &&
                index < datamodel.children.length - 1 &&
                (settings.page_aligned_blocks.indexOf(datamodel.children[index + 1].name) !== -1 || alignAll)
            ) {
                // Align to page boundary
                offset |= settings.eeprom_page_size - 1;
                offset += 1;
            }
        }
    }

    EEPROM.DataType = DataType;
    EEPROM.DataTypeName = DataTypeName;
    EEPROM.dataTypeSize = dataTypeSize;
    EEPROM.ManagementType = ManagementType;
    EEPROM.ManagementTypeName = ManagementTypeName;
    EEPROM.parameterSize = parameterSize;
    EEPROM.blockDataSize = blockDataSize;
    EEPROM.parseDataModel = parseDataModel;
    EEPROM.parsePlatformSettings = parsePlatformSettings;
    EEPROM.attachBlockMetadata = attachBlockMetadata;

})(globalThis.EEPROM = globalThis.EEPROM || {});
