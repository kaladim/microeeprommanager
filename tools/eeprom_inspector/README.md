# EEPROM Inspector

Creates a human-readable HTML report from an EEPROM binary image. Raw binary, [Intel HEX](https://en.wikipedia.org/wiki/Intel_HEX) and [Motorola S-record](https://en.wikipedia.org/wiki/Motorola_S-record) formats are accepted.    
Requires the same EEPROM data model and platform settings that were used for the **mEEM** configuration when the image was produced.  

## Features
- Shows the content of all available data instances within each block
- Validates the checksum of each data instance and tags the instance as `valid` or `invalid`
- Tags the `most recent` instance in wear-leveling blocks

## Usage
[Make sure](../../README.md#tools) the Python virtual environment is active.

### Example
```bash
python3 ./eeprom_inspector/eeprom_inspector.py \
    ./data/eeprom_dump.bin \
    ./config/datamodel.json \
    ./config/platform_settings.json \
    ./config/checksum_parameters.json \
    -o ./report_01.html
```

### Parameters
| Parameter       | Required | Description                                                    |
| --------------- | -------- | -------------------------------------------------------------- |
| `<EEPROM_BIN>`  | ✅        | Path to EEPROM binary dump (*.bin, *.hex, *s19)                |
| `<DATAMODEL>`   | ✅        | Path to `datamodel.json` (mEEM configuration)                  |
| `<SETTINGS>`    | ✅        | Path to `platform_settings.json` (mEEM configuration)          |
| `<CHECKSUM>`    | ✅        | Path to `checksum_parameters.json`                             |
| `<OUTPUT_FILE>` | ⚪        | Output file path. Defaults to `./report.html` if not specified |
