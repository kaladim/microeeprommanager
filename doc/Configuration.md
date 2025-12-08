## Configuration

### 1. Create a directory for the EEPROM data model, platform settings (and optionally - for generated mEEM configuration):
It's recommended that would be somewhere in your application's code base; see how it's done in [the example](../example/Microchip/MEEM_Config/).  
The best approach would be to integrate the generation in the build process and made it executed as a pre-build step.  

### 2. Create an EEPROM data model & platform settings
Use the provided [GUI configurator](../tools/configurator/README.md). All settings are explained in details there.

### 3. Generate the sources
Feed the newly created data model & platform settings to [the code generator](../tools/meem_config_gen/README.md).
