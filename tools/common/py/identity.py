"""The identities of mEEM v2: canonical descriptors and their hashes.

- Data identity: CRC-16/IBM-3740 of the data descriptor, the ordered parameters and bitfields of a block.
- Block identity: CRC-16/IBM-3740 of the block descriptor: block name, management type, instance count and data identity.
- EEPROM layout identity: CRC-32/ISO-HDLC of the layout descriptor: the endianness, and every block identity with its offset.
- Identity header: the 6 bytes at EEPROM offset 0, laid out like an instance: the CRC-16 over the identity, then the identity,
  both little-endian on every target.

Descriptors are UTF-8, with their lines joined by '\\n', and names stripped. The tools compute the identities; the firmware
only compares identity headers.
"""

import os
import sys
from dataclasses import dataclass
from typing import Dict, List, Sequence

sys.path.append(os.path.dirname(os.path.dirname(os.path.dirname(__file__))))
from common.py.crc import crc16, crc32
from common.py.data_model import Block, DataModel, Parameter

IDENTITY_HEADER_SIZE = 6
"""Size of the identity header at EEPROM offset 0, in bytes. The blocks follow it."""

DATA_DESCRIPTOR_TAG = "mEEM-data-v2"
BLOCK_DESCRIPTOR_TAG = "mEEM-block-v2"
LAYOUT_DESCRIPTOR_TAG = "mEEM-layout-v2"


def data_descriptor(block: Block) -> str:
    """The canonical descriptor of the block's parameters: each parameter, directly followed by its bitfields"""
    lines = [DATA_DESCRIPTOR_TAG]

    for param in block.children:
        lines.append(f"param={param.name.strip()},{Parameter.DataTypes(param.data_type).name},{param.multiplicity}")
        for bitfield in param.children:
            lines.append(f"bitfield={bitfield.name.strip()},{bitfield.size_in_bits}")

    return "\n".join(lines)


def data_identity(block: Block) -> int:
    """16-bit hash of the data descriptor. Blocks with the same data identity hold the same parameters."""
    return crc16(data_descriptor(block).encode("utf-8"))


def block_descriptor(block: Block) -> str:
    """The canonical descriptor of the block. Not its offset: the location doesn't change the content."""
    lines = [
        BLOCK_DESCRIPTOR_TAG,
        f"block={block.name.strip()}",
        f"mgmt={Block.ManagementTypes(block.management_type).name}",
        f"count={block.instance_count}",
        f"data={data_identity(block):04X}",
    ]
    return "\n".join(lines)


def block_identity(block: Block) -> int:
    """16-bit hash of the block descriptor. Blocks with the same block identity hold the same bytes in the same way."""
    return crc16(block_descriptor(block).encode("utf-8"))


def layout_descriptor(datamodel: DataModel, endianness: str) -> str:
    """The canonical descriptor of the EEPROM layout: every block identity with its offset, in definition order.
    The blocks must have their offsets attached; endianness is that of the platform settings."""
    if endianness not in ("little", "big"):
        raise ValueError(f"Unknown endianness '{endianness}': 'little' or 'big' expected")

    lines = [LAYOUT_DESCRIPTOR_TAG, f"endianness={endianness}"]

    for block in datamodel.children:
        if block.offset_in_eeprom is None:
            raise ValueError(f"Block '{block.name}' has no offset. Attach the block metadata before computing the EEPROM layout identity.")
        lines.append(f"block={block_identity(block):04X},{block.offset_in_eeprom}")

    return "\n".join(lines)


def eeprom_layout_identity(datamodel: DataModel, endianness: str) -> int:
    """32-bit hash of the layout descriptor. It changes with any change that moves or reshapes a byte, and with the endianness."""
    return crc32(layout_descriptor(datamodel, endianness).encode("utf-8"))


def identity_header(layout_identity: int) -> bytes:
    """The 6 bytes of the identity header: the CRC-16 over the identity's 4 bytes, then the identity, both little-endian"""
    identity_bytes = layout_identity.to_bytes(4, "little")
    return crc16(identity_bytes).to_bytes(2, "little") + identity_bytes


@dataclass
class EepromLayout:
    """A data model whose blocks have their offsets attached, with the endianness of its platform settings"""

    name: str
    """Names the model in error messages, e.g. its file"""

    datamodel: DataModel
    endianness: str


def check_collisions(layouts: Sequence[EepromLayout]):
    """Checks that no identity of the given EEPROM layouts stands for two different descriptors.
    Equal descriptors share their identity, which is no collision. Raises an exception that names the blocks or models of every collision.
    """
    # identity -> descriptor -> where it was seen first
    data_identities: Dict[int, Dict[str, str]] = {}
    block_identities: Dict[int, Dict[str, str]] = {}
    layout_identities: Dict[int, Dict[str, str]] = {}

    def record(identities: Dict[int, Dict[str, str]], identity: int, descriptor: str, where: str):
        identities.setdefault(identity, {}).setdefault(descriptor, where)

    for layout in layouts:
        for block in layout.datamodel.children:
            where = f"block '{block.name}' of model '{layout.name}'"
            record(data_identities, data_identity(block), data_descriptor(block), where)
            record(block_identities, block_identity(block), block_descriptor(block), where)

        descriptor = layout_descriptor(layout.datamodel, layout.endianness)
        record(layout_identities, crc32(descriptor.encode("utf-8")), descriptor, f"model '{layout.name}'")

    errors: List[str] = []

    def report(identities: Dict[int, Dict[str, str]], what: str, digits: int, descriptors: str):
        for identity, seen in identities.items():
            if len(seen) > 1:
                errors.append(f"{what} 0x{identity:0{digits}X} stands for {len(seen)} different {descriptors}: {', '.join(seen.values())}")

    report(data_identities, "Data identity", 4, "parameter lists")
    report(block_identities, "Block identity", 4, "blocks")
    report(layout_identities, "EEPROM layout identity", 8, "layouts")

    if errors:
        raise Exception(
            "Identity collisions:\n"
            + "\n".join(f"- {error}" for error in errors)
            + "\nChange the new layout without losing data, e.g. by reordering two new parameters, so that the identities differ."
        )
