import os
import sys
import argparse
import json

sys.path.append(os.path.dirname(__file__))
sys.path.append(os.path.dirname(os.path.dirname(__file__)))
from colorama import Fore
from bincopy import BinFile
from common.data_model import *
from common.platform_settings import *
from common.utils import attach_block_metadata, extract_defaults
from common.checksum_algo import *


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


def create_instance_image(block: Block, settings: PlatformSettings) -> bytearray:
    bytes = bytearray()
    defaults = extract_defaults(block, settings)
    bytes.append(checksum_algo.calculate(defaults))

    if block.management_type == Block.ManagementTypes.WearLeveling:
        bytes.append(0)  # Add a sequence counter

    bytes.extend(defaults)
    return bytes


def create_bin_file(datamodel: DataModel, settings: PlatformSettings, base_address: int, fill: bytes) -> BinFile:
    """Creates a BinFile, containing all data."""
    bf = BinFile(word_size_bits=8)

    for block in datamodel.children:
        instance = create_instance_image(block, settings)
        bf.add_binary(data=instance, address=base_address + block.offset_in_eeprom)  # type: ignore[call-arg]

        if block.management_type == Block.ManagementTypes.BackupCopy:
            # Backup copy blocks must have both instances valid
            bf.add_binary(data=instance, address=base_address + block.offset_in_eeprom + len(instance))  # type: ignore[call-arg]

    bf.fill(value=fill, max_words=settings.eeprom_size)
    return bf


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="mEEM EEPROM image generator - creates ready-to-flash EEPROM images with default values",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )

    parser.add_argument("datamodel", help="Path to the datamodel.json file")
    parser.add_argument("settings", help="Path to the platform_settings.json file")
    parser.add_argument("checksum_params", help="Path to checksum_parameters.json file")
    parser.add_argument("-a", "--address", default="0", help="Base address of the generated image (default: 0)")
    parser.add_argument("-f", "--fill", default="0xFF", help="Fill pattern for unused EEPROM space (default: 0xFF)")
    parser.add_argument("-o", "--output", default="./", help="Output directory for generated image (default: ./)")

    args = parser.parse_args()

    try:
        dataModel = DataModel.load_from_file(args.datamodel)
        settings = PlatformSettings.load_from_file(args.settings)
        checksum_params = load_checksum_params(args.checksum_params)

        attach_block_metadata(datamodel=dataModel, settings=settings)
        checksum_algo = ChecksumAlgorithm(checksum_params)
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
