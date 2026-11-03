# mEEM: automatic [EEPROM](https://en.wikipedia.org/wiki/EEPROM) manager for resource-constrained embedded systems.
## Why mEEM?
- Unlike most of the freely available EEPROM libraries, the *mEEM* provides your application with a non-blocking, immediate-like access to non-volatile data.
- Guarantees data integrity through checksum verification and provision of default values, so your application would never operate with invalid/undefined data.
- Keeps your data when your data model changes: after a firmware update, the *mEEM* recognizes the EEPROM layouts of your earlier releases and migrates their data, with no user code.
- Configurable via [GUI tool](./tools/configurator/README.md) and [JSON](./tools/) files, so you don't have to deal with EEPROM addresses at all.
- Platform-independent: can be used on bare metal, with Arduinos or RTOSes. It follows an asynchronous, non-blocking programming model, that fits everywhere. [Thread-safe](https://en.wikipedia.org/wiki/Thread_safety) with caution.
- Hardware-independent: uses unified interface to MCUs' on-chip EEPROMs, external serial EEPROMs/FLASHes, on-chip *data FLASH*es.
- Highly scalable, with minimalalistic memory footprint
- Manages up to 64KiB EEPROM address space
- Written in C99

## Principle of operation ([TL;DR](https://en.wikipedia.org/wiki/TL%3BDR))
`At startup, the mEEM loads all EEPROM data into RAM (the "cache") and validates it; if data is invalid, default values are loaded; your application uses only the cache, and occasionnaly requests specific caches to be written back to the EEPROM.`  
For more, see [a detailed description](./doc/Operation.md).  

## Get & setup the repo
```bash
$ git clone https://github.com/kaladim/microeeprommanager.git
$ cd microeeprommanager
$ git submodule update --init --recursive
```

## Project structure
```
/
├── src/                          # mEEM library source code
│   ├── core/                     # Core EEPROM management logic
│   ├── provided_interface/       # Public API headers
│   └── required_interface/       # Interface to be implemented by user (EEPROM access, callbacks, optionally the CRC unit)
├── test/                         # Unit tests with GoogleTest
│   ├── meem_config/              # Test configurations
│   ├── eeprom_simulator/         # EEPROM simulator for testing
│   └── mocks/                    # Mock implementations
├── tools/                        # Configuration and utility tools
│   ├── common/                   # Code shared by the tools
│   │   ├── js/                   # ... by the web tools
│   │   └── py/                   # ... by the Python tools
│   ├── configurator/             # GUI configuration tool
│   ├── meem_config_gen/          # Code generator from JSON config
│   ├── eeprom_image_gen/         # EEPROM binary image generator
│   ├── eeprom_inspector/         # EEPROM binary image inspector
│   └── legacy_model_collector/   # Collects the released data models from the git tags
├── example/                      # Complete integration examples
│   └── Microchip/
│       ├── Applications/         # Example application code
│       ├── EEPROM_Driver_PIC16F/ # EEPROM driver implementation
│       ├── MEEM_Config/          # Example mEEM configuration
│       └── mEEM-demo.X/          # MPLAB X demo project
├── doc/                          # Documentation
│   ├── Configuration.md          # Configuration guide
│   └── Operation.md              # Detailed operation description
├── CMakeLists.txt                # CMake build configuration
├── pyproject.toml                # Python project & dependency declaration
├── uv.lock                       # Pinned Python dependencies (managed by uv)
└── .python-version               # Python version used by the tools
```

## Prerequisites
* Python 3.12 or newer
* [uv](https://docs.astral.sh/uv/) package and project manager.

## Integration
### 1. Configure & generate:
- The process is covered in details [here](./doc/Configuration.md).  
- To migrate the data of your earlier releases, the code generator needs their data models. The [legacy model collector](./tools/legacy_model_collector/README.md) collects them from your git tags, see [here](./doc/Configuration.md#4-give-the-generator-your-earlier-releases).  

### 2. Implement the required interface:  
- Function bodies of the [EEPROM access interface](src/required_interface/MEEM_EEAIF.h)  
- Function bodies of [user callbacks](src/required_interface/MEEM_UserCallbacks.h)  
- Only with `crc_implementation` set to `hardware`: function bodies of the [CRC unit interface](src/required_interface/MEEM_CRCIF.h), see [here](./doc/Configuration.md#5-choose-how-the-checksum-is-calculated)  

> [!IMPORTANT]
> **A well-written EEPROM access driver has crucial role in the proper operation of the *mEEM*!**

### 3. Include the [MEEM.h](src/provided_interface/MEEM.h) in your application code.  
The provided API consists of two parts:  
- Core: interface to the *mEEM's* core logic, provided by the [MEEM.h](./src/provided_interface/MEEM.h) header  
- Generated: project-specific interface, provided by the [MEEM_GenInterface.h](./example/Microchip/MEEM_Config/generated/MEEM_GenInterface.h) header.  
Since the *mEEM* is useless without the generated interface, *MEEM.h* includes also *MEEM_GenInterface.h*.  
- Although block caches have public visibility, avoid their direct usage. Prefer the generated getters and setters:    
``` bash
    MEEM_Get_<block-name>_<parameter-name>()
    MEEM_Set_<block-name>_<parameter-name>(value)
```

### 4. Update your make configuration/project:  
- Add [core *mEEM* files](src/) and include paths  
- Add [generated *mEEM* files](/example/Microchip/MEEM_Config/) and include paths. `MEEM_Migration.c` is among them, also when there's nothing to migrate from.  
- Add [implemented required interface](/example/Microchip/MEEM_Config/MEEM_UserCallbacks.c) and include paths  
- [Optional] Invoke the [Legacy model collector](./tools/legacy_model_collector/README.md) as a pre-build step

See the provided [complete example](./example/Microchip/) for reference.

## Updating a project from v1
The stored data doesn't survive the update. In the project:
- Remove `checksum_size` from the data model, because the code generator rejects a data model that has it. The [configurator](./tools/configurator/README.md) removes it when it loads the file.
- Remove any whitespace around the names of blocks, parameters and bitfields, because the code generator rejects it. It doesn't change the EEPROM layout.
- Delete `checksum_params.json` and your implementation of `MEEM_Checksum.h`. The checksum is now the same for every block: a 2-byte CRC-16/IBM-3740. A data model that used a 1-byte checksum needs 1 more byte of EEPROM per data instance.
- The image of a data model now also holds the 6-byte identity header, so check that `eeprom_size` still fits. The code generator reports an error if it doesn't.
- Add `src/core/MEEM_CRC.c`, `src/core/MEEM_Legacy.c` and the generated `MEEM_Migration.c` to your build.
- `MEEM_GEN_TIMESTAMP` is gone, and the generated files have no date, so the same inputs give the same files. `MEEM_EEPROM_LAYOUT_IDENTITY` tells the layout that the firmware was generated for.
- The [image generator](./tools/eeprom_image_gen/README.md) and the [inspector](./tools/eeprom_inspector/README.md) no longer take checksum parameters.
- Call `MEEM_DeInit()` only when `MEEM_IsBusy()` returns false, see [here](./doc/Operation.md#rewrites-and-the-commit).
- Tag your releases from now on, and let your build [collect their data models](./doc/Configuration.md#4-give-the-generator-your-earlier-releases).

The flash cost of v2 is measured in the [footprint harness' report](./test/footprint/README.md#v2-measurements).

## Tools
- [Configurator](./tools/configurator/README.md)
- [Code generator](./tools/meem_config_gen/README.md)
- [EEPROM binary image generator](./tools/eeprom_image_gen/README.md)
- [EEPROM binary image inspector](./tools/eeprom_inspector/README.md)
- [Legacy model collector](./tools/legacy_model_collector/README.md)

## License
This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.