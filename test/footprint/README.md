# Footprint harness

Builds mEEM for XC8 targets and attributes the linker map to mEEM, so that v1 and v2 can be compared on real compilers.
It measured the v1 baseline below, and measures v2 against it ([Design-v2.md](../../Design-v2.md), 8.6).
It needs XC8 and the device packs, so it is not part of CTest.

## Usage
```bash
uv run test/footprint/footprint.py
```
Options:
- `--target NAME`: build only this target (repeatable).
- `--crc table|bitwise`: overrides `crc_implementation` of the targets' platform settings (default: theirs, which is `table`). The output directory of each target gets the value as a suffix.
- `--xc8 PATH`, `--packs DIR`: the compiler and the device pack directory. Defaults: `xc8-cc` from `PATH`, else XC8 v4.00, and the packs of MPLAB X v6.35.
- `--out DIR`: output directory, default `build/footprint/`. Each target gets the generated config, the map, `xc8.log` and `footprint.json`.
- `--map FILE`: only attribute an existing map, for example one of a product build.

## Targets
| Target | MCU | Options | Application |
| --- | --- | --- | --- |
| `pic16f1825-example` | PIC16F1825 | `-O1`, as in `mEEM-demo.X` | The v1 example: its data model, apps and EEPROM driver |
| `pic16f1825-example-os` | PIC16F1825 | `-Os` | The same, optimized for size |
| `pic18f46k22-production-like` | PIC18F46K22 | `-Os`, as the release build of a production application | [`production_like/`](production_like/): that application's storage shape with a minimal application |
| `pic18f46k22-production-like-migration` | PIC18F46K22 | `-Os` | The same application, with the change of [Design-v2.md](../../Design-v2.md), 9.3 applied to `Block09` (`production_like/eeprom_datamodel_migration.json`), and the unchanged model as its legacy model |

All targets use `-std=c99 -mstack=compiled -fcacheconst=off`.
With `-fcacheconst` (the default), XC8 copies `const` objects into RAM when RAM is free, so the RAM figures would depend on the rest of the build.

`production_like/eeprom_datamodel.json` is derived from the 2.3 data model of the production application, a shipped product that uses mEEM v1. It keeps the blocks, management types, instance counts, recovery strategies, parameter types, multiplicities, default values and bitfield widths, but has neutral names and no descriptions.
`production_like/eeprom_datamodel_migration.json` changes its `Block09` as 9.3 changes the production application's `ConfigCommon`: `p02` (uint8) is replaced by `p06` (uint16), `p04` widens to uint32, and `p07` (uint8) is new. Both models are laid out with `platform_settings_xc8.json`. The `-MigrationExample` test configuration migrates between them on the host.
The application in `main.c` calls the whole mEEM API except the MultiProfile part, and `footprint_user.c` holds minimal user code. All of its driver state is volatile, so the compiler can't fold any path of the core away.

## Method
The script runs the code generator, builds with `xc8-cc` and parses the map:
- **Code.** XC8 gives every function its own psect. At `-Os`, code factored out by procedural abstraction stays in that psect behind the function's end label, and on PIC16 several functions may share a psect. So every code psect is counted once and split among its functions in proportion to their sizes up to their end labels.
- **Objects.** `const` objects are sized by their end labels. RAM objects have none, so their size is the distance to the next symbol in their psect.
- **Categories.**
  - *core*: functions of `src/core`.
  - *generated*: `MEEM_ValidateConfiguration()`, the accessor functions, and with legacy models the migration routines, their conversion helpers and the dispatcher `MEEM_MigrateBlock()`.
  - *user*: the driver and callbacks. In v1 also the checksum, which v2 builds into the core.
  - *memcpy/memset*: library code that the application shares.
  - *config*: `MEEM_block_config` and `MEEM_defaults_*`, and with legacy models their tables `MEEM_legacy_*`.
  - *core RAM*: the other `MEEM_` RAM objects.
  - *caches*: `MEEM_cache_*`.

Not attributed: the compiled stack, which is reported for the whole program because XC8 overlays function locals; runtime helpers such as `___fladd`; and the startup code.

Limit: on PIC16 at `-Os`, procedural abstraction shares code between mEEM and the application, so the split into categories is only approximate. To compare mEEM versions there, use the difference of the whole-build totals, since the application stays the same.

## v1 baseline
Measured on 2026-10-27 with XC8 v4.00, v1 at `88b2d39`. PIC16 code is in words, PIC18 code in bytes.

| | PIC16F1825, v1 example | PIC16F1825, v1 example | PIC18F46K22, production-like | Production application 2.3 as shipped |
| --- | --- | --- | --- | --- |
| Options | `-O1` (example project) | `-Os` | `-Os` | `-Os`, const caching on |
| mEEM core code | 2,510 words | 2,026 words | 2,894 B | 2,848 B |
| Generated code | 137 words (inline accessors) | 1 word | 2 B | 2 B |
| Config constants in flash | 71 words | 71 words | 165 B | 159 B |
| mEEM core RAM | 39 B | 39 B | 103 B | 103 B |
| Parameter caches (RAM) | 33 B | 33 B | 523 B | n/a |
| Config constants cached in RAM by XC8 | off | off | off | 18 B |
| Compiled stack, whole build | 57 B | 51 B | 62 B | 141 B |
| Whole build: program, data | 4,294 words, 152 B | 3,433 words, 146 B | 3,530 B, 700 B | n/a |

- The harness's mEEM core code is within 2 % of the production application's shipped build. The harness also calls `MEEM_Suspend()`, `MEEM_DeInit()`, `MEEM_GetBlockStatus()` and `MEEM_RestoreDefaults()`, which that application doesn't. Its column comes from its own map (`--map`), so it has no whole-build totals.
- mEEM core RAM is `13 + 2 × blocks + C + max(D)` bytes; on PIC18 that's 13 + 24 + 66 = 103 B. The caches are the parameters themselves and don't change in v2.
- On the PIC16F1825, v1's mEEM core takes 25-31 % of the 8 K-word flash, which makes it the tighter target.

## v2 measurements
The footprint checkpoints of [Development-plan.md](../../Development-plan.md), section 11, each against the v1 baseline above (F0). There is no budget: each result is for the user to judge.

### F1: fixed checksum (WP1.3)
Measured on 2026-10-30 with XC8 v4.00, v2 at WP1.3. The instance checksum is the CRC-16/IBM-3740 built into the core, with its asynchronous stages ([Design-v2.md](../../Design-v2.md), 8.4), in both built-in implementations (`--crc bitwise`, `--crc table`). `hardware` isn't measured, since neither MCU has a CRC unit.

PIC18F46K22, production-like, `-Os`. Its v1 user code held a bitwise CRC-16/IBM-3740 (112 B), which v2 builds into the core, so core and user code are compared together:

| | v1 (F0) | `bitwise` | `table` (default) |
| --- | --- | --- | --- |
| mEEM core code | 2,894 B | 2,998 B | 2,932 B |
| User code | 220 B | 108 B | 108 B |
| Core and user code | 3,114 B | 3,106 B (−8 B) | 3,040 B (−74 B) |
| CRC table | | | 512 B |
| Config constants in flash | 165 B | 165 B | 177 B |
| mEEM core RAM | 103 B | 103 B | 103 B |
| Compiled stack, whole build | 62 B | 62 B | 63 B |
| Whole build: program, data | 3,530 B, 700 B | 3,522 B, 700 B (−8 B) | 3,980 B, 701 B (+450 B) |

PIC16F1825, v1 example. Its v1 checksum was a CRC-8 with a 256-entry table in the example's `CRC.c`, which the harness doesn't attribute, so the whole-build totals are compared:

| | v1 (F0) | `bitwise` | `table` (default) |
| --- | --- | --- | --- |
| `-O1`: whole build, program and data | 4,294 words, 152 B | 4,062 words (−232), 153 B | 4,568 words (+274), 153 B |
| `-O1`: mEEM core code | 2,510 words | 2,570 words | 2,564 words |
| `-Os`: whole build, program and data | 3,433 words, 146 B | 3,184 words (−249), 147 B | 3,697 words (+264), 147 B |
| CRC table | | | 512 words |
| mEEM core RAM | 39 B | 40 B | 40 B |

- `bitwise` keeps the PIC18 build at v1's size: the core grows by 104 B, about the CRC routine that left the user code (112 B).
- `table` costs its 512 B, plus 12 B of configuration on the PIC18: with the table, XC8 moves the `const` objects from `smallconst` to `mediumconst`, where a pointer to `const` takes 3 bytes, so each of the 12 block configurations grows by 1 B. The compiled stack grows by 1 B too.
- On the PIC16F1825, each byte of a `const` table takes a program word (`RETLW`), so the table takes 512 words, 6 % of the flash.
- The example's core RAM grows by 1 B because the work buffer holds a 2-byte checksum instead of v1's 1 byte (C = 2, D41).
- At `-Os` on the PIC16, procedural abstraction moves code between the core and the rest of the build (core code: 1,505 words with `table`, 1,820 words with `bitwise`), so only the totals compare.
- `hardware` was built once outside the harness, on the PIC18 target with a stub driver, to check that XC8 compiles it, the generated self-test included. With `NDEBUG`, the core code is 3,046 B: 48 B more than with `bitwise`, because the read and write tasks gain the wait stages and the handling of a failing unit (+58 B), while the core's step of the unit (102 B) replaces the routine (112 B). The driver comes on top.

### F2: identity header (WP2.7)
Measured on 2026-10-30 with XC8 v4.00, v2 at WP2.7: the identity header at offset 0, rewrites, the commit and the reset, without legacy models ([Design-v2.md](../../Design-v2.md), 8.1, 8.3). It's compared with F1 in the same CRC implementation, so the CRC-16 cancels out, as it does in the steady-state estimate of [Design-v2.md](../../Design-v2.md), 8.6: about v1 + 0.2-0.3 KB (1.07-1.1×) on the PIC18F46K22.

PIC18F46K22, production-like, `-Os`:

| | F1 `bitwise` | F2 `bitwise` | F1 `table` | F2 `table` (default) |
| --- | --- | --- | --- | --- |
| mEEM core code | 2,998 B | 3,436 B (+438, 1.15×) | 2,932 B | 3,354 B (+422, 1.14×) |
| User code | 108 B | 108 B | 108 B | 108 B |
| Identity header (`MEEM_identity_header`) | | 6 B | | 6 B |
| Config constants in flash | 165 B | 165 B | 177 B | 177 B |
| mEEM core RAM | 103 B | 103 B | 103 B | 103 B |
| Compiled stack, whole build | 62 B | 62 B | 63 B | 63 B |
| Whole build: program, data | 3,522 B, 700 B | 3,966 B (+444), 700 B | 3,980 B, 701 B | 4,408 B (+428), 701 B |

Where the core code grows, with `table`:

| Work package | Core code | Functions |
| --- | --- | --- |
| WP2.5 Rewrites | +180 B | `MEEM_StartWriteOperationCachedBlock()` +98, `MEEM_WriteFinalize()` +92, `MEEM_InitializeWearLevelingBlock()` −10 |
| WP2.6 Commit | +108 B | `MEEM_StartCommit()` 50, `MEEM_IsRewritePending()` 22, `MEEM_ProcessCurrentRequest()` +18, `MEEM_TryProcessNextRequest()` +16, `MEEM_IsBusy()` +2 |
| WP2.7 Header check and reset | +134 B | `MEEM_IsResetNeeded()` 88, `MEEM_Init()` +46 |

PIC16F1825, v1 example, whole builds:

| | F1 `bitwise` | F2 `bitwise` | F1 `table` | F2 `table` (default) |
| --- | --- | --- | --- | --- |
| `-O1`: whole build, program and data | 4,062 words, 153 B | 4,362 words (+300), 153 B | 4,568 words, 153 B | 4,868 words (+300), 153 B |
| `-O1`: mEEM core code | 2,570 words | 2,864 words (+294) | 2,564 words | 2,858 words (+294) |
| `-Os`: whole build, program and data | 3,184 words, 147 B | 3,420 words (+236), 147 B | 3,697 words, 147 B | 3,925 words (+228), 147 B |
| mEEM core RAM | 40 B | 40 B | 40 B | 40 B |

- The steady state costs 422-438 B of core code on the PIC18, 1.14-1.15× F1's core, against the estimate of 0.2-0.3 KB (1.07-1.1×). The rewrites of all instances are the largest part.
- RAM doesn't grow on any target: `rewrite_all`, `remaining_instances` and `identity_pending` take spare bits, and the compiled stack stays as at F1.
- On the PIC16F1825 at `-O1`, the 300 words are 3.7 % of the flash.
- The header is compared in a loop: `memcmp()` added 88 B of library code on the PIC18, which the harness doesn't attribute (only `memcpy()` and `memset()` are), so only the whole-build total showed it.

### F3: migration (WP3.6)
Measured on 2026-10-31 with XC8 v4.00, v2 at WP3.6, on the target `pic18f46k22-production-like-migration`: the production-like model with the change of [Design-v2.md](../../Design-v2.md), 9.3 applied to `Block09`, migrating from the unchanged model (8.2). It has no MultiProfile block, so pass 2 isn't linked. It's compared with the steady state of the same build, `pic18f46k22-production-like` (F2 − 8 B since WP3.5), and with F1 in the same CRC implementation. The estimate of [Design-v2.md](../../Design-v2.md), 8.6: about 0.8-1.0 KB more than the steady state, 1.35-1.45× v1's code, routines and legacy tables included.

PIC18F46K22, `-Os`:

| | Steady state `bitwise` | Migration `bitwise` | Steady state `table` | Migration `table` (default) |
| --- | --- | --- | --- | --- |
| mEEM core code | 3,428 B | 4,156 B (+728) | 3,346 B | 4,170 B (+824) |
| Generated code | 2 B | 124 B (+122) | 2 B | 132 B (+130) |
| Legacy tables (`MEEM_legacy_*`) | | 40 B | | 42 B |
| Config constants in flash, legacy tables included | 165 B | 209 B (+44) | 177 B | 223 B (+46) |
| Core, generated code and legacy tables, against F1's core | 1.14× | 1.44× | 1.14× | 1.48× |
| mEEM core RAM | 103 B | 103 B | 103 B | 103 B |
| Parameter caches | 523 B | 527 B (+4) | 523 B | 527 B (+4) |
| Compiled stack, whole build | 63 B | 76 B (+13) | 64 B | 78 B (+14) |
| Whole build: program, data | 3,958 B, 701 B | 4,852 B (+894), 718 B | 4,400 B, 702 B | 5,400 B (+1,000), 720 B |

Where the migration adds to the steady state, with `table`:

| Part | Size | Functions and objects |
| --- | --- | --- |
| Legacy readers | 488 B | `MEEM_ReadLegacyInstance()` 202, `MEEM_CopyLegacyBlock()` 158, `MEEM_FetchLegacyInstance()` 128 |
| Pass 1 | 210 B | `MEEM_Init()` +108, `MEEM_FillFromLegacySource()` 102 |
| Header compare | 92 B | `MEEM_FindLegacyModel()` 84, `MEEM_IdentifyStoredModel()` +8 |
| Routine and dispatcher (generated) | 130 B | `MEEM_Migrate_Block09_67C3()` 104, `MEEM_MigrateBlock()` 26 |
| Legacy tables | 42 B | `MEEM_legacy_sources_0` 12 (1 B per current block), `MEEM_legacy_blocks_0` 20 (5 B per copied or converted block), `MEEM_legacy_models` 10 (the 6 B header and two pointers) |
| Functions that the migration doesn't change | 34 B | 12 functions, 2-8 B each, such as `MEEM_ReadOperationTask()` +8. The `bitwise` build doesn't show it. |

- The migration adds 890 B (`bitwise`) to 996 B (`table`) of code and tables, within the estimate of 0.8-1.0 KB. The steady state already came out above its own estimate (F2), so the whole is 1.44-1.48× F1's core, against 1.35-1.45×.
- The readers are the largest part. Each further legacy model adds its tables and a dispatcher case, and a routine only for a source data identity that no other legacy model has (D31).
- mEEM core RAM doesn't grow: `migrated` takes a spare status bit, and no legacy instance that the core reads is larger than the work buffer. The compiled stack grows by 13-14 B with the legacy read path, in the range the spike measured (+9 to +14 B). The caches grow by the 4 B that the change adds to `Block09`.
- Pass 2 isn't linked in this target. WP3.5 measured it on a variant with `Block11` as a MultiProfile block in both models: about 414 B more (`MEEM_MigratePass2()` 276, `MEEM_IsInPass2()` 74, `MEEM_WriteProfile()` 46, `MEEM_InitializeMultiProfileBlock()` 18) and 16 B more compiled stack.
- On the device, the migration rewrites 209 B and then writes the header, as 9.3 describes for the production application. The `-MigrationExample` test configuration checks this on the host.

### Final (WP6.3)
Measured on 2026-11-02 with XC8 v4.00, v2 at WP6.2, on every target and in both built-in CRC implementations, against the v1 baseline (F0). It comes before WP5.1 and WP5.2 ([Development-plan.md](../../Development-plan.md), section 10), which concern the build and not the target's code.

PIC18F46K22, `-Os`. Steady state is `pic18f46k22-production-like`, migration is `pic18f46k22-production-like-migration`:

| | v1 (F0) | Steady state `bitwise` | Steady state `table` (default) | Migration `bitwise` | Migration `table` (default) |
| --- | --- | --- | --- | --- | --- |
| mEEM core code | 2,894 B | 3,428 B | 3,346 B | 4,156 B | 4,170 B |
| Generated code | 2 B | 2 B | 2 B | 124 B | 132 B |
| User code | 220 B | 108 B | 108 B | 108 B | 108 B |
| CRC table | | | 512 B | | 512 B |
| Identity header | | 6 B | 6 B | 6 B | 6 B |
| Config constants in flash, legacy tables included | 165 B | 165 B | 177 B | 209 B | 223 B |
| mEEM core RAM | 103 B | 103 B | 103 B | 103 B | 103 B |
| Parameter caches | 523 B | 523 B | 523 B | 527 B | 527 B |
| Compiled stack, whole build | 62 B | 63 B | 64 B | 76 B | 78 B |
| Whole build: program | 3,530 B | 3,958 B (+428, 1.12×) | 4,400 B (+870, 1.25×) | 4,852 B (+1,322, 1.37×) | 5,400 B (+1,870, 1.53×) |
| Whole build: data | 700 B | 701 B | 702 B | 718 B | 720 B |

PIC16F1825, v1 example, `pic16f1825-example` and `pic16f1825-example-os`:

| | v1 (F0) | `bitwise` | `table` (default) |
| --- | --- | --- | --- |
| `-O1`: whole build, program and data | 4,294 words, 152 B | 4,364 words (+70), 154 B | 4,870 words (+576), 154 B |
| `-O1`: mEEM core code | 2,510 words | 2,866 words | 2,860 words |
| `-O1`: compiled stack, whole build | 57 B | 58 B | 58 B |
| `-Os`: whole build, program and data | 3,433 words, 146 B | 3,419 words (−14), 148 B | 3,927 words (+494), 148 B |
| `-Os`: compiled stack, whole build | 51 B | 52 B | 52 B |
| CRC table | | | 512 words |
| mEEM core RAM | 39 B | 40 B | 40 B |

The estimates of [Design-v2.md](../../Design-v2.md), 8.6, came from a spike of another concept, and the measurements have replaced them there:

| On the PIC18F46K22 | Estimate | Measured |
| --- | --- | --- |
| Steady state, flash | v1 + 0.2-0.3 KB, 1.07-1.1× | +414 B (`table`) to +430 B (`bitwise`) of core code, 1.14× F1's core |
| With a legacy model, flash | 0.8-1.0 KB more, 1.35-1.45× | 890 B (`bitwise`) to 996 B (`table`) more, 1.44-1.48× F1's core |
| Steady state, compiled stack | About +0 B | +1 B (`bitwise`) to +2 B (`table`) |
| With a legacy model, compiled stack | +9 to +14 B | +13 B (`bitwise`) to +14 B (`table`) more |
| mEEM core RAM | +0 B | +0 B |

- The steady state is the only figure above its estimate, and that carries through to the 1.44-1.48× of the migration. The migration part itself is within its estimate.
- Since F3, the PIC18 builds haven't changed by a byte: phases 4 to 6 don't touch the core. The steady state is 8 B below F2, from WP3.5.
- Since F2, the PIC16 builds are 2 words larger at `-O1`, and at `-Os` 2 words larger with `table` and 1 word smaller with `bitwise`. Their data grows by 1 B, and so does their compiled stack.
- The example's core RAM is 1 B above v1 because the work buffer holds a 2-byte checksum instead of 1 byte. It's the only RAM growth outside the stack, apart from the 4 B that the change adds to `Block09`'s cache.
- `bitwise` keeps the PIC16 example at about v1's size, 1.02× at `-O1` and below v1 at `-Os`, because the example's v1 checksum was a CRC-8 with a 256-entry table. `table` costs 6 % of that MCU's flash.
- As a check on the harness, MPLAB X's generated makefiles built the example project with the same result at `-O1`: 4,870 words, 154 B.
- Not measured here: `hardware`, since neither MCU has a CRC unit (F1 has a one-off build), and pass 2, since no target has a MultiProfile block in it (F3 has WP3.5's variant, about 414 B and 16 B of stack more).
