# Code generator for the *mEEM*
Generates a configuration and API for the *mEEM* as C-language source code.   
The generator script is `meem_config_gen.py` and expects 3 input files:  

| Parameter      | Required | Description                          |
| -------------- | -------- | ------------------------------------ |
| `<DATAMODEL>`  | ✅        | Path to `datamodel.json`             |
| `<SETTINGS>`   | ✅        | Path to `platform_settings.json`     |
| `<OUTPUT_DIR>` | ✅        | Output directory for the source code |

## Usage
[Make sure](../../README.md#prerequisites) `uv` is installed. It provisions Python and the dependencies on demand, so no virtual environment has to be created or activated by hand.

From the project root execute:
```bash
uv run ./tools/meem_config_gen/meem_config_gen.py  <DATAMODEL>  <SETTINGS>  <OUTPUT_DIR>
```

### Example
```bash
uv run ./tools/meem_config_gen/meem_config_gen.py \
    ./example/Microchip/MEEM_Config/eeprom_datamodel_example.json \
    ./example/Microchip/MEEM_Config/platform_settings_xc8.json \
    ./example/Microchip/MEEM_Config/generated
```
