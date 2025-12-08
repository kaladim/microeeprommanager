# mEEM: automatic [EEPROM](https://en.wikipedia.org/wiki/EEPROM) manager for resource-constrained embedded systems.
## Why mEEM?
- Unlike most of the freely available EEPROM libraries, the *mEEM* provides your application with a non-blocking, immediate-like access to non-volatile data.
- Guarantees data integrity through checksum verification and provision of default values, so your application would never operate with invalid/undefined data.
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
$ git submodule update --init
```

## Prerequisites
[Python 3.10](https://www.python.org/downloads/) (or newer) + an [active virtual environment](./setup_venv.sh):  
**Linux/macOS:**
```bash
$ source ./setup_venv.sh
```
**Windows:**
```powershell
> .\setup_venv.ps1
```

## Integration
### 1. Configure & generate:
- The process is covered in details [here](./doc/Configuration.md).  

### 2. Implement the required interface:  
- Function bodies of the [EEPROM access interface](src/required_interface/MEEM_EEAIF.h)  
- Function body of the [checksum routine](src/required_interface/MEEM_Checksum.h)  
- Function bodies of [user callbacks](src/required_interface/MEEM_UserCallbacks.h)  

**A well-written EEPROM access driver and properly chosen checksum algorithm have crucial role in the proper operation of the *mEEM*!**

### 3. Include the [MEEM.h](src/provided_interface/MEEM.h) in your application code.  
The provided API consists of two parts:  
- Core: interface to the *mEEM's* core logic, provided by the [MEEM.h](./src/provided_interface/MEEM.h) header  
- Generated: project-specific interface, provided by the [MEEM_GenInterface.h](./example/Microchip/MEEM_Config/generated/MEEM_GenInterface.h) header.  
Since the *mEEM* is useless without the generated interface, *MEEM.h* includes also *MEEM_GenInterface.h*.  
- Although block caches have public visibility, avoid their direct usage. Prefer the generated getters and setters.    

### 4. Update your make configuration/project:  
- Add [core *mEEM* files](src/) and include paths  
- Add [generated *mEEM* files](/example/Microchip/MEEM_Config/) and include paths  
- Add [implemented required interface](/example/Microchip/MEEM_Config/MEEM_UserCallbacks.c) and include paths  

See the provided [complete example](./example/Microchip/) for reference.

## Tools
- [Configurator](./tools/configurator/README.md)
- [Code generator](./tools/meem_config_gen/README.md)
- [EEPROM binary image generator](./tools/eeprom_image_gen/README.md)
- [EEPROM binary image inspector](./tools/eeprom_inspector/README.md)

## License
This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.