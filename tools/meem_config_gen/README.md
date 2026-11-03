# Code generator for the *mEEM*
Generates a configuration and API for the *mEEM* as C-language source code.   
The generator script is `meem_config_gen.py` and expects 3 input files, and optionally the data models of your earlier releases:  

| Parameter      | Required | Description                          |
| -------------- | -------- | ------------------------------------ |
| `<DATAMODEL>`  | ✅        | Path to `datamodel.json`             |
| `<SETTINGS>`   | ✅        | Path to `platform_settings.json`     |
| `<OUTPUT_DIR>` | ✅        | Output directory for the source code |
| `--legacy <DATAMODEL> <SETTINGS>` | ⚪ | A released data model, with the platform settings it was released with, from which `MEEM_Init()` migrates. Repeatable. |
| `--legacy-dir <DIR>` | ⚪ | A directory of released data models, one per subdirectory as `data-model.json` with `platform-settings.json`, as the [legacy model collector](../legacy_model_collector/README.md) writes it. Repeatable, and goes with `--legacy`. |

## Usage
[Make sure](../../README.md#prerequisites) `uv` is installed. It provisions Python and the dependencies on demand, so no virtual environment has to be created or activated by hand.

From the project root execute:
```bash
uv run ./tools/meem_config_gen/meem_config_gen.py  <DATAMODEL>  <SETTINGS>  <OUTPUT_DIR>  [--legacy <DATAMODEL> <SETTINGS>]...  [--legacy-dir <DIR>]...
```

### Example
```bash
uv run ./tools/meem_config_gen/meem_config_gen.py \
    ./example/Microchip/MEEM_Config/eeprom_datamodel_example.json \
    ./example/Microchip/MEEM_Config/platform_settings_xc8.json \
    ./example/Microchip/MEEM_Config/generated
```

## Generated files
| File | Content |
| --- | --- |
| `MEEM_GenConfig.h/.c` | The block configuration, the default values, the identity header, and with `crc_implementation` set to `table`, the CRC table |
| `MEEM_GenInterface.h/.c` | The project-specific API: the block IDs, the caches, and the getters and setters of the parameters |
| `MEEM_Migration.h/.c` | The legacy configurations, the migration routines and their dispatcher. It's always generated, and without legacy models the core compiles its migration code out. |

The same inputs give byte-identical files: they hold no date or time stamp.

## Legacy models
With legacy models, `MEEM_Init()` migrates the data of these releases. Each legacy model is laid out with its own platform settings, and its `crc_implementation` and `checksum_size` are ignored. [Configuration](../../doc/Configuration.md#4-give-the-generator-your-earlier-releases) explains how to get them, what is migrated, and the errors and warnings the generator reports.
