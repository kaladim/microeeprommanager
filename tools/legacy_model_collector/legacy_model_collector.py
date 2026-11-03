"""mEEM legacy model collector.

Collects the data model and the platform settings of every release from the git tags, into a directory of models that the code
generator reads with --legacy-dir. In each tag it reads the files at the paths that the current ones have in the repository.
A tag that lacks them is ignored, and one whose data model has 'checksum_size' is skipped: a release of mEEM v1, whose images
have no identity header and are reset anyway. Tags with the same files make one model, named after the oldest of them.

With --depth, only the newest models are kept. Only a model that the firmware can migrate from counts: not one with the EEPROM
layout of the current model or of a newer model, which the generator would ignore.

The tool owns the directory: each run leaves exactly the models of that run, writes a file only when its content changed, and
refuses a non-empty directory that it didn't write.
"""

import os
import sys
import json
import shutil
import argparse
import tempfile
import subprocess
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Sequence, Tuple

sys.path.append(os.path.dirname(__file__))
sys.path.append(os.path.dirname(os.path.dirname(__file__)))
from colorama import Fore
from common.py.data_model import DataModel
from common.py.identity import layout_descriptor
from common.py.legacy_dir import DATAMODEL_FILE, SETTINGS_FILE
from common.py.platform_settings import PlatformSettings
from common.py.utils import attach_block_metadata

GIT = "git"
"""The git command line, which reads the tags"""

RECORD_FILE = "legacy-models.json"
"""The record of a run, at the top of the directory: its presence marks the directory as the tool's"""

UNSAFE_NAME_CHARACTERS = '/"<>|'
"""Characters of a tag name that a file name on Windows can't hold. git rejects the others in a tag name."""


class CollectorError(Exception):
    """An error that stops the tool, without a traceback"""


@dataclass
class Tag:
    name: str
    commit: str


@dataclass
class Model:
    """A distinct pair of files, in one or more tags"""

    directory: str
    """Its subdirectory: the name of its oldest tag"""

    datamodel: bytes
    settings: bytes
    tags: List[Tag] = field(default_factory=list)


def info(message: str):
    print(f"legacy_model_collector::info:: {message}")


def warning(message: str):
    print(f"{Fore.YELLOW}legacy_model_collector::warning:: {message}{Fore.RESET}")


def run_git(directory: str, *args: str, input: Optional[bytes] = None) -> subprocess.CompletedProcess:
    try:
        return subprocess.run([GIT, "-C", directory, *args], input=input, capture_output=True)
    except FileNotFoundError:
        raise CollectorError(f"'{GIT}' wasn't found: the tool needs the git command line on the PATH.")


def git(repository: str, *args: str, input: Optional[bytes] = None) -> bytes:
    """Runs git in the repository, and returns its output"""
    result = run_git(repository, *args, input=input)
    if result.returncode != 0:
        raise CollectorError(f"git {' '.join(args)} failed in '{repository}': {result.stderr.decode(errors='replace').strip()}")
    return result.stdout


def find_repository(datamodel_path: str, settings_path: str) -> Tuple[str, str, str]:
    """The repository that holds the current files, and their paths relative to its top level, with '/' as in git"""
    toplevels = []
    for path in (datamodel_path, settings_path):
        if not os.path.isfile(path):
            raise CollectorError(f"'{path}' doesn't exist.")

        result = run_git(os.path.dirname(os.path.abspath(path)), "rev-parse", "--show-toplevel")
        if result.returncode != 0:
            raise CollectorError(
                f"'{path}' isn't in a git repository, so there are no tags to collect the released models from. "
                "A build without them would drop migration, and every device it's installed on would reset its EEPROM."
            )
        toplevels.append(result.stdout.decode().strip())

    if os.path.normcase(os.path.realpath(toplevels[0])) != os.path.normcase(os.path.realpath(toplevels[1])):
        raise CollectorError(f"'{datamodel_path}' and '{settings_path}' are in different repositories: '{toplevels[0]}' and '{toplevels[1]}'.")

    repository = os.path.realpath(toplevels[0])
    paths = [os.path.relpath(os.path.realpath(path), repository).replace(os.sep, "/") for path in (datamodel_path, settings_path)]
    return repository, paths[0], paths[1]


def list_tags(repository: str, patterns: Sequence[str], exclude_patterns: Sequence[str]) -> List[Tag]:
    """The tags of commits, oldest first, that match the patterns and none of the exclude patterns, as git matches them.
    A tag of a tree or a blob is left out."""

    def for_each_tag(patterns: Sequence[str]) -> List[Tuple[str, ...]]:
        # The last --sort key is the primary one: the creation date, then the name.
        # Without a pattern, the prefix lists every tag: in git's patterns, '*' doesn't match a '/'.
        output = git(
            repository,
            "for-each-ref",
            "--sort=refname",
            "--sort=creatordate",
            "--format=%(refname:strip=2)%09%(objecttype)%09%(objectname)%09%(*objecttype)%09%(*objectname)",
            *([f"refs/tags/{pattern}" for pattern in patterns] or ["refs/tags/"]),
        )
        return [tuple(line.split("\t")) for line in output.decode("utf-8").splitlines() if line]

    excluded = {fields[0] for fields in for_each_tag(exclude_patterns)} if exclude_patterns else set()
    tags = []
    for name, object_type, object_name, peeled_type, peeled_name in for_each_tag(patterns):
        if name in excluded:
            continue
        if object_type == "commit":
            tags.append(Tag(name, object_name))  # A lightweight tag
        elif peeled_type == "commit":
            tags.append(Tag(name, peeled_name))  # An annotated tag
    return tags


def read_files(repository: str, requests: Sequence[str]) -> List[Optional[bytes]]:
    """The content of each '<commit>:<path>', or None where the commit has no file at the path, read with one git process"""
    if not requests:
        return []

    output = git(repository, "cat-file", "--batch", input="".join(f"{request}\n" for request in requests).encode("utf-8"))
    contents: List[Optional[bytes]] = []
    position = 0
    for _ in requests:
        end_of_line = output.index(b"\n", position)
        header = output[position:end_of_line].split(b" ")
        position = end_of_line + 1

        if header[-1] == b"missing" or header[-1] == b"ambiguous":
            contents.append(None)
            continue

        size = int(header[2])
        contents.append(output[position : position + size] if header[1] == b"blob" else None)
        position += size + 1  # The content is followed by a newline
    return contents


def directory_name(tag_name: str, taken: Sequence[str]) -> str:
    """The subdirectory of a model, named after its oldest tag. A name that collides, on a file system that ignores case too, gets -2, -3, ..."""
    base = "".join("_" if c in UNSAFE_NAME_CHARACTERS else c for c in tag_name)
    taken_folded = {name.casefold() for name in taken}
    name, n = base, 1
    while name.casefold() in taken_folded:
        n += 1
        name = f"{base}-{n}"
    return name


def collect_models(tags: Sequence[Tag], files: Sequence[Optional[bytes]], datamodel_path: str, settings_path: str) -> List[Model]:
    """Decides for each tag, in the order of the tags, whether its files make a model"""
    models: List[Model] = []
    by_content: Dict[Tuple[bytes, bytes], Model] = {}

    for i, tag in enumerate(tags):
        datamodel, settings = files[2 * i], files[2 * i + 1]

        if datamodel is None or settings is None:
            missing = " and ".join(path for path, content in ((datamodel_path, datamodel), (settings_path, settings)) if content is None)
            info(f"{tag.name}: ignored, it has no {missing}.")
            continue

        try:
            parsed = json.loads(datamodel.decode("utf-8"))
            if not isinstance(parsed, dict):
                raise ValueError("a data model is a JSON object")
        except ValueError as x:
            warning(f"{tag.name}: skipped, its {datamodel_path} isn't a valid data model: {x}")
            continue

        if "checksum_size" in parsed:
            info(f"{tag.name}: skipped, a release of mEEM v1: its data model has 'checksum_size', and the firmware resets its images.")
            continue

        model = by_content.get((datamodel, settings))
        if model is not None:
            model.tags.append(tag)
            info(f"{tag.name}: the same files as {model.directory}.")
            continue

        model = Model(directory_name(tag.name, [RECORD_FILE] + [m.directory for m in models]), datamodel, settings, [tag])
        by_content[(datamodel, settings)] = model
        models.append(model)
        info(f"{tag.name}: collected as {model.directory}.")

    return models


def write_if_changed(path: str, content: bytes):
    """Writes the file only when its content changed, so that a build tool that compares time stamps sees no change"""
    if os.path.isfile(path):
        with open(path, "rb") as f:
            if f.read() == content:
                return
    with open(path, "wb") as f:
        f.write(content)


def write_directory(output_dir: str, models: Sequence[Model], record: dict):
    """Leaves exactly the models in the directory. A non-empty directory without the record isn't the tool's, and is refused."""
    if os.path.exists(output_dir):
        if not os.path.isdir(output_dir):
            raise CollectorError(f"'{output_dir}' isn't a directory.")
        if os.listdir(output_dir) and not os.path.isfile(os.path.join(output_dir, RECORD_FILE)):
            raise CollectorError(f"'{output_dir}' isn't empty and has no {RECORD_FILE}, so it isn't the tool's directory. Nothing was changed in it.")
    os.makedirs(output_dir, exist_ok=True)

    # The record first, so that a run that stops half-way leaves a directory that the next run takes as the tool's
    write_if_changed(os.path.join(output_dir, RECORD_FILE), (json.dumps(record, indent=4) + "\n").encode("utf-8"))

    current = {model.directory.casefold() for model in models}
    for entry in os.scandir(output_dir):
        if entry.is_dir() and entry.name.casefold() not in current:
            shutil.rmtree(entry.path)

    for model in models:
        directory = os.path.join(output_dir, model.directory)
        os.makedirs(directory, exist_ok=True)
        write_if_changed(os.path.join(directory, DATAMODEL_FILE), model.datamodel)
        write_if_changed(os.path.join(directory, SETTINGS_FILE), model.settings)


def eeprom_layout(datamodel: bytes, settings: bytes) -> Optional[str]:
    """The descriptor of the EEPROM layout, by which the generator tells the models apart.
    None if the files have no layout to work out: then the generator rejects them, with an error that tells why."""
    with tempfile.TemporaryDirectory() as directory:
        datamodel_path, settings_path = os.path.join(directory, DATAMODEL_FILE), os.path.join(directory, SETTINGS_FILE)
        write_if_changed(datamodel_path, datamodel)
        write_if_changed(settings_path, settings)
        try:
            model = DataModel.load_from_file(datamodel_path, legacy=True)
            platform_settings = PlatformSettings.load_from_file(settings_path)
            attach_block_metadata(model, platform_settings)
            return layout_descriptor(model, platform_settings.endianness)
        except Exception:
            return None


def keep_newest(models: Sequence[Model], tags: Sequence[Tag], depth: int, current_layout: Optional[str]) -> List[Model]:
    """The newest models up to the depth, by their newest tags, in their order. A model with the EEPROM layout of the current model or
    of a newer model is left out and doesn't count: the generator would ignore it, and leave the firmware short of the depth.
    A model without a layout counts, so that the generator's error about it stops the build, as it does without a depth."""
    seen = {current_layout: "the current model"} if current_layout is not None else {}
    kept = []

    for model in sorted(models, key=lambda model: tags.index(model.tags[-1]), reverse=True):
        layout = eeprom_layout(model.datamodel, model.settings)

        if layout in seen:
            info(f"{model.directory}: left out, it has the same EEPROM layout as {seen[layout]}.")
        elif len(kept) == depth:
            info(f"{model.directory}: left out, it's behind the depth of {depth}.")
        else:
            kept.append(model)
            if layout is not None:
                seen[layout] = model.directory

    return [model for model in models if model in kept]


def collect(
    datamodel_path: str,
    settings_path: str,
    output_dir: str,
    patterns: Sequence[str] = (),
    exclude_patterns: Sequence[str] = (),
    depth: Optional[int] = None,
) -> List[Model]:
    """Collects the released models of the repository that holds the current files into the directory, and returns them.
    A depth of None keeps every model."""
    repository, datamodel_in_repository, settings_in_repository = find_repository(datamodel_path, settings_path)

    if git(repository, "rev-parse", "--is-shallow-repository").decode().strip() == "true":
        warning(
            f"'{repository}' is a shallow clone, so the tags outside the fetched history are missing. "
            "The build job has to fetch the release tags, e.g. with 'git fetch --tags'."
        )

    tags = list_tags(repository, patterns, exclude_patterns)
    files = read_files(repository, [f"{tag.commit}:{path}" for tag in tags for path in (datamodel_in_repository, settings_in_repository)])
    models = collect_models(tags, files, datamodel_in_repository, settings_in_repository)

    if depth is not None:
        with open(datamodel_path, "rb") as current_datamodel, open(settings_path, "rb") as current_settings:
            models = keep_newest(models, tags, depth, eeprom_layout(current_datamodel.read(), current_settings.read()))

    record = {
        "description": [
            "Written by the mEEM legacy model collector, which owns this directory and rewrites it in every run.",
            "Each subdirectory holds a data model released in the git tags, with its platform settings: the code generator reads them with --legacy-dir.",
        ],
        "datamodel": datamodel_in_repository,
        "settings": settings_in_repository,
        "tags": list(patterns),
        "exclude_tags": list(exclude_patterns),
        "depth": "all" if depth is None else depth,
        "models": [{"directory": model.directory, "tags": [{"tag": tag.name, "commit": tag.commit} for tag in model.tags]} for model in models],
    }
    write_directory(output_dir, models, record)

    if not tags:
        info("No tag matches, so there's no released model to migrate from.")
    info(f"{len(models)} legacy model(s) from {len(tags)} tag(s) in '{output_dir}'.")
    return models


def depth_argument(value: str) -> Optional[int]:
    """A positive number, or None for 'all'. A build without legacy models leaves out --legacy-dir: a depth of 0 is more likely a mistake."""
    if value == "all":
        return None
    if value.isdecimal() and int(value) > 0:
        return int(value)
    raise argparse.ArgumentTypeError(f"'{value}' is neither a positive number nor 'all'")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="mEEM legacy model collector: collects the released data models from the git tags, for the code generator's --legacy-dir",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )

    parser.add_argument("datamodel", help="Path to the current datamodel.json, in the repository whose tags mark the releases")
    parser.add_argument("settings", help="Path to the current platform_settings.json")
    parser.add_argument("output_dir", help="The directory of models to write. Created if it doesn't exist.")
    parser.add_argument(
        "--tags", action="append", default=[], metavar="GLOB", help="Only the tags that match the glob, e.g. 'v*'. Repeatable. Default: every tag."
    )
    parser.add_argument("--exclude-tags", action="append", default=[], metavar="GLOB", help="Leaves out the tags that match the glob. Repeatable.")
    parser.add_argument(
        "--depth",
        type=depth_argument,
        metavar="N",
        help="Only the newest N legacy models that the firmware can migrate from: a positive number, or 'all'. Default: all.",
    )
    args = parser.parse_args()

    try:
        collect(args.datamodel, args.settings, args.output_dir, args.tags, args.exclude_tags, args.depth)
        exit_code = 0
    except CollectorError as x:
        print(f"{Fore.RED}legacy_model_collector::error:: {x}{Fore.RESET}")
        exit_code = 1

    sys.exit(exit_code)
