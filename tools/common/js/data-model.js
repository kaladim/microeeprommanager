/*
 * mEEM web tools - data model.
 *
 * What the web tools share of the data model: the enums, the sizes of the EEPROM
 * format and the offsets of the blocks. Port of
 * common/py/data_model.py and the relevant parts of common/py/utils.py.
 *
 * The blocks are those of a parsed datamodel.json. A missing attribute takes its
 * default from common/py/data_model.py, as the Python loader does. Integers can
 * also be BigInts or text, as the configurator keeps them.
 *
 * Classic (non-module) script, so it works from file://, that contributes to
 * the global `MeemCommon` namespace. No dependencies.
 */
(function (MeemCommon) {
    'use strict';

    // ---- Enums (mirror common/py/data_model.py) ----------------------------

    const DataTypes = {
        uint8: 0, int8: 1, uint16: 2, int16: 3,
        uint32: 4, int32: 5, uint64: 6, int64: 7,
        float32: 8, float64: 9,
    };

    const ManagementTypes = { Basic: 0, BackupCopy: 1, MultiProfile: 2, WearLeveling: 3 };

    // The enums' values count up from 0, so these are indexed by value. The names are those of the Python enums, which the descriptors use.
    const DataTypeNames = Object.keys(DataTypes);
    const DataTypeSizes = [1, 1, 2, 2, 4, 4, 8, 8, 4, 8];
    const ManagementTypeNames = Object.keys(ManagementTypes);

    // What the web tools show the user
    const ManagementTypeLabels = {
        [ManagementTypes.Basic]: 'Basic',
        [ManagementTypes.BackupCopy]: 'Backup copy',
        [ManagementTypes.MultiProfile]: 'Multi-profile',
        [ManagementTypes.WearLeveling]: 'Wear-leveling',
    };

    // An enum's entry for the value, as Python's IntEnum(value) finds it
    function lookup(table, value, what) {
        const entry = table[Number(value ?? NaN)];
        if (entry === undefined) throw new Error('Unknown ' + what + ' ' + value);
        return entry;
    }

    function dataTypeName(dataType) { return lookup(DataTypeNames, dataType, 'data type'); }
    function dataTypeSize(dataType) { return lookup(DataTypeSizes, dataType, 'data type'); }
    function managementTypeName(managementType) { return lookup(ManagementTypeNames, managementType, 'management type'); }

    // ---- EEPROM format -----------------------------------------------------

    // Size of the identity header at EEPROM offset 0, in bytes. The blocks follow it.
    const IDENTITY_HEADER_SIZE = 6;

    // Size of the instance checksum in bytes: CRC-16/IBM-3740, the same for every data model
    const CHECKSUM_SIZE = 2;

    // ---- Sizes (mirror Parameter.size / Block.data_size) -------------------

    function multiplicityOf(param) { return Number(param.multiplicity ?? 1); }
    function instanceCountOf(block) { return Number(block.instance_count ?? 1); }

    function parameterSize(param) {
        return multiplicityOf(param) * dataTypeSize(param.data_type);
    }

    /** Aggregate size of all parameters in the block, in bytes. A wear-leveling block also holds its sequence counter. */
    function blockDataSize(block) {
        let sum = Number(block.management_type) === ManagementTypes.WearLeveling ? 1 : 0;
        for (const param of block.children ?? []) sum += parameterSize(param);
        return sum;
    }

    /** Size of all instances of the block, in bytes */
    function blockSizeInEeprom(block) {
        return (CHECKSUM_SIZE + blockDataSize(block)) * instanceCountOf(block);
    }

    // ---- EEPROM layout (mirror common/py/utils.py attach_block_metadata) ---

    /**
     * The offset of each block: behind the identity header, in definition order.
     * A page-aligned block starts at the next page boundary, so a page-aligned
     * first block starts at the first page boundary after the header. The
     * settings' attributes default as in common/py/platform_settings.py.
     */
    function blockOffsets(datamodel, settings) {
        const pageSize = Number(settings.eeprom_page_size ?? 0);
        const pageAlignedBlocks = settings.page_aligned_blocks ?? ['*'];
        const alignAll = pageAlignedBlocks.includes('*');
        const offsets = [];
        let offset = IDENTITY_HEADER_SIZE;

        for (const block of datamodel.children ?? []) {
            if (pageSize > 0 && (offset % pageSize) !== 0 && (alignAll || pageAlignedBlocks.includes(block.name))) {
                offset = (offset | (pageSize - 1)) + 1;
            }
            offsets.push(offset);
            offset += blockSizeInEeprom(block);
        }
        return offsets;
    }

    MeemCommon.DataTypes = DataTypes;
    MeemCommon.DataTypeNames = DataTypeNames;
    MeemCommon.DataTypeSizes = DataTypeSizes;
    MeemCommon.ManagementTypes = ManagementTypes;
    MeemCommon.ManagementTypeLabels = ManagementTypeLabels;
    MeemCommon.dataTypeName = dataTypeName;
    MeemCommon.dataTypeSize = dataTypeSize;
    MeemCommon.managementTypeName = managementTypeName;
    MeemCommon.IDENTITY_HEADER_SIZE = IDENTITY_HEADER_SIZE;
    MeemCommon.CHECKSUM_SIZE = CHECKSUM_SIZE;
    MeemCommon.multiplicityOf = multiplicityOf;
    MeemCommon.instanceCountOf = instanceCountOf;
    MeemCommon.parameterSize = parameterSize;
    MeemCommon.blockDataSize = blockDataSize;
    MeemCommon.blockSizeInEeprom = blockSizeInEeprom;
    MeemCommon.blockOffsets = blockOffsets;

})(globalThis.MeemCommon = globalThis.MeemCommon || {});
