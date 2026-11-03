"""Legacy models and their source tables: what MEEM_Init() migrates from, worked out by the generator.

Each current block is paired with the legacy block of the same name, its source, and their descriptors are compared:
- the same block descriptor at the same offset: unchanged, the block is initialized as in v1;
- the same data descriptor: the core copies the block from its entry in the legacy block array;
- another data descriptor: a generated routine converts the block from its entry in the legacy block array;
- no source: the block gets its defaults.

The legacy block array holds only the sources that the core reads, in the order of the current blocks.
The generator compares descriptors, never only their hashes.

The MultiProfile blocks copied or converted from a MultiProfile source are migrated in pass 2, profile by profile, each written
right after its source profile has been read. The source table carries their order after its entries: one in which no block
overwrites a source profile that is still to be read, its own or another block's.
"""

import os
import sys
from dataclasses import dataclass, field
from typing import Iterable, List, Optional, Tuple

sys.path.append(os.path.dirname(os.path.dirname(__file__)))
from common.py.data_model import CHECKSUM_SIZE, Block, DataModel
from common.py.identity import block_descriptor, data_descriptor, eeprom_layout_identity, identity_header
from common.py.legacy_dir import DATAMODEL_FILE, SETTINGS_FILE
from common.py.platform_settings import PlatformSettings

SOURCE_ROUTINE = 0xFD
"""Source table entry: another parameter list, converted by a generated routine"""

SOURCE_NONE = 0xFE
"""Source table entry: no source, a new or renamed block, which gets its defaults"""

SOURCE_UNCHANGED = 0xFF
"""Source table entry: the same block descriptor at the same offset, initialized as in v1"""

MAX_SOURCE_INDEX = 0xFC
"""The highest index into the legacy block array that a source table entry holds"""


@dataclass
class LegacyModel:
    """A data model given with --legacy or --legacy-dir, with the platform settings it was released with"""

    path: str
    """The data model's file, which names the model in messages"""

    datamodel: DataModel
    settings: PlatformSettings

    name_in_code: Optional[str] = None
    """Names the model in the generated code instead of its file name: for a model of --legacy-dir, its subdirectory and file name"""

    @property
    def name(self) -> str:
        """The name of the model in the generated code, the same on every machine: by default the data model's file name"""
        return self.name_in_code or os.path.basename(self.path)

    @staticmethod
    def load_from_files(datamodel_path: str, settings_path: str) -> "LegacyModel":
        """Loads a legacy model. Its 'checksum_size', if any, is ignored without any message."""
        return LegacyModel(datamodel_path, DataModel.load_from_file(datamodel_path, legacy=True), PlatformSettings.load_from_file(settings_path))

    @staticmethod
    def load_from_directory(directory: str) -> List["LegacyModel"]:
        """Loads the legacy models of a directory of models, in the order of their subdirectories' names.
        Each subdirectory holds one, as its data model with its platform settings under fixed names. Files at the top are ignored.
        A missing directory, and a subdirectory without both files, are errors: a hand-kept history mustn't drop a release without a word."""
        if not os.path.isdir(directory):
            raise Exception(f"The legacy model directory '{directory}' doesn't exist.")

        models: List[LegacyModel] = []
        for subdirectory in sorted(entry.name for entry in os.scandir(directory) if entry.is_dir()):
            datamodel_path = os.path.join(directory, subdirectory, DATAMODEL_FILE)
            settings_path = os.path.join(directory, subdirectory, SETTINGS_FILE)

            missing = [file for file, path in ((DATAMODEL_FILE, datamodel_path), (SETTINGS_FILE, settings_path)) if not os.path.isfile(path)]
            if missing:
                raise Exception(
                    f"'{os.path.join(directory, subdirectory)}' has no {' and no '.join(missing)}: "
                    f"each subdirectory of a legacy model directory holds one legacy model, as {DATAMODEL_FILE} with {SETTINGS_FILE}."
                )

            model = LegacyModel.load_from_files(datamodel_path, settings_path)
            model.name_in_code = f"{subdirectory}/{DATAMODEL_FILE}"
            models.append(model)
        return models


@dataclass
class LegacyEntry:
    """An entry of the legacy block array: a legacy block that the core reads"""

    source: Block
    """The legacy block"""

    target: Block
    """The current block of the same name"""

    copied: bool
    """True if the core copies it, the same parameter list. False if a routine converts it."""

    def describe(self) -> str:
        """What happens to the block, for the comments of the generated code"""
        if not self.copied:
            return "routine"

        changes = []
        if self.source.offset_in_eeprom != self.target.offset_in_eeprom:
            changes.append("moved")
        if self.source.management_type != self.target.management_type:
            changes.append(f"{Block.ManagementTypes(self.source.management_type).name} -> {Block.ManagementTypes(self.target.management_type).name}")
        if self.source.instance_count != self.target.instance_count:
            changes.append(f"{self.source.instance_count} -> {self.target.instance_count} instances")
        return "copied, " + ", ".join(changes)

    @property
    def is_in_pass_2(self) -> bool:
        """A MultiProfile block from a MultiProfile source, whose profiles are migrated one by one"""
        return self.source.management_type == self.target.management_type == Block.ManagementTypes.MultiProfile

    @property
    def read_profiles(self) -> int:
        """The source profiles that pass 2 reads: those that the current block still has"""
        return min(self.source.instance_count, self.target.instance_count)


@dataclass
class Pass2Step:
    """A block of pass 2, in the order of the pass"""

    target: Block
    """The current block"""

    descending: bool
    """True if its profiles are migrated from the highest one down, False if from profile 0 up"""


@dataclass
class LegacyConfiguration:
    """The source table and the legacy block array of one legacy model"""

    model: LegacyModel

    sources: List[int]
    """One entry per current block: an index into 'entries', or SOURCE_ROUTINE, SOURCE_NONE or SOURCE_UNCHANGED"""

    entries: List[LegacyEntry]
    """The legacy block array: the sources that the core reads, those of copied blocks and of blocks with a routine"""

    pass_2: List[Pass2Step] = field(default_factory=list)
    """The blocks of pass 2 in the order that keeps every source profile until it has been read"""

    @property
    def layout_identity(self) -> int:
        return eeprom_layout_identity(self.model.datamodel, self.model.settings.endianness)

    @property
    def identity_header(self) -> bytes:
        """The header that an image of this model starts with, which MEEM_Init() compares"""
        return identity_header(self.layout_identity)


def build_legacy_configuration(current: DataModel, model: LegacyModel) -> LegacyConfiguration:
    """Pairs every current block with its source by name and classifies it. Both models must have their offsets attached."""
    sources_by_name = {block.name.strip(): block for block in model.datamodel.children}
    sources: List[int] = []
    entries: List[LegacyEntry] = []

    for block in current.children:
        source = sources_by_name.get(block.name.strip())

        if source is None:
            sources.append(SOURCE_NONE)
        elif (block_descriptor(source) == block_descriptor(block)) and (source.offset_in_eeprom == block.offset_in_eeprom):
            sources.append(SOURCE_UNCHANGED)
        else:
            copied = data_descriptor(source) == data_descriptor(block)

            if copied and (len(entries) > MAX_SOURCE_INDEX):
                raise Exception(
                    f"Legacy model '{model.path}': the core reads more than {MAX_SOURCE_INDEX + 1} of its blocks, "
                    f"so the source table can't index block '{block.name}'."
                )
            sources.append(len(entries) if copied else SOURCE_ROUTINE)
            entries.append(LegacyEntry(source=source, target=block, copied=copied))

    return LegacyConfiguration(model=model, sources=sources, entries=entries, pass_2=order_pass_2(model, entries))


Range = Tuple[int, int]
"""A byte range of the EEPROM: [start, end)"""


def instance_range(block: Block, index: int) -> Range:
    instance_size = CHECKSUM_SIZE + block.data_size
    start = block.offset_in_eeprom + index * instance_size  # type: ignore
    return start, start + instance_size


def overlaps(ranges: Iterable[Range], others: Iterable[Range]) -> bool:
    others = list(others)
    return any(start < other_end and other_start < end for start, end in ranges for other_start, other_end in others)


def unread_sources(entry: LegacyEntry) -> List[Range]:
    """The source profiles of a block of pass 2 that haven't been read before the block's turn: all that the pass reads"""
    return [instance_range(entry.source, q) for q in range(entry.read_profiles)]


def keeps_own_sources(entry: LegacyEntry, descending: bool) -> bool:
    """Whether migrating the block's profiles in this direction writes no profile over a source profile that is still to be read.
    Profile p is read, then written; profiles beyond the source's count are only written."""
    profiles = range(entry.target.instance_count)
    unread = set(range(entry.read_profiles))

    for p in reversed(profiles) if descending else profiles:
        unread.discard(p)
        if overlaps([instance_range(entry.target, p)], (instance_range(entry.source, q) for q in unread)):
            return False
    return True


def order_pass_2(model: LegacyModel, entries: List[LegacyEntry]) -> List[Pass2Step]:
    """Orders the blocks of pass 2: a block may run once its writes miss the unread sources of the other remaining blocks,
    and once a direction keeps its own. Running a block only frees its sources, so a block that may run stays so. Therefore picking
    any block that may run finds an order whenever one exists: the first in definition order, preferably from profile 0 up."""
    remaining = [entry for entry in entries if entry.is_in_pass_2]
    order: List[Pass2Step] = []

    while remaining:
        step: Optional[Pass2Step] = None

        for entry in remaining:
            writes = [instance_range(entry.target, p) for p in range(entry.target.instance_count)]
            if not overlaps(writes, (r for other in remaining if other is not entry for r in unread_sources(other))):
                direction = next((descending for descending in (False, True) if keeps_own_sources(entry, descending)), None)
                if direction is not None:
                    step = Pass2Step(target=entry.target, descending=direction)
                    remaining.remove(entry)
                    break

        if step is None:
            raise Exception(
                f"Legacy model '{model.path}': no order of its MultiProfile blocks keeps every old profile until it has been read:\n"
                + "\n".join(f"- {reason}" for reason in why_blocked(remaining))
                + "\nChange the layout so that these blocks don't overwrite each other's old places, e.g. keep their order."
            )
        order.append(step)
    return order


def why_blocked(remaining: List[LegacyEntry]) -> List[str]:
    """Why none of the remaining blocks of pass 2 can run, for the error"""
    reasons = []

    for entry in remaining:
        writes = [instance_range(entry.target, p) for p in range(entry.target.instance_count)]
        victims = [f"'{other.target.name}'" for other in remaining if other is not entry and overlaps(writes, unread_sources(other))]
        if victims:
            reasons.append(f"block '{entry.target.name}' would overwrite the old profiles of block {', '.join(victims)}")
        else:
            reasons.append(f"block '{entry.target.name}' would overwrite its own old profiles before reading them, in either direction")
    return reasons
