/*
 * mEEM EEPROM Inspector - report / view-model builder + orchestration.
 *
 * Reads the EEPROM image according to the data model and produces the view
 * model consumed by the UI: the image's identity header against the model's,
 * and the blocks with their identities, parameters, per-instance values,
 * checksum validity and wear-leveling "most recent" tags.
 * Port of v1's report_builder.py / view_types.py and the eeprom_inspector.py
 * main flow, on the EEPROM format of mEEM v2.
 *
 * Part of the split `core/` business logic. Classic (non-module) script that
 * contributes to the shared global `EEPROM` namespace. Depends on
 * config-loader.js and wear-leveling.js, and on data-model.js, crc.js and
 * identity.js of common/js (all loaded earlier).
 */
(function (EEPROM) {
    'use strict';

    const DataTypes = MeemCommon.DataTypes;
    const DataTypeNames = MeemCommon.DataTypeNames;
    const ManagementTypes = MeemCommon.ManagementTypes;
    const ManagementTypeLabels = MeemCommon.ManagementTypeLabels;
    const IDENTITY_HEADER_SIZE = MeemCommon.IDENTITY_HEADER_SIZE;
    const CHECKSUM_SIZE = MeemCommon.CHECKSUM_SIZE;
    const dataTypeSize = MeemCommon.dataTypeSize;
    const parameterSize = MeemCommon.parameterSize;
    const crc16 = MeemCommon.crc16;
    const dataIdentity = MeemCommon.dataIdentity;
    const blockIdentity = MeemCommon.blockIdentity;
    const eepromLayoutIdentity = MeemCommon.eepromLayoutIdentity;
    const identityHeader = MeemCommon.identityHeader;
    const formatHex = MeemCommon.formatHex;
    const formatBytes = MeemCommon.formatBytes;
    const attachBlockMetadata = EEPROM.attachBlockMetadata;
    const findIndexOfMostRecentSequenceCounter = EEPROM.findIndexOfMostRecentSequenceCounter;

    // ---- Value formatting helpers (mirror report_builder.py) --------------

    function toHexByte(b) {
        return b.toString(16).toUpperCase().padStart(2, '0');
    }

    function formatAddress(offset) {
        return formatHex(offset, 4);
    }

    // ---- Identity header --------------------------------------------------

    // How the image's identity header relates to that of the data model
    const HeaderStatus = {
        match: 'match',     // the header of the data model
        other: 'other',     // intact (its CRC-16 is valid), but of another EEPROM layout
        garbage: 'garbage', // fails its CRC-16: corrupted, torn, blank, zeroed, or no v2 image at all
    };

    /**
     * Compares the image's identity header with that of the data model. Like
     * the firmware, it compares all 6 bytes. A header that isn't the model's is
     * told apart by its own check, which covers only the header.
     */
    function inspectIdentityHeader(datamodel, settings, eeprom) {
        const layoutIdentity = eepromLayoutIdentity(datamodel, settings);
        const expected = identityHeader(layoutIdentity);
        const stored = eeprom.subarray(0, IDENTITY_HEADER_SIZE);
        const view = new DataView(stored.buffer, stored.byteOffset, stored.byteLength);

        // Both fields are little-endian on every target
        const storedCrc = view.getUint16(0, true);
        const calculatedCrc = crc16(stored.subarray(2));

        let status = HeaderStatus.garbage;
        if (stored.every((b, i) => b === expected[i])) status = HeaderStatus.match;
        else if (storedCrc === calculatedCrc) status = HeaderStatus.other;

        return {
            status: status,
            stored: formatBytes(stored),
            stored_crc: formatHex(storedCrc, 4),
            calculated_crc: formatHex(calculatedCrc, 4),
            stored_layout_identity: formatHex(view.getUint32(2, true), 8),
            expected: formatBytes(expected),
            layout_identity: formatHex(layoutIdentity, 8),
        };
    }

    // ---- Blocks -----------------------------------------------------------

    function ReportBuilder(datamodel, settings, eeprom) {
        this.datamodel = datamodel;
        this.settings = settings;
        this.eeprom = eeprom; // Uint8Array
        this.view = new DataView(eeprom.buffer, eeprom.byteOffset, eeprom.byteLength);
        this.littleEndian = settings.endianness !== 'big';
    }

    ReportBuilder.prototype.getRawValue = function (offset, dt) {
        const size = dataTypeSize(dt);
        return this.eeprom.subarray(offset, offset + size);
    };

    ReportBuilder.prototype.getNormalValue = function (offset, dt) {
        const le = this.littleEndian;
        const v = this.view;
        switch (dt) {
            case DataTypes.uint8: return v.getUint8(offset);
            case DataTypes.int8: return v.getInt8(offset);
            case DataTypes.uint16: return v.getUint16(offset, le);
            case DataTypes.int16: return v.getInt16(offset, le);
            case DataTypes.uint32: return v.getUint32(offset, le);
            case DataTypes.int32: return v.getInt32(offset, le);
            case DataTypes.uint64: return v.getBigUint64(offset, le);
            case DataTypes.int64: return v.getBigInt64(offset, le);
            case DataTypes.float32: return v.getFloat32(offset, le);
            case DataTypes.float64: return v.getFloat64(offset, le);
            default: return 0;
        }
    };

    // Collect values from all instances, starting at given initial offset.
    // All values are regarded as arrays, no matter their multiplicity.
    ReportBuilder.prototype.collectDataInstances = function (block, offset, dt, multiplicity) {
        multiplicity = multiplicity || 1;
        const instances = [];
        const step = CHECKSUM_SIZE + block.data_size;
        const size = dataTypeSize(dt);

        for (let inst = 0; inst < block.instance_count; inst++) {
            const numArray = [];
            let offs = offset;
            for (let m = 0; m < multiplicity; m++) {
                // The hex string shows the most significant byte first, whatever the byte order in memory
                const hexBytes = Array.from(this.getRawValue(offs, dt), toHexByte);
                if (this.littleEndian) hexBytes.reverse();
                numArray.push({
                    value_dec: String(this.getNormalValue(offs, dt)),
                    value_hex: '0x' + hexBytes.join(''),
                    address: formatAddress(offs),
                });
                offs += size;
            }
            instances.push({ data: numArray, is_valid: true, is_most_recent: false });
            offset += step;
        }
        return instances;
    };

    // The checksum of each instance is the CRC-16/IBM-3740 of its data as stored,
    // stored in the target's byte order
    ReportBuilder.prototype.collectChecksumInstances = function (block) {
        const ci = this.collectDataInstances(block, block.offset_in_eeprom, DataTypes.uint16, 1);
        let offset = block.offset_in_eeprom;
        const step = CHECKSUM_SIZE + block.data_size;

        for (const instance of ci) {
            const data = this.eeprom.subarray(offset + CHECKSUM_SIZE, offset + step);
            instance.is_valid = crc16(data) === this.getNormalValue(offset, DataTypes.uint16);
            offset += step;
        }
        return ci;
    };

    ReportBuilder.prototype.collectSequenceCounterInstances = function (block, checksums) {
        const sci = this.collectDataInstances(
            block, block.offset_in_eeprom + CHECKSUM_SIZE, DataTypes.uint8, 1);
        const counters = [];
        for (const c of sci) {
            for (const n of c.data) counters.push(parseInt(n.value_dec, 10));
        }
        for (let i = 0; i < checksums.length; i++) {
            if (!checksums[i].is_valid) counters[i] = 0xFF; // invalidate counter of invalid instance
        }
        const mostRecent = findIndexOfMostRecentSequenceCounter(counters);
        if (mostRecent !== null && mostRecent !== undefined) {
            sci[mostRecent].is_most_recent = true;
        }
        return sci;
    };

    ReportBuilder.prototype.createBlockViews = function () {
        const blockViews = [];

        for (const block of this.datamodel.children) {
            let offset = block.offset_in_eeprom;
            const paramViews = [];
            const checksumInstances = this.collectChecksumInstances(block);

            paramViews.push({
                name: 'Checksum',
                data_type: DataTypeNames[DataTypes.uint16],
                description: 'Stored CRC-16/IBM-3740 of the data',
                instances: checksumInstances,
                kind: 'checksum',
            });
            offset += CHECKSUM_SIZE;

            let seqCounterInstances = null;
            if (block.management_type === ManagementTypes.WearLeveling) {
                seqCounterInstances = this.collectSequenceCounterInstances(block, checksumInstances);
                paramViews.push({
                    name: 'Sequence counter',
                    data_type: DataTypeNames[DataTypes.uint8],
                    description: '',
                    instances: seqCounterInstances,
                    kind: 'sequence',
                });
                offset += 1;
            }

            for (const param of block.children) {
                paramViews.push({
                    name: param.name,
                    data_type: DataTypeNames[param.data_type],
                    description: param.description || '',
                    instances: this.collectDataInstances(block, offset, param.data_type, param.multiplicity),
                    kind: 'param',
                });
                offset += parameterSize(param);
            }

            // Per-instance status used for the column headers.
            const instanceStatus = [];
            for (let i = 0; i < block.instance_count; i++) {
                instanceStatus.push({
                    is_valid: checksumInstances[i].is_valid,
                    is_most_recent: seqCounterInstances ? seqCounterInstances[i].is_most_recent : false,
                });
            }

            blockViews.push({
                name: block.name,
                management_type: ManagementTypeLabels[block.management_type],
                description: block.description || '',
                instance_count: block.instance_count,
                address: formatAddress(block.offset_in_eeprom),
                total_size: block.size_in_eeprom,
                block_identity: formatHex(blockIdentity(block), 4),
                data_identity: formatHex(dataIdentity(block), 4),
                params: paramViews,
                instance_status: instanceStatus,
                is_wear_leveling: block.management_type === ManagementTypes.WearLeveling,
            });
        }
        return blockViews;
    };

    // ---- Top-level orchestration (mirror eeprom_inspector.py main flow) ----

    function validateSize(datamodel, eeprom) {
        const lastBlock = datamodel.children[datamodel.children.length - 1];
        const eepromSize = eeprom.length;
        const datamodelSize = lastBlock ? lastBlock.offset_in_eeprom + lastBlock.size_in_eeprom : IDENTITY_HEADER_SIZE;
        if (eepromSize < datamodelSize) {
            throw new Error(
                'Size of provided EEPROM image (' + eepromSize +
                ' bytes) is insufficient for this Data model (requiring ' +
                datamodelSize + ' bytes)!');
        }
    }

    /**
     * Build the full report model from already-parsed inputs.
     * @param {object} datamodel  parsed datamodel object
     * @param {object} settings   parsed platform settings object
     * @param {Uint8Array} eeprom EEPROM image bytes
     * @returns {{header: object, blocks: Array}}
     */
    function buildReport(datamodel, settings, eeprom) {
        attachBlockMetadata(datamodel, settings);
        validateSize(datamodel, eeprom);

        const builder = new ReportBuilder(datamodel, settings, eeprom);
        const blocks = builder.createBlockViews();

        return {
            header: {
                date: new Date().toString(),
                image_size: eeprom.length,
                endianness: settings.endianness,
                identity_header: inspectIdentityHeader(datamodel, settings, eeprom),
            },
            blocks: blocks,
        };
    }

    EEPROM.HeaderStatus = HeaderStatus;
    EEPROM.inspectIdentityHeader = inspectIdentityHeader;
    EEPROM.ReportBuilder = ReportBuilder;
    EEPROM.buildReport = buildReport;

})(globalThis.EEPROM = globalThis.EEPROM || {});
