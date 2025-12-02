# Code generator for the **mEEM**
Generates a configuration and API for the *mEEM* as C-language source code.

# Usage
## 1. Create a directory for the EEPROM data model, platform settings (and optionally - for generated mEEM configuration):
It's recommended that would be somewhere in your application's code base; see how it's done in [the example](../../example/Microchip/MEEM_Config/).  
The best approach would be to integrate the generation in the build process and made it executed as a pre-build step.  

## 2. Create JSONs for datamodel & platform settings
Use the provided [configurator](../configurator/README.md). Later, you may modify the JSON files manually.  

## 3. Generate the sources
The generator script is `meem_config_gen.py` and expects 3 input files:  

| Parameter      | Required | Description                          |
| -------------- | -------- | ------------------------------------ |
| `<DATAMODEL>`  | ✅        | Path to `datamodel.json`             |
| `<SETTINGS>`   | ✅        | Path to `platform_settings.json`     |
| `<OUTPUT_DIR>` | ✅        | Output directory for the source code |

In `tools` directory execute:
```bash
python3 ./meem_config_gen/meem_config_gen.py  <DATAMODEL>  <SETTINGS>  <OUTPUT_DIR>
```
