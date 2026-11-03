/*
 * mEEM EEPROM Inspector - config loader.
 *
 * JSON parsing of the datamodel and platform settings, and the EEPROM-layout
 * metadata of the blocks (attach_block_metadata). The enums, sizes and offsets
 * themselves are shared with the configurator, in common/js/data-model.js.
 *
 * Part of the split `core/` business logic. Classic (non-module) script that
 * contributes to the shared global `EEPROM` namespace. Depends on
 * common/js/data-model.js (loaded earlier).
 */
(function (EEPROM) {
    'use strict';

    const blockDataSize = MeemCommon.blockDataSize;
    const blockSizeInEeprom = MeemCommon.blockSizeInEeprom;
    const blockOffsets = MeemCommon.blockOffsets;

    // ---- Parsing ----------------------------------------------------------

    /**
     * Parse a datamodel.json string into a plain object.
     * The Python loader reconstructs typed objects, but the report only needs
     * the plain nested structure plus the enum integer fields (which JSON
     * already stores as integers).
     * A 'checksum_size' is ignored, as the Python loader ignores that of a
     * legacy model, since the inspector also reads images of released models,
     * which v1's configurator saved with it.
     */
    function parseDataModel(text) {
        const dm = JSON.parse(text);
        delete dm.checksum_size;
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

    // ---- EEPROM layout (mirror common/py/utils.py attach_block_metadata) ---
    //
    // Only offset_in_eeprom / size_in_eeprom are needed for the report, so the
    // default-pattern deduction (used solely by the code generator) is omitted.

    function attachBlockMetadata(datamodel, settings) {
        const offsets = blockOffsets(datamodel, settings);

        datamodel.children.forEach((block, index) => {
            block.offset_in_eeprom = offsets[index];
            block.data_size = blockDataSize(block);
            block.size_in_eeprom = blockSizeInEeprom(block);
        });
    }

    EEPROM.parseDataModel = parseDataModel;
    EEPROM.parsePlatformSettings = parsePlatformSettings;
    EEPROM.attachBlockMetadata = attachBlockMetadata;

})(globalThis.EEPROM = globalThis.EEPROM || {});
