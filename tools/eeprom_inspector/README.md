# EEPROM Inspector

A browser-based tool that renders a human-readable report from an EEPROM binary image.
Raw binary, [Intel HEX](https://en.wikipedia.org/wiki/Intel_HEX) and [Motorola S-record](https://en.wikipedia.org/wiki/Motorola_S-record) formats are accepted.
Requires the same EEPROM data model and platform settings that were used for the **mEEM** configuration when the image was produced.

It is a pure client-side web application — no Python, no build step and no server. All parsing and validation runs in your browser.

## Features
- Shows the content of all available data instances within each block
- Validates the checksum of each data instance and tags the instance as `Valid` or `Invalid`
- Tags the `Most recent` instance in wear-leveling blocks
- Foldable blocks
- Switches any value between hexadecimal and decimal representation — per value, per block, or globally

## Usage
Make sure your web browser has `JavaScript` enabled and simply open [`eeprom_inspector.html`](./eeprom_inspector.html) in it.

1. On the **Input files** tab, provide the four files (via the *Choose file* buttons or by drag&drop):

   | File                       | Description                                                    |
   | -------------------------- | -------------------------------------------------------------- |
   | EEPROM image               | EEPROM dump (`*.bin`, `*.hex`, `*.s19`)                        |
   | `datamodel.json`           | mEEM data model configuration                                  |
   | `platform_settings.json`   | mEEM platform settings                                         |
   | `checksum_parameters.json` | Checksum parameters                                            |

2. The report is generated automatically once all four files are loaded. Open the **Report** tab (or click *View report*).
3. In the report:
   - Click a value to toggle it between **hex** and **dec**.
   - Use a block's *Show all in DEC/HEX* button to switch every value in that block.
   - Use the toolbar's *Show all in DEC/HEX* to switch every value in the whole report, and *Expand all* / *Collapse all* to fold the blocks.

> **Note:** The Font Awesome and WebAwesome libraries are loaded from public CDNs, so an internet connection is required the first time the page is opened.

## Implementation
The application is intentionally dependency-light and framework-free.

| File                     | Responsibility                                                    |
| ------------------------ | ----------------------------------------------------------------- |
| `eeprom_inspector.html`  | Page shell, CDN links (Font Awesome / WebAwesome)                 |
| `app.js`                 | UI: file selection, rendering, folding and hex/dec switching      |
| `styles.css`             | Dark theme layered on WebAwesome                                  |

The business logic lives in `core/`, split by responsibility. These are classic
scripts that each contribute to a shared global `EEPROM` namespace and are loaded
in dependency order (as listed) — no bundler or ES modules, so it works from `file://`.

| File                       | Responsibility                                                       |
| -------------------------- | -------------------------------------------------------------------- |
| `core/data_model.js`       | Data-type/management enums, JSON parsing, EEPROM-layout computation  |
| `core/wear_leveling.js`    | "Most recent" instance finder for wear-leveling blocks              |
| `core/crc.js`              | CRC (crcmod-exact) and the checksum-algorithm wrapper               |
| `core/image_loader.js`     | Image-format dispatch + sparse→contiguous byte helper               |
| `core/intel_hex.js`        | Intel HEX parser                                                    |
| `core/srecord.js`          | Motorola S-record (S19/S28/S37) parser                             |
| `core/report_builder.js`   | Report/view-model builder and top-level orchestration              |
