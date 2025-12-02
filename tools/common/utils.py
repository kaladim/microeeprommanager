import os
import sys
import struct

sys.path.append(os.path.dirname(__file__))
from typing import Optional
from common.data_model import *
from common.platform_settings import *

param_type_to_format = {
    Parameter.DataTypes.uint8: "B",
    Parameter.DataTypes.int8: "b",
    Parameter.DataTypes.uint16: "H",
    Parameter.DataTypes.int16: "h",
    Parameter.DataTypes.uint32: "I",
    Parameter.DataTypes.int32: "i",
    Parameter.DataTypes.uint64: "Q",
    Parameter.DataTypes.int64: "q",
    Parameter.DataTypes.float32: "f",
    Parameter.DataTypes.float64: "d",
}


def extract_defaults(block: Block, settings: PlatformSettings) -> bytearray:
    """Extracts the default values of all parameters in a block as compacted byte array."""

    bytes = bytearray()
    fmt_base = ">" if settings.endianness == "big" else "<"

    for param in block.children:
        fmt = fmt_base + param_type_to_format[param.data_type]

        for val in param.default_value:
            bytes.extend(struct.pack(fmt, val))

    return bytes


def deduce_default_pattern(block: Block, settings: PlatformSettings) -> Optional[bytes]:
    """Tries to find the smallest value, which can be used to initialize all parameters within a block.
    If no such value exists, returns None"""

    # produce a compacted byte arrray
    bytes = extract_defaults(block, settings)

    # find the shortest recurring byte
    if all(b == bytes[0] for b in bytes):
        return bytes[0:1]

    # find the shortest recurring pattern
    for width in range(2, len(bytes) // 2 + 1):
        subs = [bytes[i : i + width] for i in range(0, len(bytes), width)]  # spilt 'bytes' to sub-lists with 'width' size
        if all(sl == subs[0] for sl in subs):
            return subs[0]
    return None


def attach_block_metadata(datamodel: DataModel, settings: PlatformSettings):
    align_all = "*" in [name for name in settings.page_aligned_blocks]
    offset_in_eeprom = 0

    for index, block in enumerate(datamodel.children):
        block.offset_in_eeprom = offset_in_eeprom
        block.size_in_eeprom = (datamodel.checksum_size + block.data_size) * block.instance_count
        block.default_pattern = deduce_default_pattern(block, settings) if block.compress_defaults else None

        offset_in_eeprom += block.size_in_eeprom

        if (
            (settings.eeprom_page_size > 0)
            and ((offset_in_eeprom % settings.eeprom_page_size) != 0)
            and (index < len(datamodel.children) - 1)
            and ((datamodel.children[index + 1].name in settings.page_aligned_blocks) or align_all)
        ):
            # Align to page boundary:
            offset_in_eeprom |= settings.eeprom_page_size - 1
            offset_in_eeprom += 1


def find_index_of_most_recent_sequence_counter(sequence_counters: bytes) -> Optional[int]:
    """Applies to wear-leveling blocks only."""
    INVALID_INSTANCE = 0xFF

    instance_count = len(sequence_counters)
    sequence_counter_last_valid = INVALID_INSTANCE
    min = 0xFF  # Initial min-max thresholds are intentionally inverted!
    max = 0  # Initial min-max thresholds are intentionally inverted!
    min_index: Optional[int] = None
    max_index: Optional[int] = None
    rollover_start_index: Optional[int] = None
    rollover_end_index: Optional[int] = None
    i = 0

    # Find min, max, rollover region.
    # Note: the loop must be executed +1 time than the instance_count, this is critically important!
    for _ in range(0, instance_count + 1):
        sequence_counter_current = sequence_counters[i]
        if sequence_counter_current != INVALID_INSTANCE:

            if sequence_counter_current < min:
                min = sequence_counter_current
                min_index = i
            if sequence_counter_current >= max:
                max = sequence_counter_current
                max_index = i

            # Capture the rollover region boundaries
            if (
                (rollover_start_index is None)
                and (sequence_counter_current < sequence_counter_last_valid)
                and ((sequence_counter_last_valid - sequence_counter_current) >= instance_count)
            ):
                rollover_start_index = i
            elif (
                (rollover_end_index is None)
                and (sequence_counter_current > sequence_counter_last_valid)
                and ((sequence_counter_current - sequence_counter_last_valid) >= instance_count)
            ):
                rollover_end_index = i

            sequence_counter_last_valid = sequence_counter_current

        i = (i + 1) % instance_count

    # Check for at least 1 valid instance:
    if (min_index is None) or (max_index is None):
        return None

    # Check for sequence counter rollover:
    if (max - min) >= instance_count:
        # Yep, rollover:
        assert (rollover_start_index is not None) and (rollover_end_index is not None)

        def findIndexOfMaxElement(array: bytes, start_index: int, loop_count: int):
            max = 0
            max_index: Optional[int] = None
            i = start_index

            for _ in range(0, loop_count):
                element = array[i]
                if (element != INVALID_INSTANCE) and (element >= max):
                    max = element
                    max_index = i
                i = (i + 1) % len(array)
            return max_index

        length = (
            (rollover_end_index - rollover_start_index)
            if (rollover_end_index > rollover_start_index)
            else (instance_count - (rollover_start_index - rollover_end_index))
        )
        return findIndexOfMaxElement(sequence_counters, rollover_start_index, length)

    # No rollover, return the max
    return max_index
