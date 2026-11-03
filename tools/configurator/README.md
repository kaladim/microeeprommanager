# Configurator app
A GUI editor for EEPROM data model and platform settings.  
Make sure your favorite web browser has enabled `JavaScript` and simply open the [.html](./index.html) in it.

## Creating a brand new configuration
### Data model
- Select the `Data model` tab.
- Right-click on the left panel and select `Create new data model` from the context menu.
- Once the `data model` node is created, continue with creating blocks, parameters and bitfields, in the same manner.
- Click on each created node in the tree and edit its properties on the right panel.

### Platform settings
- Select the `Platform settings` tab.
- Edit the necessary fields.

There are no checksum parameters: mEEM v2 checks every data model with the same 2-byte CRC-16 (CRC-16/IBM-3740).

## Saving your changes
- Click the `Save to file` button on each tab and select destination path for each `.json` file.  
- The files will be saved to your browser's default download directory.

## Editing existing data model and platform settings
- Use the `Load from file` on each tab to import the respective `.json` files.
- A data model that v1's configurator saved has a `checksum_size`, which the v2 code generator rejects. The configurator removes it when it loads the file, and says so in the status bar. Save the data model to write it without it.

## Identities
While both a data model and platform settings are loaded (or created, or saved), the properties panels show the identities of mEEM v2, as the code generator computes them:
- A block's panel: its `block identity` (name, management type, instance count and data identity) and its `data identity` (the parameters and bitfields). An edit that changes the block identity changes how the block is stored, so the firmware migrates the block from the previous release.
- The data model's panel: the `EEPROM layout identity` (every block identity with its offset, and the endianness) and the 6-byte `identity header` that the firmware writes at EEPROM offset 0.

They aren't shown for the default platform settings that the configurator creates when a block's placement is edited before any platform settings were loaded: those may not be the target's.

<br></br>
# EEPROM data model
The format has a [JSON Schema](../common/js/data-model.schema.json), for your editor or any other validator. The code generator stays the final check, since a schema can't compare e.g. the count of default values with the `multiplicity`. Associate the schema in your editor's settings: don't add a `$schema` attribute to the data model, because the code generator rejects it.

## Blocks
- `name` (string): Has to be a valid C-language identifier
- `description` (string, optional): Will appear in the generated sources.
- `children` (list of Parameters): Must not be emty!
- `management_type` (enum): defines block's strategy for EEPROM area management. See the management types [here](../../doc/Operation.md#17).
- `instance_count` (integer): number of data instances in the EEPROM. Depends on the selected `management_type`:
 
  | Management type | Number of data instances in the EEPROM |
  | --------------- | -------------------------------------- |
  | *Basic*         | 1                                      |
  | *BackupCopy*    | 2                                      |
  | *MultiProfile*  | [2..15], configurable                  |
  | *Wear-leveling* | [2..15], configurable                  |

- `data_recovery_strategy` (enum): defines the behavior if the data integrity check fails on init. The choice is between: load defaults and repair the EEPROM area (recommended) or just load defaults
- `compress_defaults`(boolean): flag, instructing the code generator to deduce the shortest possible pattern for default values. In many cases, you may end up using just a single byte for all your defaults.

## Parameters
- `name` (string): Has to be a valid C-language identifier
- `description` (string, optional): Will appear in the generated sources.
- `children` (list of BitFields). When defining BitFields, you should use an unsigned integer `data_type`
- `data_type` (enum): can be any standard integer and floating-point type.
- `multiplicity` (integer): values > 1 define the parameter as an array
- `default_value` (list of integers/floats, depending on the `data_type`). The count must be equal to the `multiplicity`. A floating-point parameter also accepts NaN, which the file holds as the string `"NaN"`, since JSON has no NaN. In the firmware, test for it with `isnan()`: `x != x` doesn't detect it on XC8.

## BitFields
- `name` (string): of each bitfield has to be a valid C-language identifier
- `description` (string, optional): Will appear in the generated sources.
- `size_in_bits` (integer): size of the bitfield
  
# Platform settings
- [`endianness`](https://en.wikipedia.org/wiki/Endianness) : `little` or `big`  
- `eeprom_size` (integer): amount of EEPROM, allocated to the mEEM
- `eeprom_page_size` (integer): set to 0 for EEPROMs that can only write one byte at-a-time, like most MCU's on-chip ones. When using external EEPROMs, set it to the page size, defined in the EEPROM's datasheet.
- `page_aligned_blocks` (list of strings): block names, which you want aligned to EEPROM page boundaries. It's highly recommended for wear-leveling blocks. Make sense only if `eeprom_page_size` > 0. An asterisk (`*`) means *all blocks*.
- `enter_critical_section_operation` (string, optional): define this one only if you use the mEEM in a pre-emptive environment. Your OS usually provides one.
- `exit_critical_section_operation` (string, optional): define this one only if you use the mEEM in a pre-emptive environment. Your OS usually provides one.
- `external_headers` (list of strings): header(s), containing declarations of `enter/exit critical section` operations
- `crc_implementation` (enum): how the core calculates the instance checksum, CRC-16/IBM-3740. All of them give the same result. `table` (the default): built-in routine with a 256-entry table (512 bytes of flash), the fastest built-in variant. `bitwise`: built-in routine without a table, the smallest; choose it when flash is tight. `hardware`: the MCU's CRC unit, through your driver for `MEEM_CRCIF.h`.
- `compiler_directives` (dict[string, string]): compiler-specific directives for packing and placement:  
  - `opening_pack_directive` (string): required only for 16/32/64 bit CPUs. All generated mEEM data structures must be byte-aligned!
  - `closing_pack_directive` (string): used in conjunction with `compiler_opening_pack_directive`
  - `pack_attribute` (string): has the same purpose as `compiler_opening_pack_directive`/`compiler_closing_pack_directive`, but will be placed inline with generated type definitions. You either use directive or attribute, but never both at the same time.  
  - `block_placement_directives` dict with blocks (names as keys), that requires special placement of defaults or/and caches:
    - `directive_for_defaults` (string, optional): In some situations, you may need special placement of defaults.
    - `attribute_for_defaults` (string, optional): You may use either an attribute or a directive, but never both at the same time
    - `directive_for_cache` (string, optional): In some situations, you may need special placement of the cache.
    - `attribute_for_cache` (string, optional): You may use either an attribute or a directive, but never both at the same time
  - `crc_table_placement_directive` (string, optional): placement directive for the CRC table, placed just before its definition. Applies only to `crc_implementation` `table`.
  - `crc_table_placement_attribute` (string, optional): placement attribute for the CRC table, like AVR's `__flash`, added to its definition and declaration. Applies only to `crc_implementation` `table`. You either use a directive or an attribute, but never both at the same time.
- `code_gen_settings` (dict): settings that control the style of the generated code:
  - `language_standard` (enum): C standard used for `const` object initializations. `c99` uses designated initializers (e.g. `.field = value`), `c90` uses positional initializers only. Default: `c90`.
  - `accessor_style` (enum): style of the generated parameter accessors (getters/setters). `inline_functions` generates `static inline` getters/setters, `macros` generates preprocessor macros. Default: `inline_functions`.
  - `cache_layout` (enum): layout of the parameter cache objects. `granular` generates one cache struct per block, `monolith` aggregates all blocks into one large struct. Default: `granular`.
