# Configurator app
A GUI editor for EEPROM data model, platform settings and checksum parameters.  
Just open the [.html](./configurator.html) file in your favorite browser.

## Creating a brand new configuration
<details>

### Data model
- Select the `Data model` tab.
- Right-click on the left panel and select `Create new data model` from the context menu.
- Once the `data model` node is created, continue with creating blocks, parameters and bitfields, in the same manner.
- Click on each created node in the tree and edit its properties on the right panel.

### Platform settings
- Select the `Platform settings` tab.
- Edit the necessary fields.

### Checksum parameters
- Select the `Checksum parameters` tab.
- Edit the necessary fields.
- Add new or remove existing fields by using the context menu (right-click on the labels on the left side).
</details>

## Saving your changes
- Click the `Save to file` button on each tab and select destination path for each `.json` file.  
- The files will be saved to your browser's default download directory.

## Editing existing data model, platform settings and checksum parameters
- Use the `Load from file` on each tab to import the respective `.json` files.

<br></br>
# EEPROM data model
<details>

## Blocks
- `name` of each block has to be a valid C-language identifier
- `description` optional description. Will appear in the generated sources.
- `children` list of Parameters. Must not be emty!
- `management_type` defines block's strategy for EEPROM area management. See [here](../../README.md#17).
- `instance_count` number of data instances in the EEPROM. Depends on the selected `management_type`:
 
  | Management type | Number of data instances in the EEPROM |
  | --------------- | -------------------------------------- |
  | *Basic*         | 1                                      |
  | *BackupCopy*    | 2                                      |
  | *MultiProfile*  | [2..15], configurable                  |
  | *Wear-leveling* | [2..15], configurable                  |

- `data_recovery_strategy` defines the beahavior if the data integrity check fails on init. The choice is between: load defaults and repair the EEPROM area (recommended) or just load defaults
- `compress_defaults` flag, instructing the code generator to deduce the shortest possible pattern for default values. In many cases, you may end up using just a single byte for all your defaults.

## Parameters
- `name` of each parameter has to be a valid C-language identifier
- `description` optional description. Will appear in the generated sources.
- `children` optional list of BitFields. When defining BitFields, you should use an unsigned integer `data_type`
- `data_type` can be any standard integer and floating-point type.
- `multiplicity` values > 1 define the parameter as an array
- `default_value` list of integers/floats, depending on the `data_type`. The count must be equal to the `multiplicity`

## BitFields
- `name` of each bitfield has to be a valid C-language identifier
- `description` optional description. Will appear in the generated sources.
- `size_in_bits` size of the bitfield

</details>
<br></br>
  
# Platform settings
<details>

- [`endianness`](https://en.wikipedia.org/wiki/Endianness) : `little` or `big`  
- `eeprom_size` amount of EEPROM, allocated to the mEEM
- `eeprom_page_size` set to 0 for EEPROMs that can only write one byte at-a-time, like most MCU's on-chip ones. When using external EEPROMs, set it to the page size, defined in the EEPROM's datasheet.
- `page_aligned_blocks` list of block names, which you want aligned to EEPROM page boundaries. It's recommended to do so for wear-leveling blocks. Make sense only if `eeprom_page_size` > 0. An asterisk (`*`) means *all blocks*.
- `enter_critical_section_operation` define this one only if you use the mEEM in a pre-emptive environment. Your OS usually provides one.
- `exit_critical_section_operation` define this one only if you use the mEEM in a pre-emptive environment. Your OS usually provides one.
- `external_headers` list of header(s), containing declarations of `enter/exit critical section` operations
- `compiler_directives` collection of compiler-specific directives for packing and placement:  
  - `opening_pack_directive` required only for 16/32/64 bit CPUs. All generated mEEM data structures must be byte-aligned!
  - `closing_pack_directive` used in conjunction with `compiler_opening_pack_directive`
  - `pack_attribute` has the same purpose as `compiler_opening_pack_directive`/`compiler_closing_pack_directive`, but will be placed inline with generated type definitions. You either use directive or attribute, but never both at the same time.  
  - `block_placement_directives` a dict with blocks (names as keys), that requires special placement of defaults or/and caches:
    - `directive_for_defaults` optional. In some situations, you may need special placement of defaults.
    - `attribute_for_defaults` optional. You may use either an attribute or a directive, but never both at the same time
    - `directive_for_cache` optional. In some situations, you may need special placement of the cache.
    - `attribute_for_cache` optional. You may use either an attribute or a directive, but never both at the same time

</details>
<br></br>

# Checksum parameters
- `algo` name of the checksum algorithm, a string. **This is the only mandatory field.**  
- Other fields are algorithm-specific. The names and their types depend on the [implemented algo](../common/checksum_algo.py).  