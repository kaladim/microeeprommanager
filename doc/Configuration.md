## Configuration

### 1. Create a directory for the EEPROM data model, platform settings (and optionally - for generated mEEM configuration):
It's recommended that would be somewhere in your application's code base; see how it's done in [the example](../example/Microchip/MEEM_Config/).  
The best approach would be to integrate the generation in the build process and made it executed as a pre-build step.  

### 2. Create an EEPROM data model & platform settings
Use the provided [GUI configurator](../tools/configurator/README.md). All settings are explained in details there.

### 3. Generate the sources
Feed the newly created data model & platform settings to [the code generator](../tools/meem_config_gen/README.md):
```bash
uv run ./tools/meem_config_gen/meem_config_gen.py  <DATAMODEL>  <SETTINGS>  <OUTPUT_DIR>
```
[`uv`](../README.md#prerequisites) takes care of the Python interpreter and the dependencies, so the command is also safe to call directly from a build script (see [the CMake integration](../test/meem_config/CMakeLists.txt) and [the MPLAB X pre-build actions](../example/Microchip/mEEM-demo.X/pre_build_actions.ps1) for reference).

### 4. Give the generator your earlier releases
A change of the data model usually changes the EEPROM layout of your next release. A device that updates to that release migrates its data from the old layout, if the generator got the data model of the release the device comes from, its *legacy model*. If it didn't, the device doesn't recognize the old layout and [resets](./Operation.md#reset) its EEPROM: every block gets its default values.

So each build needs the data models of all releases that are still in use. The [legacy model collector](../tools/legacy_model_collector/README.md) collects them from your git tags, with the platform settings of each release. Run it before the generator:
```bash
uv run ./tools/legacy_model_collector/legacy_model_collector.py  <DATAMODEL>  <SETTINGS>  <LEGACY_DIR>  --tags "v*"

uv run ./tools/meem_config_gen/meem_config_gen.py  <DATAMODEL>  <SETTINGS>  <OUTPUT_DIR>  --legacy-dir <LEGACY_DIR>
```
- Tag every release, and keep the data model and the platform settings at the same paths: a tag in which they have other paths is ignored.
- `<LEGACY_DIR>` is a build artifact, so don't commit it. It's empty until your first release on *mEEM* v2.
- Without the tags, give the models by hand with `--legacy <DATAMODEL> <SETTINGS>`, once per model, each with the platform settings it was released with. A committed directory in [the collector's format](../tools/legacy_model_collector/README.md#the-directory-of-models) works too, as a second `--legacy-dir`.
- Leaving a release out, for example with `--exclude-tags` or `--depth`, saves its flash, but devices on that release will reset. Each legacy model costs its tables: the 6 B header with two pointers, 1 B per current block and 5 B per block that is copied or converted. A generated routine comes on top for each changed block, unless another legacy model already needs the same one.
- [The example's pre-build actions](../example/Microchip/mEEM-demo.X/pre_build_actions.ps1) only run the generator, because the demo has no earlier releases.

**What is migrated.** The migration derives from the data models alone, so there's nothing to write or to add to the data model:

| Change since the legacy model | What the block gets |
| --- | --- |
| None, but the block moved because an earlier block changed | Its data, copied from the old place |
| Instance count or management type | Its data. Wear-leveling takes the most recent valid instance, backup-copy the first valid copy. Multi-profile blocks keep their profiles by index, and new profiles get default values. A multi-profile block that becomes another type takes profile 0, and a block that becomes multi-profile fills every profile. |
| Parameter added, removed or reordered | Parameters are matched by name. New ones keep their default values, removed ones are dropped. |
| Parameter type | The value is converted: it's rounded and saturated to the new range. A negative value becomes 0 in an unsigned type, and NaN becomes 0 in an integer type. |
| Parameter multiplicity | The first elements are converted, as many as both sides have. The rest keep their default values or are dropped. |
| Bitfields | Matched by name, and the value is clamped to the new width. New ones keep their default bits. A parameter that gains its first bitfields or loses all of them is converted as a whole value. |
| Block added | Default values |
| Block removed | Dropped |

What the names don't reveal isn't migrated:
- A renamed block, parameter or bitfield, or a parameter moved to another block, starts from its default value.
- A change of meaning under the same name (units, enum values, scaling) is migrated as if the meaning were the same. To discard the old value on purpose, give the parameter a new name.

**What the generator reports:**
- Errors, which stop the build:
  - A legacy model with another endianness.
  - MultiProfile blocks that have no safe order, for example two that swap places. The error names them.
  - Two different layouts with the same identity. The error names them. Change the new layout, for example by reordering two new parameters.
  - A current data model that still has `checksum_size`.
  - A data model that doesn't fit `eeprom_size`, including the identity header.
- Warnings: dropped parameters, elements or bitfields, and narrowing conversions. Read them when you change a data model: they tell which data the devices lose with the update.
- Info: a legacy model identical to the current one or to another legacy model is ignored.

### 5. Choose how the checksum is calculated
The checksum is always CRC-16/IBM-3740, so this choice doesn't change a byte in the EEPROM. `crc_implementation` in the platform settings selects how the core calculates it, and only that variant is compiled:

| Value | Implementation | Choose it when |
| --- | --- | --- |
| `table` (default) | Built-in routine with a 256-entry table (512 B of flash) | 512 B of flash are affordable. It's the fastest built-in variant. |
| `bitwise` | Built-in routine without a table | Flash is tight. It's the smallest. |
| `hardware` | The MCU's CRC unit, through your driver for [`MEEM_CRCIF.h`](../src/required_interface/MEEM_CRCIF.h) | The MCU has a CRC unit, and CPU time matters more than flash. It saves the table, but not flash compared with `bitwise`. |

A compiler that needs more than `const` to place the table in flash, such as AVR with `__flash`, gets it from `crc_table_placement_directive` or `crc_table_placement_attribute` in `compiler_directives`: either one, and only with `table`.

With `hardware`:
- The application initializes the unit before `MEEM_Init()`.
- The application may share the unit with the core, because the core retries when the driver reports `CRCIF_BUSY`. But it must not hold the unit while `MEEM_Init()` runs, which waits for it.
- The driver retries by itself where that makes sense. The core doesn't retry after `CRCIF_NOK`: a failed check counts like a failed read, and a failed calculation for a write skips the write and sets `write_failed`.
- Debug builds run `"123456789"` through the unit at init and assert `0x29B1`, which catches a misconfigured unit. In a release build, a unit that computes another CRC makes every block fail its check, and the tools reject what the device then writes.
