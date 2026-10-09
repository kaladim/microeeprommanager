import os
import sys
import argparse
import json
import struct

sys.path.append(os.path.dirname(__file__))
sys.path.append(os.path.dirname(os.path.dirname(__file__)))
from colorama import Fore
from bincopy import BinFile
from common.data_model import *
from common.platform_settings import *
from common.utils import attach_block_metadata, extract_defaults, param_type_to_format
from common.checksum_algo import *

checksum_size_to_data_type = {
    1: Parameter.DataTypes.uint8,
    2: Parameter.DataTypes.uint16,
    4: Parameter.DataTypes.uint32,
}


def parse_base_address(literal: str) -> int:
    if not literal.lower().startswith("0x"):
        raise Exception("Expected a leading 0x")
    return int(literal[2:], base=16)


def parse_fill(literal: str) -> bytes:
    if not literal.lower().startswith("0x"):
        raise Exception("Expected a leading 0x")
    return bytes.fromhex(literal[2:4])


def load_checksum_params(path: str) -> dict:
    with open(path, mode="r") as f:
        return json.load(f)


def create_instance_image(block: Block, settings: PlatformSettings, checksum_algo: ChecksumAlgorithm, checksum_size: int) -> bytearray:
    """Creates the image of one block instance: [checksum][data].
    As in the firmware, the checksum covers the data exactly as stored, including the sequence counter of wear-leveling blocks."""
    data = bytearray()

    if block.management_type == Block.ManagementTypes.WearLeveling:
        data.append(0)  # Add a sequence counter

    data.extend(extract_defaults(block, settings))

    fmt = (">" if settings.endianness == "big" else "<") + param_type_to_format[checksum_size_to_data_type[checksum_size]]
    return bytearray(struct.pack(fmt, checksum_algo.calculate(data))) + data


def create_bin_file(datamodel: DataModel, settings: PlatformSettings, checksum_algo: ChecksumAlgorithm, base_address: int, fill: bytes) -> BinFile:
    """Creates a BinFile, containing all data."""
    bf = BinFile(word_size_bits=8)

    for block in datamodel.children:
        instance = create_instance_image(block, settings, checksum_algo, datamodel.checksum_size)
        bf.add_binary(data=instance, address=base_address + block.offset_in_eeprom)  # type: ignore[call-arg]

        if block.management_type == Block.ManagementTypes.BackupCopy or block.management_type == Block.ManagementTypes.MultiProfile:
            # Backup copy and Multi-profile blocks must have all instances present in the EEPROM image
            for i in range(block.instance_count - 1):
                bf.add_binary(data=instance, address=base_address + block.offset_in_eeprom + (i + 1) * len(instance))  # type: ignore[call-arg]

        elif block.management_type == Block.ManagementTypes.WearLeveling:
            # Wear leveling blocks must have at least one valid instance and all instances present in the EEPROM image
            for i in range(block.instance_count - 1):
                bf.add_binary(data=[0xFF] * len(instance), address=base_address + block.offset_in_eeprom + (i + 1) * len(instance))  # type: ignore[call-arg]

    # Unused space is everything after the last block, plus the gaps between page-aligned blocks
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
    parser.add_argument("checksum_params", help="Path to checksum_parameters.json file")
    parser.add_argument("-a", "--address", default="0x0", help="Base address of the generated image (default: 0x0)")
    parser.add_argument("-f", "--fill", default="0xFF", help="Fill pattern for unused EEPROM space (default: 0xFF)")
    parser.add_argument("-o", "--output", default="./", help="Output directory for generated image (default: ./)")

    args = parser.parse_args()

    try:
        dataModel = DataModel.load_from_file(args.datamodel)
        settings = PlatformSettings.load_from_file(args.settings)
        checksum_params = load_checksum_params(args.checksum_params)

        attach_block_metadata(datamodel=dataModel, settings=settings)
        checksum_algo = ChecksumAlgorithm(checksum_params)
        binFile = create_bin_file(
            datamodel=dataModel, settings=settings, checksum_algo=checksum_algo, base_address=parse_base_address(args.address), fill=parse_fill(args.fill)
        )

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
