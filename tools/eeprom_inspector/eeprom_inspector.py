import os
import sys
import argparse
import json
import bincopy

sys.path.append(os.path.dirname(__file__))
sys.path.append(os.path.dirname(os.path.dirname(__file__)))
from colorama import Fore
from common.data_model import *
from common.platform_settings import *
from common.utils import attach_block_metadata
from common.checksum_algo import *
from report_builder import ReportBuilder


def load_eeprom_image(path: str) -> bytes:
    """Load EEPROM image from binary, Intel HEX, or Motorola S-record format."""
    ext = os.path.splitext(path)[1].lower()

    if ext in [".hex", ".ihex", ".srec", ".s19", ".s28", ".s37"]:
        bf = bincopy.BinFile(path)
        return bf.as_binary(minimum_address=bf.minimum_address, maximum_address=bf.maximum_address, padding=b"\xff")
    else:
        # Load as raw binary
        with open(path, mode="rb") as f:
            return f.read()


def load_checksum_params(path: str) -> dict:
    with open(path, mode="r") as f:
        return json.load(f)


def validate(datamodel: DataModel, eeprom_image: bytearray | bytes):
    assert dataModel.children[0].offset_in_eeprom is not None, "'utils.attach_block_metadata()' should be already called at this point!"

    last_block: Block = datamodel.children[-1]
    eeprom_size: int = len(eeprom_image)
    datamodel_size: int = last_block.offset_in_eeprom + last_block.size_in_eeprom  # type:ignore

    if eeprom_size < datamodel_size:
        raise Exception(f"Size of provided EEPROM image ({eeprom_size} bytes) is insufficient for this Data model (requiring {datamodel_size} bytes)!")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="mEEM EEPROM Inspector - EEPROM image analyzer", formatter_class=argparse.RawDescriptionHelpFormatter)

    parser.add_argument("eeprom", help="Path to binary EEPROM dump (*.bin) or (*.hex)")
    parser.add_argument("datamodel", help="Path to the datamodel.json file")
    parser.add_argument("settings", help="Path to the platform_settings.json file")
    parser.add_argument("checksum_params", help="Path to checksum_parameters.json file")
    parser.add_argument("-o", "--output", default="./report.html", help="Output file path. Defaults to './report.html' if not specified")

    args = parser.parse_args()

    try:
        eeprom_image = load_eeprom_image(args.eeprom)
        dataModel = DataModel.load_from_file(args.datamodel)
        settings = PlatformSettings.load_from_file(args.settings)
        checksum_params = load_checksum_params(args.checksum_params)
        paths = {
            "bin": args.eeprom,
            "datamodel": args.datamodel,
            "settings": args.settings,
            "checksum_params": args.checksum_params,
            "report": args.output,
        }

        attach_block_metadata(datamodel=dataModel, settings=settings)
        validate(datamodel=dataModel, eeprom_image=eeprom_image)
        report = ReportBuilder(
            datamodel=dataModel, settings=settings, eeprom_image=eeprom_image, checksum_algo=ChecksumAlgorithm(params=checksum_params), paths=paths
        ).create_report()

        with open(args.output, mode="w", encoding="utf-8") as f:
            f.write(report)

        print(f"{Fore.GREEN}eeprom_inspector::info:: report successfully generated to {args.output}{Fore.RESET}")
        exit_code = 0
    except Exception as x:
        import traceback

        print(f"{Fore.RED}eeprom_inspector::error:: {x}{traceback.print_exc()}{Fore.RESET}")
        exit_code = 1

    sys.exit(exit_code)
