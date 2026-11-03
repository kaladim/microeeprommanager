import os
import sys
import argparse
import struct

sys.path.append(os.path.dirname(__file__))
sys.path.append(os.path.dirname(os.path.dirname(__file__)))
from colorama import Fore
from bincopy import BinFile
from common.py.data_model import *
from common.py.platform_settings import *
from common.py.utils import attach_block_metadata, extract_defaults
from common.py.crc import crc16
from common.py.identity import eeprom_layout_identity, identity_header


def parse_base_address(literal: str) -> int:
    if not literal.lower().startswith("0x"):
        raise Exception("Expected a leading 0x")
    return int(literal[2:], base=16)


def parse_fill(literal: str) -> bytes:
    if not literal.lower().startswith("0x"):
        raise Exception("Expected a leading 0x")
    return bytes.fromhex(literal[2:4])


def create_instance_image(block: Block, settings: PlatformSettings) -> bytearray:
    """Creates the image of one block instance: [checksum][data].
    As in the firmware, the checksum is the CRC-16 of the data exactly as stored, including the sequence counter of wear-leveling blocks,
    and is stored in the target's byte order."""
    data = bytearray()

    if block.management_type == Block.ManagementTypes.WearLeveling:
        data.append(0)  # Add a sequence counter

    data.extend(extract_defaults(block, settings))

    fmt = (">" if settings.endianness == "big" else "<") + "H"
    return bytearray(struct.pack(fmt, crc16(data))) + data


def create_bin_file(datamodel: DataModel, settings: PlatformSettings, base_address: int, fill: bytes) -> BinFile:
    """Creates a BinFile, containing all data: the identity header at offset 0, then the blocks at their offsets."""
    bf = BinFile(word_size_bits=8)

    bf.add_binary(data=identity_header(eeprom_layout_identity(datamodel, settings.endianness)), address=base_address)  # type: ignore[call-arg]

    for block in datamodel.children:
        instance = create_instance_image(block, settings)
        bf.add_binary(data=instance, address=base_address + block.offset_in_eeprom)  # type: ignore[call-arg]

        if block.management_type == Block.ManagementTypes.BackupCopy or block.management_type == Block.ManagementTypes.MultiProfile:
            # Backup copy and Multi-profile blocks must have all instances present in the EEPROM image
            for i in range(block.instance_count - 1):
                bf.add_binary(data=instance, address=base_address + block.offset_in_eeprom + (i + 1) * len(instance))  # type: ignore[call-arg]

        elif block.management_type == Block.ManagementTypes.WearLeveling:
            # Wear leveling blocks must have at least one valid instance and all instances present in the EEPROM image
            for i in range(block.instance_count - 1):
                bf.add_binary(data=[0xFF] * len(instance), address=base_address + block.offset_in_eeprom + (i + 1) * len(instance))  # type: ignore[call-arg]

    # Unused space is everything after the last block, plus the gaps in front of page-aligned blocks, including a page-aligned first block
    used_size = bf.maximum_address - base_address
    if used_size > settings.eeprom_size:
        raise Exception(f"The datamodel requires {used_size} bytes of EEPROM, but only {settings.eeprom_size} bytes are available")
    if used_size < settings.eeprom_size:
        bf.add_binary(data=fill * (settings.eeprom_size - used_size), address=bf.maximum_address)  # type: ignore[call-arg]

    bf.fill(value=fill)
    return bf


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="mEEM EEPROM image generator - creates ready-to-flash EEPROM images with default values",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )

    parser.add_argument("datamodel", help="Path to the datamodel.json file")
    parser.add_argument("settings", help="Path to the platform_settings.json file")
    parser.add_argument("-a", "--address", default="0x0", help="Base address of the generated image (default: 0x0)")
    parser.add_argument("-f", "--fill", default="0xFF", help="Fill pattern for unused EEPROM space (default: 0xFF)")
    parser.add_argument("-o", "--output", default="./", help="Output directory for generated image (default: ./)")

    args = parser.parse_args()

    try:
        dataModel = DataModel.load_from_file(args.datamodel)
        settings = PlatformSettings.load_from_file(args.settings)

        attach_block_metadata(datamodel=dataModel, settings=settings)
        binFile = create_bin_file(datamodel=dataModel, settings=settings, base_address=parse_base_address(args.address), fill=parse_fill(args.fill))

        image_path = os.path.join(args.output, "eeprom.hex")
        with open(image_path, "w") as f:
            f.write(binFile.as_ihex())

        print(f"{Fore.GREEN}eeprom_image_gen::info:: EEPROM image generated successfully: {image_path}{Fore.RESET}")
        exit_code = 0
    except Exception as x:
        import traceback

        print(f"{Fore.RED}eeprom_image_gen::error:: {x}{traceback.print_exc()}{Fore.RESET}")
        exit_code = 1

    sys.exit(exit_code)
