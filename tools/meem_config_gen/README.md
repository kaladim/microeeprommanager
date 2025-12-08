# Code generator for the *mEEM*
Generates a configuration and API for the *mEEM* as C-language source code.   
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
