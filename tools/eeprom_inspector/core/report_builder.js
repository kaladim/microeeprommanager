/*
 * mEEM EEPROM Inspector - report / view-model builder + orchestration.
 *
 * Reads the EEPROM image according to the data model and produces the view
 * model consumed by the UI (blocks, parameters, per-instance values, checksum
 * validity and wear-leveling "most recent" tags). Faithful port of
 * report_builder.py / view_types.py and the eeprom_inspector.py main flow.
 *
 * Part of the split `core/` business logic. Classic (non-module) script that
 * contributes to the shared global `EEPROM` namespace. Depends on data_model.js,
 * crc.js and wear_leveling.js (all loaded earlier).
 */
(function (EEPROM) {
    'use strict';

    const DataType = EEPROM.DataType;
    const DataTypeName = EEPROM.DataTypeName;
    const ManagementType = EEPROM.ManagementType;
    const ManagementTypeName = EEPROM.ManagementTypeName;
    const dataTypeSize = EEPROM.dataTypeSize;
    const parameterSize = EEPROM.parameterSize;
    const ChecksumAlgorithm = EEPROM.ChecksumAlgorithm;
    const attachBlockMetadata = EEPROM.attachBlockMetadata;
    const findIndexOfMostRecentSequenceCounter = EEPROM.findIndexOfMostRecentSequenceCounter;

    // ---- Value formatting helpers (mirror report_builder.py) --------------

    function toHexByte(b) {
        return b.toString(16).toUpperCase().padStart(2, '0');
    }

    function formatAddress(offset) {
        return '0x' + offset.toString(16).toUpperCase().padStart(4, '0');
    }

    function ReportBuilder(datamodel, settings, eeprom, checksumAlgo) {
        this.datamodel = datamodel;
        this.settings = settings;
        this.eeprom = eeprom; // Uint8Array
        this.view = new DataView(eeprom.buffer, eeprom.byteOffset, eeprom.byteLength);
        this.littleEndian = settings.endianness !== 'big';
        this.checksumAlgo = checksumAlgo;
    }

    ReportBuilder.prototype.getRawValue = function (offset, dt) {
        const size = dataTypeSize(dt);
        return this.eeprom.subarray(offset, offset + size);
    };

    ReportBuilder.prototype.getNormalValue = function (offset, dt) {
        const le = this.littleEndian;
        const v = this.view;
        switch (dt) {
            case DataType.uint8: return v.getUint8(offset);
            case DataType.int8: return v.getInt8(offset);
            case DataType.uint16: return v.getUint16(offset, le);
            case DataType.int16: return v.getInt16(offset, le);
            case DataType.uint32: return v.getUint32(offset, le);
            case DataType.int32: return v.getInt32(offset, le);
            case DataType.uint64: return v.getBigUint64(offset, le);
            case DataType.int64: return v.getBigInt64(offset, le);
            case DataType.float32: return v.getFloat32(offset, le);
            case DataType.float64: return v.getFloat64(offset, le);
            default: return 0;
        }
    };

    ReportBuilder.prototype.getChecksumType = function () {
        if (this.datamodel.checksum_size === 1) return DataType.uint8;
        if (this.datamodel.checksum_size === 2) return DataType.uint16;
        return DataType.uint32;
    };

    // Collect values from all instances, starting at given initial offset.
    // All values are regarded as arrays, no matter their multiplicity.
    ReportBuilder.prototype.collectDataInstances = function (block, offset, dt, multiplicity) {
        multiplicity = multiplicity || 1;
        const instances = [];
        const step = block.data_size + this.datamodel.checksum_size;
        const size = dataTypeSize(dt);

        for (let inst = 0; inst < block.instance_count; inst++) {
            const numArray = [];
            let offs = offset;
            for (let m = 0; m < multiplicity; m++) {
                const raw = this.getRawValue(offs, dt);
                // Faithful to the Python: the hex string is the raw bytes in
                // reversed memory order (i.e. correct for little-endian).
                let hex = '';
                for (let b = raw.length - 1; b >= 0; b--) hex += toHexByte(raw[b]);
                numArray.push({
                    value_dec: String(this.getNormalValue(offs, dt)),
                    value_hex: '0x' + hex,
                    address: formatAddress(offs),
                });
                offs += size;
            }
            instances.push({ data: numArray, is_valid: true, is_most_recent: false });
            offset += step;
        }
        return instances;
    };

    ReportBuilder.prototype.collectChecksumInstances = function (block) {
        const ci = this.collectDataInstances(block, block.offset_in_eeprom, this.getChecksumType(), 1);
        let offset = block.offset_in_eeprom + this.datamodel.checksum_size;
        const step = block.data_size + this.datamodel.checksum_size;

        for (const instance of ci) {
            const region = this.eeprom.subarray(offset, offset + block.data_size);
            const calculated = this.checksumAlgo.calculate(region);
            const raw = parseInt(instance.data[0].value_dec, 10);
            instance.is_valid = calculated === raw;
            offset += step;
        }
        return ci;
    };

    ReportBuilder.prototype.collectSequenceCounterInstances = function (block, checksums) {
        const sci = this.collectDataInstances(
            block, block.offset_in_eeprom + this.datamodel.checksum_size, DataType.uint8, 1);
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
                data_type: DataTypeName[this.getChecksumType()],
                description: 'Actual checksum',
                instances: checksumInstances,
                kind: 'checksum',
            });
            offset += this.datamodel.checksum_size;

            let seqCounterInstances = null;
            if (block.management_type === ManagementType.WearLeveling) {
                seqCounterInstances = this.collectSequenceCounterInstances(block, checksumInstances);
                paramViews.push({
                    name: 'Sequence counter',
                    data_type: DataTypeName[DataType.uint8],
                    description: '',
                    instances: seqCounterInstances,
                    kind: 'sequence',
                });
                offset += 1;
            }

            for (const param of block.children) {
                paramViews.push({
                    name: param.name,
                    data_type: DataTypeName[param.data_type],
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
                management_type: ManagementTypeName[block.management_type],
                description: block.description || '',
                instance_count: block.instance_count,
                total_size: block.size_in_eeprom,
                params: paramViews,
                instance_status: instanceStatus,
                is_wear_leveling: block.management_type === ManagementType.WearLeveling,
            });
        }
        return blockViews;
    };

    // ---- Top-level orchestration (mirror eeprom_inspector.py main flow) ----

    function validateSize(datamodel, eeprom) {
        const lastBlock = datamodel.children[datamodel.children.length - 1];
        const eepromSize = eeprom.length;
        const datamodelSize = lastBlock.offset_in_eeprom + lastBlock.size_in_eeprom;
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
     * @param {object} checksumParams parsed checksum parameters object
     * @returns {{header: object, blocks: Array}}
     */
    function buildReport(datamodel, settings, eeprom, checksumParams) {
        const checksumAlgo = new ChecksumAlgorithm(checksumParams);
        attachBlockMetadata(datamodel, settings);
        validateSize(datamodel, eeprom);

        const builder = new ReportBuilder(datamodel, settings, eeprom, checksumAlgo);
        const blocks = builder.createBlockViews();

        return {
            header: {
                date: new Date().toString(),
                image_size: eeprom.length,
                endianness: settings.endianness,
                checksum_size: datamodel.checksum_size,
                checksum_params: checksumParams,
            },
            blocks: blocks,
        };
    }

    EEPROM.ReportBuilder = ReportBuilder;
    EEPROM.buildReport = buildReport;

})(globalThis.EEPROM = globalThis.EEPROM || {});
