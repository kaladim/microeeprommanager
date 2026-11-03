# Legacy model collector

Collects the data model and the platform settings of every release from the git tags, into a directory of models. The [code generator](../meem_config_gen/README.md) reads it with `--legacy-dir`, and generates the migration from each of these releases. Migration is only as good as that list of released models, so the build collects it, instead of a developer keeping it by hand.

Run it in the build, before the generator: in an MPLAB X project, in the pre-build step.

## Usage
[Make sure](../../README.md#prerequisites) `uv` is installed, and the git command line is on the `PATH`.

From the project root execute:
```bash
uv run ./tools/legacy_model_collector/legacy_model_collector.py <DATA-MODEL> <PF-SETTINGS> <OUTPUT-DIR> [--tags <GLOB>]... [--exclude-tags <GLOB>]... [--depth <N>]
```

### Example
```bash
uv run ./tools/legacy_model_collector/legacy_model_collector.py \
    ./config/datamodel.json \
    ./config/platform_settings.json \
    ./build/meem_legacy_models \
    --tags "v*"

uv run ./tools/meem_config_gen/meem_config_gen.py \
    ./config/datamodel.json \
    ./config/platform_settings.json \
    ./config/generated \
    --legacy-dir ./build/meem_legacy_models
```

### Parameters

| Parameter        | Required | Description                                                                                                 |
| ---------------- | -------- | ----------------------------------------------------------------------------------------------------------- |
| `<DATA-MODEL>`   | ✅        | The current `datamodel.json`, in the repository whose tags mark the releases: the file the generator gets  |
| `<PF-SETTINGS>`  | ✅        | The current `platform_settings.json`                                                                        |
| `<OUTPUT-DIR>`   | ✅        | The directory of models to write. Created if it doesn't exist.                                              |
| `--tags`         | ⚪        | Only the tags that match the glob, e.g. `v*`. Repeatable. By default every tag.                             |
| `--exclude-tags` | ⚪        | Leaves out the tags that match the glob, e.g. a release whose model the generator rejects. Repeatable.      |
| `--depth`        | ⚪        | Only the [newest `N` legacy models](#history-depth): a positive number, or `all`. By default `all`.         |

The globs match tag names as `git for-each-ref refs/tags/<GLOB>` does: `*` doesn't match a `/`, so `release/*` is needed for `release/2.0`.

## What it collects
In each tag, the tool reads the files at the paths that `<DATA-MODEL>` and `<PF-SETTINGS>` have in the repository. It reads the tags in the order of their creation:

| Tag | Outcome |
| --- | --- |
| Lacks either file | Ignored |
| Its data model isn't valid JSON | Skipped, with a warning |
| Its data model has `checksum_size` | Skipped: a release of mEEM v1, whose images have no identity header, so the firmware resets them anyway |
| Both files are byte for byte those of an earlier tag | Added to that model's tags |
| Otherwise | A new model, named after this tag |

A tag in which the files had other paths is ignored, too. Its model can still be given to the generator by hand: with `--legacy`, or in a committed directory of the same format, as a second `--legacy-dir`.

## History depth
`--depth <N>` keeps the newest `N` models, to save the flash of the older ones. Devices on a release behind the depth reset their EEPROM when they update. Without `--depth`, or with `all`, every model is kept, as the table above says.

`N` counts only what the firmware can migrate from, so it's what the generator gets to `MEEM_LEGACY_MODEL_COUNT`:

| Tag or model | Counts? |
| --- | --- |
| A tag left out by the globs, or ignored or skipped as in the table above | No |
| The tags of one model | Once. The model is as new as its newest tag. |
| A model with the same EEPROM layout as the current model | No, and it's left out: the generator would ignore it, so a build at a release tag would spend the depth on itself |
| A model with the same EEPROM layout as a newer model that is kept, e.g. only its default values differ | No, and it's left out, for the same reason |
| A model that the generator rejects | Yes: the error stops the build, as it does without a depth. `--exclude-tags` leaves it out, and the next older model takes its place. |

A model keeps its name whatever the depth. Fewer than `N` models are no error.

## The directory of models
```
<OUTPUT-DIR>/
├── legacy-models.json
├── v2.4/
│   ├── data-model.json
│   └── platform-settings.json
└── v2.6/
    ├── data-model.json
    └── platform-settings.json
```
- One subdirectory per model, named after its oldest tag. The characters `/`, `"`, `<`, `>` and `|` become `_`, and a name that then collides gets `-2`, `-3` and so on.
- The two files have these fixed names in every model, whatever the current files are called in the repository.
- `legacy-models.json` records the run: the paths read, the globs, the depth, and the tags and commits of each model. It holds no date.
- The directory is a build artifact, not to be committed. The tool owns it: each run leaves exactly the models of that run, and writes a file only when its content changed. A directory that isn't empty and has no `legacy-models.json` is refused, so a wrong path deletes nothing.
- It may be empty: the first release has no earlier one to migrate from.

## Errors and warnings
- **Errors**, with exit code 1: git isn't installed; `<DATA-MODEL>` isn't in a git repository, because a build without the tags would silently drop migration; a file doesn't exist, or the two are in different repositories; `<OUTPUT-DIR>` isn't the tool's.
- **Warning**: the repository is a shallow clone, so tags outside the fetched history are missing. The build job has to fetch the release tags, e.g. with `git fetch --tags`.
