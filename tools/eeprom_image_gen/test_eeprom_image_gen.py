import os
import sys
import json
import subprocess
import tempfile
import unittest

sys.path.append(os.path.dirname(__file__))
sys.path.append(os.path.dirname(os.path.dirname(__file__)))
from bincopy import BinFile
from common.py.data_model import *
from common.py.platform_settings import *
from common.py.utils import attach_block_metadata, extract_defaults
from common.py.crc import crc16
from common.py.identity import IDENTITY_HEADER_SIZE, eeprom_layout_identity, identity_header
from eeprom_image_gen import create_instance_image, create_bin_file
from make_image_vectors import load_image_vectors, generate_image

IMAGE_GEN = os.path.join(os.path.dirname(__file__), "eeprom_image_gen.py")
ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
TEST_CONFIG_DIR = os.path.join(ROOT, "test", "meem_config")
VECTORS_FILE = os.path.join(ROOT, "test", "vectors", "identity_vectors.json")
NEUTRAL_SETTINGS = os.path.join(ROOT, "test", "footprint", "production_like", "platform_settings_xc8.json")


def create_block(management_type: Block.ManagementTypes, instance_count: int) -> Block:
    param = Parameter(name="param", data_type=Parameter.DataTypes.uint16, multiplicity=3, default_value=[0x1234, 0xABCD, 0x0F0F])
    return Block(name="block", children=[param], management_type=management_type, instance_count=instance_count)


def load_json(path: str) -> dict:
    with open(path, mode="r", encoding="utf-8") as f:
        return json.load(f)


def write_json(path: str, obj: dict) -> str:
    with open(path, mode="w", encoding="utf-8") as f:
        json.dump(obj, f)
    return path


class TestCreateInstanceImage(unittest.TestCase):
    def test_checksum_is_crc16_in_target_byte_order(self):
        block = create_block(Block.ManagementTypes.Basic, 1)

        for endianness in ("little", "big"):
            with self.subTest(endianness=endianness):
                settings = PlatformSettings(endianness=endianness)

                image = create_instance_image(block, settings)

                data = image[CHECKSUM_SIZE:]
                checksum = crc16(data)
                self.assertEqual(data, extract_defaults(block, settings))
                self.assertEqual(image[:CHECKSUM_SIZE], checksum.to_bytes(CHECKSUM_SIZE, endianness))

                # The checks above are meaningful only if the byte order matters
                self.assertNotEqual(checksum.to_bytes(CHECKSUM_SIZE, "little"), checksum.to_bytes(CHECKSUM_SIZE, "big"))

    def test_nan_default_is_stored_as_a_quiet_nan(self):
        param = Parameter(name="param", data_type=Parameter.DataTypes.float32, default_value=[float("nan")])
        block = Block(name="block", children=[param])

        self.assertEqual(bytes(create_instance_image(block, PlatformSettings(endianness="little"))[CHECKSUM_SIZE:]), bytes.fromhex("0000C07F"))
        self.assertEqual(bytes(create_instance_image(block, PlatformSettings(endianness="big"))[CHECKSUM_SIZE:]), bytes.fromhex("7FC00000"))

    def test_wear_leveling_checksum_covers_sequence_counter(self):
        block = create_block(Block.ManagementTypes.WearLeveling, 2)
        settings = PlatformSettings(endianness="little")

        image = create_instance_image(block, settings)

        data = image[CHECKSUM_SIZE:]
        self.assertEqual(len(data), block.data_size)
        self.assertEqual(data[0], 0)  # Sequence counter
        self.assertEqual(data[1:], extract_defaults(block, settings))
        self.assertEqual(int.from_bytes(image[:CHECKSUM_SIZE], "little"), crc16(data))

        # The check above is meaningful only if the sequence counter affects the checksum
        self.assertNotEqual(crc16(data), crc16(data[1:]))


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

        bf = create_bin_file(datamodel, settings, base_address, b"\x00")
        image = bf.as_binary()

        self.assertEqual(bf.minimum_address, base_address)
        self.assertEqual(bf.maximum_address, base_address + settings.eeprom_size)
        self.assertLess(used_size, settings.eeprom_size)  # The check below is meaningful only if there's unused space
        self.assertEqual(image[used_size:], bytes(settings.eeprom_size - used_size))

        # Between the identity header and the page-aligned first block
        first_offset = datamodel.children[0].offset_in_eeprom
        self.assertEqual(image[:IDENTITY_HEADER_SIZE], identity_header(eeprom_layout_identity(datamodel, settings.endianness)))
        self.assertGreater(first_offset, IDENTITY_HEADER_SIZE)  # type: ignore[arg-type]
        self.assertEqual(image[IDENTITY_HEADER_SIZE:first_offset], bytes(first_offset - IDENTITY_HEADER_SIZE))  # type: ignore[operator]

        # The gaps between page-aligned blocks
        for block, next_block in zip(datamodel.children, datamodel.children[1:]):
            gap_start = block.offset_in_eeprom + block.size_in_eeprom  # type: ignore[operator]
            self.assertEqual(image[gap_start : next_block.offset_in_eeprom], bytes(next_block.offset_in_eeprom - gap_start))  # type: ignore[operator]

    def test_datamodel_exceeding_the_eeprom_size_is_rejected(self):
        datamodel, settings = self.load_test_configuration()
        last_block = datamodel.children[-1]
        settings.eeprom_size = last_block.offset_in_eeprom + last_block.size_in_eeprom - 1  # type: ignore[operator]

        with self.assertRaises(Exception):
            create_bin_file(datamodel, settings, 0, b"\xff")


class TestGoldenImage(unittest.TestCase):
    """The image of the neutral golden model against the shared vector file: the identity header, and each block at its offset"""

    def test_header_and_block_offsets(self):
        model_vectors = load_json(VECTORS_FILE)["models"][0]
        datamodel_path = os.path.join(ROOT, model_vectors["datamodel"])

        for layout in model_vectors["layouts"]:
            with self.subTest(endianness=layout["endianness"]), tempfile.TemporaryDirectory() as tmp_dir:
                settings_json = load_json(NEUTRAL_SETTINGS) | {"endianness": layout["endianness"]}
                settings_path = write_json(os.path.join(tmp_dir, "settings.json"), settings_json)

                result = subprocess.run([sys.executable, IMAGE_GEN, datamodel_path, settings_path, "-o", tmp_dir], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

                bf = BinFile()
                bf.add_ihex_file(os.path.join(tmp_dir, "eeprom.hex"))
                image = bf.as_binary()

                self.assertEqual(image[:IDENTITY_HEADER_SIZE], bytes.fromhex(layout["identity_header"]))

                settings = PlatformSettings.load_from_file(settings_path)
                for block, block_vectors in zip(DataModel.load_from_file(datamodel_path).children, model_vectors["blocks"], strict=True):
                    instance = create_instance_image(block, settings)
                    offset = block_vectors["offset"]
                    self.assertEqual(image[offset : offset + len(instance)], instance, f"Block {block.name} at offset {offset}")


class TestImageVectors(unittest.TestCase):
    """The images that the inspector's tests read (test/vectors/image_vectors.json) are those that the image generator writes now"""

    def test_images_are_up_to_date(self):
        for vector in load_image_vectors():
            with self.subTest(image=vector["image"]):
                with open(os.path.join(ROOT, vector["image"]), mode="r", encoding="ascii") as f:
                    committed = f.read()

                self.assertEqual(committed, generate_image(vector), "Stale, regenerate it with: uv run ./tools/eeprom_image_gen/make_image_vectors.py")


class TestEepromImageGen(unittest.TestCase):
    def test_image_of_test_configuration_passes_checksum_verification(self):
        """Generates images of the unit tests' configuration and verifies each instance's checksum the same way the firmware does."""

        for endianness in ("little", "big"):
            with self.subTest(endianness=endianness), tempfile.TemporaryDirectory() as tmp_dir:
                datamodel_path = os.path.join(TEST_CONFIG_DIR, "eeprom_datamodel.json")
                settings_json = load_json(os.path.join(TEST_CONFIG_DIR, "platform_settings_gcc.json")) | {"endianness": endianness}
                settings_path = write_json(os.path.join(tmp_dir, "settings.json"), settings_json)

                result = subprocess.run(
                    [sys.executable, IMAGE_GEN, datamodel_path, settings_path, "-a", "0x0", "-o", tmp_dir],
                    capture_output=True,
                    text=True,
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

                bf = BinFile()
                bf.add_ihex_file(os.path.join(tmp_dir, "eeprom.hex"))
                image = bf.as_binary()

                datamodel = DataModel.load_from_file(datamodel_path)
                attach_block_metadata(datamodel=datamodel, settings=PlatformSettings.load_from_file(settings_path))

                for block in datamodel.children:
                    instance_size = CHECKSUM_SIZE + block.data_size

                    for i in range(block.instance_count):
                        offset = block.offset_in_eeprom + i * instance_size
                        instance = image[offset : offset + instance_size]

                        if block.management_type == Block.ManagementTypes.WearLeveling and i > 0:
                            self.assertEqual(instance, bytes([0xFF] * instance_size), f"{block.name}[{i}] is expected to be erased")
                        else:
                            checksum = int.from_bytes(instance[:CHECKSUM_SIZE], endianness)
                            self.assertEqual(checksum, crc16(instance[CHECKSUM_SIZE:]), f"{block.name}[{i}] has invalid checksum")

    def test_data_model_with_checksum_size_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp_dir:
            datamodel_json = load_json(os.path.join(TEST_CONFIG_DIR, "eeprom_datamodel.json")) | {"checksum_size": 2}
            datamodel_path = write_json(os.path.join(tmp_dir, "datamodel.json"), datamodel_json)

            result = subprocess.run(
                [sys.executable, IMAGE_GEN, datamodel_path, os.path.join(TEST_CONFIG_DIR, "platform_settings_gcc.json"), "-o", tmp_dir],
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 1)
            self.assertIn("checksum_size", result.stdout + result.stderr)
            self.assertFalse(os.path.exists(os.path.join(tmp_dir, "eeprom.hex")))

    def test_base_address_defaults_to_0(self):
        with tempfile.TemporaryDirectory() as tmp_dir:
            result = subprocess.run(
                [
                    sys.executable,
                    IMAGE_GEN,
                    os.path.join(TEST_CONFIG_DIR, "eeprom_datamodel.json"),
                    os.path.join(TEST_CONFIG_DIR, "platform_settings_gcc.json"),
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
