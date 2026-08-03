# EEPROM image generator

Creates a ready-to-flash binary image from a data model, platform settings and checksum parameters. Default values from the data model are used.  
The output is a [Intel HEX](https://en.wikipedia.org/wiki/Intel_HEX) file.

## Usage
[Make sure](../../README.md#prerequisites) `uv` is installed. It provisions Python and the dependencies on demand, so no virtual environment has to be created or activated by hand.

### Example

From the project root execute:
```bash
uv run ./tools/eeprom_image_gen/eeprom_image_gen.py \
    ./config/datamodel.json \
    ./config/platform_settings.json \
    ./config/checksum_parameters.json \
    -a 0x300000 \
    -f 0xAA \
    -o ./eeprom_image
```

### Parameters

| Parameter      | Required | Description                                                                                                 |
| -------------- | -------- | ----------------------------------------------------------------------------------------------------------- |
| `<DATAMODEL>`  | ✅        | Path to `datamodel.json` (mEEM configuration)                                                               |
| `<SETTINGS>`   | ✅        | Path to `platform_settings.json` (mEEM configuration)                                                       |
| `<CHECKSUM>`   | ✅        | Path to `checksum_parameters.json`                                                                          |
| `<ADDRESS>`    | ⚪        | Base address of the image. Hex format only, with leading `0x`. Defaults to 0 if not specified.              |
| `<FILL>`       | ⚪        | Fill byte for unused EEPROM space. Hex format only, with leading `0x`. Defaults to `0xFF` if not specified. |
| `<OUTPUT_DIR>` | ⚪        | Output directory for generated EEPROM image. Defaults to `./` if not specified                              |
