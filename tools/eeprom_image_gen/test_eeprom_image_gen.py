import os
import sys
import json
import subprocess
import tempfile
import unittest

sys.path.append(os.path.dirname(__file__))
sys.path.append(os.path.dirname(os.path.dirname(__file__)))
from bincopy import BinFile
from common.data_model import *
from common.platform_settings import *
from common.utils import attach_block_metadata, extract_defaults
from common.checksum_algo import *
from eeprom_image_gen import create_instance_image, create_bin_file

IMAGE_GEN = os.path.join(os.path.dirname(__file__), "eeprom_image_gen.py")
TEST_CONFIG_DIR = os.path.join(os.path.dirname(__file__), "..", "..", "test", "meem_config")

# A CRC for each supported checksum size. All of them start with a non-zero shift register,
# so a leading 0 (the initial sequence counter of wear-leveling blocks) changes the result.
checksum_params_by_size = {
    1: {"algo": "crc", "polynomial": 0x107, "initial_value": 0xFF, "reverse_input": False, "final_xor_value": 0},
    2: {"algo": "crc", "polynomial": 0x11021, "initial_value": 0xFFFF, "reverse_input": False, "final_xor_value": 0},  # CRC-16/CCITT-FALSE
    4: {"algo": "crc", "polynomial": 0x104C11DB7, "initial_value": 0, "reverse_input": True, "final_xor_value": 0xFFFFFFFF},  # CRC-32
}


def create_block(management_type: Block.ManagementTypes, instance_count: int) -> Block:
    param = Parameter(name="param", data_type=Parameter.DataTypes.uint16, multiplicity=3, default_value=[0x1234, 0xABCD, 0x0F0F])
    return Block(name="block", children=[param], management_type=management_type, instance_count=instance_count)


class TestCreateInstanceImage(unittest.TestCase):
    def test_checksum_has_checksum_size_and_endianness(self):
        block = create_block(Block.ManagementTypes.Basic, 1)

        for checksum_size, checksum_params in checksum_params_by_size.items():
            for endianness in ("little", "big"):
                with self.subTest(checksum_size=checksum_size, endianness=endianness):
                    settings = PlatformSettings(endianness=endianness)
                    checksum_algo = ChecksumAlgorithm(checksum_params)

                    image = create_instance_image(block, settings, checksum_algo, checksum_size)

                    data = image[checksum_size:]
                    checksum = checksum_algo.calculate(data)
                    self.assertEqual(data, extract_defaults(block, settings))
                    self.assertEqual(image[:checksum_size], checksum.to_bytes(checksum_size, endianness))

                    if checksum_size > 1:
                        # The checks above are meaningful only if the byte order matters
                        self.assertNotEqual(checksum.to_bytes(checksum_size, "little"), checksum.to_bytes(checksum_size, "big"))

    def test_wear_leveling_checksum_covers_sequence_counter(self):
        block = create_block(Block.ManagementTypes.WearLeveling, 2)
        settings = PlatformSettings(endianness="little")

        for checksum_size, checksum_params in checksum_params_by_size.items():
            with self.subTest(checksum_size=checksum_size):
                checksum_algo = ChecksumAlgorithm(checksum_params)

                image = create_instance_image(block, settings, checksum_algo, checksum_size)

                data = image[checksum_size:]
                self.assertEqual(len(data), block.data_size)
                self.assertEqual(data[0], 0)  # Sequence counter
                self.assertEqual(data[1:], extract_defaults(block, settings))
                self.assertEqual(int.from_bytes(image[:checksum_size], "little"), checksum_algo.calculate(data))

                # The check above is meaningful only if the sequence counter affects the checksum
                self.assertNotEqual(checksum_algo.calculate(data), checksum_algo.calculate(data[1:]))


class TestCreateBinFile(unittest.TestCase):
    def load_test_configuration(self) -> tuple[DataModel, PlatformSettings]:
        datamodel = DataModel.load_from_file(os.path.join(TEST_CONFIG_DIR, "eeprom_datamodel.json"))
        settings = PlatformSettings.load_from_file(os.path.join(TEST_CONFIG_DIR, "platform_settings_gcc.json"))
        attach_block_metadata(datamodel=datamodel, settings=settings)
        return datamodel, settings

    def test_unused_space_is_filled_up_to_the_eeprom_size(self):
        datamodel, settings = self.load_test_configuration()
        base_address = 0x1000
        last_block = datamodel.children[-1]
        used_size = last_block.offset_in_eeprom + last_block.size_in_eeprom  # type: ignore[operator]

        bf = create_bin_file(datamodel, settings, ChecksumAlgorithm(checksum_params_by_size[1]), base_address, b"\x00")
        image = bf.as_binary()

        self.assertEqual(bf.minimum_address, base_address)
        self.assertEqual(bf.maximum_address, base_address + settings.eeprom_size)
        self.assertLess(used_size, settings.eeprom_size)  # The check below is meaningful only if there's unused space
        self.assertEqual(image[used_size:], bytes(settings.eeprom_size - used_size))

        # The gaps between page-aligned blocks
        for block, next_block in zip(datamodel.children, datamodel.children[1:]):
            gap_start = block.offset_in_eeprom + block.size_in_eeprom  # type: ignore[operator]
            self.assertEqual(image[gap_start : next_block.offset_in_eeprom], bytes(next_block.offset_in_eeprom - gap_start))  # type: ignore[operator]

    def test_datamodel_exceeding_the_eeprom_size_is_rejected(self):
        datamodel, settings = self.load_test_configuration()
        last_block = datamodel.children[-1]
        settings.eeprom_size = last_block.offset_in_eeprom + last_block.size_in_eeprom - 1  # type: ignore[operator]

        with self.assertRaises(Exception):
            create_bin_file(datamodel, settings, ChecksumAlgorithm(checksum_params_by_size[1]), 0, b"\xff")


class TestEepromImageGen(unittest.TestCase):
    def test_image_of_test_configuration_passes_checksum_verification(self):
        """Generates images of the unit tests' configuration and verifies each instance's checksum the same way the firmware does."""

        def load_json(path: str) -> dict:
            with open(path, mode="r", encoding="utf-8") as f:
                return json.load(f)

        def write_json(path: str, obj: dict) -> str:
            with open(path, mode="w", encoding="utf-8") as f:
                json.dump(obj, f)
            return path

        for checksum_size, checksum_params in checksum_params_by_size.items():
            for endianness in ("little", "big"):
                with self.subTest(checksum_size=checksum_size, endianness=endianness), tempfile.TemporaryDirectory() as tmp_dir:
                    datamodel_json = load_json(os.path.join(TEST_CONFIG_DIR, "eeprom_datamodel.json")) | {"checksum_size": checksum_size}
                    settings_json = load_json(os.path.join(TEST_CONFIG_DIR, "platform_settings_gcc.json")) | {"endianness": endianness}
                    datamodel_path = write_json(os.path.join(tmp_dir, "datamodel.json"), datamodel_json)
                    settings_path = write_json(os.path.join(tmp_dir, "settings.json"), settings_json)
                    checksum_params_path = write_json(os.path.join(tmp_dir, "checksum_params.json"), checksum_params)

                    result = subprocess.run(
                        [sys.executable, IMAGE_GEN, datamodel_path, settings_path, checksum_params_path, "-a", "0x0", "-o", tmp_dir],
                        capture_output=True,
                        text=True,
                    )
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

                    bf = BinFile()
                    bf.add_ihex_file(os.path.join(tmp_dir, "eeprom.hex"))
                    image = bf.as_binary()

                    datamodel = DataModel.load_from_file(datamodel_path)
                    attach_block_metadata(datamodel=datamodel, settings=PlatformSettings.load_from_file(settings_path))
                    checksum_algo = ChecksumAlgorithm(checksum_params)

                    for block in datamodel.children:
                        instance_size = checksum_size + block.data_size

                        for i in range(block.instance_count):
                            offset = block.offset_in_eeprom + i * instance_size
                            instance = image[offset : offset + instance_size]

                            if block.management_type == Block.ManagementTypes.WearLeveling and i > 0:
                                self.assertEqual(instance, bytes([0xFF] * instance_size), f"{block.name}[{i}] is expected to be erased")
                            else:
                                checksum = int.from_bytes(instance[:checksum_size], endianness)
                                self.assertEqual(checksum, checksum_algo.calculate(instance[checksum_size:]), f"{block.name}[{i}] has invalid checksum")

    def test_base_address_defaults_to_0(self):
        with tempfile.TemporaryDirectory() as tmp_dir:
            result = subprocess.run(
                [
                    sys.executable,
                    IMAGE_GEN,
                    os.path.join(TEST_CONFIG_DIR, "eeprom_datamodel.json"),
                    os.path.join(TEST_CONFIG_DIR, "platform_settings_gcc.json"),
                    os.path.join(TEST_CONFIG_DIR, "checksum_params.json"),
                    "-o",
                    tmp_dir,
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

            bf = BinFile()
            bf.add_ihex_file(os.path.join(tmp_dir, "eeprom.hex"))
            self.assertEqual(bf.minimum_address, 0)


if __name__ == "__main__":
    unittest.main()
