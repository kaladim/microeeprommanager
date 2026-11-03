# EEPROM Inspector

A browser-based tool that renders a human-readable report from an EEPROM binary image.
Raw binary, [Intel HEX](https://en.wikipedia.org/wiki/Intel_HEX) and [Motorola S-record](https://en.wikipedia.org/wiki/Motorola_S-record) formats are accepted.
Requires the same EEPROM data model and platform settings that were used for the **mEEM** configuration when the image was produced.

It is a pure client-side web application — no Python, no build step and no server. All parsing and validation runs in your browser.

## Features
- Checks the identity header at the start of the image against that of the data model, and shows whether it matches, is the intact header of another EEPROM layout, or is garbage
- Shows the content of all available data instances within each block, with the block's identity and data identity
- Validates the checksum of each data instance (CRC-16/IBM-3740, in the target's byte order) and tags the instance as `Valid` or `Invalid`
- Tags the `Most recent` instance in wear-leveling blocks
- Foldable blocks
- Switches any value between hexadecimal and decimal representation — per value, per block, or globally

## Usage
Make sure your web browser has `JavaScript` enabled and simply open [`index.html`](./index.html) in it.

1. On the **Input files** tab, provide the three files (via the *Choose file* buttons or by drag&drop):

   | File                     | Description                                                    |
   | ------------------------ | -------------------------------------------------------------- |
   | EEPROM image             | EEPROM dump (`*.bin`, `*.hex`, `*.s19`)                        |
   | `datamodel.json`         | mEEM data model configuration                                  |
   | `platform_settings.json` | mEEM platform settings                                         |

   There are no checksum parameters: every data model uses the same checksum. A data model's `checksum_size`, which v1's configurator saved, is ignored.

2. The report is generated automatically once all three files are loaded. Open the **Report** tab (or click *View report*).
3. In the report:
   - Click a value to toggle it between **hex** and **dec**.
   - Use a block's *Show all in DEC/HEX* button to switch every value in that block.
   - Use the toolbar's *Show all in DEC/HEX* to switch every value in the whole report, and *Expand all* / *Collapse all* to fold the blocks.

> **Note:** The Font Awesome and WebAwesome libraries are loaded from public CDNs, so an internet connection is required the first time the page is opened.

## Implementation
The application is intentionally dependency-light and framework-free.

| File                     | Responsibility                                                    |
| ------------------------ | ----------------------------------------------------------------- |
| `index.html`             | Page shell, CDN links (Font Awesome / WebAwesome)                 |
| `app.js`                 | UI: file selection, rendering, folding and hex/dec switching      |
| `styles.css`             | Dark theme layered on WebAwesome                                  |

The business logic lives in `core/`, split by responsibility. These are classic
scripts that each contribute to a shared global `EEPROM` namespace and are loaded
in dependency order (as listed) — no bundler or ES modules, so it works from `file://`.

| File                       | Responsibility                                                       |
| -------------------------- | -------------------------------------------------------------------- |
| `core/config-loader.js`    | JSON parsing, the blocks' EEPROM-layout metadata                     |
| `core/wear-leveling.js`    | "Most recent" instance finder for wear-leveling blocks              |
| `core/image-loader.js`     | Image-format dispatch + sparse→contiguous byte helper               |
| `core/intel-hex.js`        | Intel HEX parser                                                    |
| `core/srecord.js`          | Motorola S-record (S19/S28/S37) parser                             |
| `core/report-builder.js`   | Identity header check, report/view-model builder and top-level orchestration |

The code it shares with the [configurator](../configurator/README.md) lives in [`tools/common/js/`](../common/js/), which the page loads first, in a global `MeemCommon` namespace, the same way:

| File                       | Responsibility                                                       |
| -------------------------- | -------------------------------------------------------------------- |
| `data-model.js`            | Data-type/management enums, sizes, the offsets of the blocks         |
| `crc.js`                   | CRC-16/IBM-3740 and CRC-32/ISO-HDLC                                  |
| `identity.js`              | Descriptors, identities and the identity header                      |

The business logic is covered by tests in `test/` and `tools/common/js/test/`, run with the built-in test runner of [Node.js](https://nodejs.org) (`node --test`, from this directory). They are part of the CMake test suite, if Node.js is installed. Their EEPROM images come from the [image generator](../eeprom_image_gen/README.md) and are committed under [`test/vectors/images/`](../../test/vectors/images/), listed in [`image_vectors.json`](../../test/vectors/image_vectors.json), so the tests need no Python. The generator's tests fail if one of them is stale; regenerate them with `uv run ./tools/eeprom_image_gen/make_image_vectors.py`.
