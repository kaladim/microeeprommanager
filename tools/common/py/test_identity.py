import copy
import json
import os
import sys
import unittest
from unittest import mock

sys.path.append(os.path.dirname(os.path.dirname(os.path.dirname(__file__))))
from common.py import identity
from common.py.data_model import *
from common.py.identity import (
    IDENTITY_HEADER_SIZE,
    EepromLayout,
    block_descriptor,
    block_identity,
    check_collisions,
    data_descriptor,
    data_identity,
    eeprom_layout_identity,
    identity_header,
    layout_descriptor,
)

ROOT = os.path.join(os.path.dirname(__file__), "..", "..", "..")
VECTORS_FILE = os.path.join(ROOT, "test", "vectors", "identity_vectors.json")


def load_vectors() -> dict:
    with open(VECTORS_FILE, "r", encoding="utf-8") as f:
        return json.load(f)


def load_model_with_vector_offsets(model_vectors: dict) -> DataModel:
    """Loads a model of the vector file, with the offsets of its blocks as the vector file gives them"""
    datamodel = DataModel.load_from_file(os.path.join(ROOT, model_vectors["datamodel"]))

    for block, block_vectors in zip(datamodel.children, model_vectors["blocks"], strict=True):
        assert block.name == block_vectors["name"]
        block.offset_in_eeprom = block_vectors["offset"]
    return datamodel


def make_block(name: str = "block", params: Optional[List[Parameter]] = None, **kwargs) -> Block:
    if params is None:
        params = [Parameter(name="p1", data_type=Parameter.DataTypes.uint16, default_value=[0])]
    return Block(name=name, children=params, **kwargs)


def make_model(*blocks: Block, first_offset: int = IDENTITY_HEADER_SIZE) -> DataModel:
    """A data model with its blocks laid out one after the other, from first_offset on"""
    offset = first_offset
    for block in blocks:
        block.offset_in_eeprom = offset
        offset += (CHECKSUM_SIZE + block.data_size) * block.instance_count
    return DataModel(name="model", children=list(blocks))


class TestGoldenVectors(unittest.TestCase):
    """The values of the shared vector file, which the JavaScript tests read too"""

    def test_block_descriptors_and_identities(self):
        for model_vectors in load_vectors()["models"]:
            datamodel = load_model_with_vector_offsets(model_vectors)

            for block, expected in zip(datamodel.children, model_vectors["blocks"]):
                with self.subTest(model=model_vectors["datamodel"], block=block.name):
                    self.assertEqual(data_descriptor(block), "\n".join(expected["data_descriptor"]))
                    self.assertEqual(data_identity(block), int(expected["data_identity"], 16))
                    self.assertEqual(block_descriptor(block), "\n".join(expected["block_descriptor"]))
                    self.assertEqual(block_identity(block), int(expected["block_identity"], 16))

    def test_layout_descriptors_identities_and_headers(self):
        for model_vectors in load_vectors()["models"]:
            datamodel = load_model_with_vector_offsets(model_vectors)

            for expected in model_vectors["layouts"]:
                with self.subTest(model=model_vectors["datamodel"], endianness=expected["endianness"]):
                    layout_identity = eeprom_layout_identity(datamodel, expected["endianness"])

                    self.assertEqual(layout_descriptor(datamodel, expected["endianness"]), "\n".join(expected["descriptor"]))
                    self.assertEqual(layout_identity, int(expected["eeprom_layout_identity"], 16))
                    self.assertEqual(identity_header(layout_identity), bytes.fromhex(expected["identity_header"]))

    def test_neutral_model(self):
        """The golden values of the neutral model"""
        model_vectors = next(m for m in load_vectors()["models"] if m["datamodel"] == "test/footprint/production_like/eeprom_datamodel.json")
        datamodel = load_model_with_vector_offsets(model_vectors)

        self.assertEqual(eeprom_layout_identity(datamodel, "little"), 0x6C987786)
        self.assertEqual(identity_header(0x6C987786), bytes([0x91, 0x1F, 0x86, 0x77, 0x98, 0x6C]))
        self.assertEqual(model_vectors["blocks"][0]["offset"], IDENTITY_HEADER_SIZE)

    def test_headers(self):
        for expected in load_vectors()["headers"]:
            with self.subTest(identity=expected["eeprom_layout_identity"]):
                self.assertEqual(identity_header(int(expected["eeprom_layout_identity"], 16)), bytes.fromhex(expected["identity_header"]))


class TestDescriptors(unittest.TestCase):
    def test_data_descriptor_lists_bitfields_directly_after_their_parameter(self):
        params = [
            Parameter(name="flags", data_type=Parameter.DataTypes.uint8, children=[Bitfield(name="a", size_in_bits=1), Bitfield(name="b", size_in_bits=3)]),
            Parameter(name="values", data_type=Parameter.DataTypes.float32, multiplicity=4, default_value=[0.0] * 4),
        ]

        self.assertEqual(
            data_descriptor(make_block(params=params)),
            "mEEM-data-v2\nparam=flags,uint8,1\nbitfield=a,1\nbitfield=b,3\nparam=values,float32,4",
        )

    def test_block_descriptor(self):
        block = make_block(name="Settings", management_type=Block.ManagementTypes.WearLeveling, instance_count=4)

        self.assertEqual(
            block_descriptor(block),
            f"mEEM-block-v2\nblock=Settings\nmgmt=WearLeveling\ncount=4\ndata={data_identity(block):04X}",
        )

    def test_names_are_stripped(self):
        padded = make_block(name=" block ", params=[Parameter(name=" p1 ", children=[Bitfield(name=" b1 ")])])
        plain = make_block(name="block", params=[Parameter(name="p1", children=[Bitfield(name="b1")])])

        self.assertEqual(data_descriptor(padded), data_descriptor(plain))
        self.assertEqual(block_descriptor(padded), block_descriptor(plain))

    def test_layout_descriptor_needs_the_offsets(self):
        datamodel = DataModel(name="model", children=[make_block()])

        with self.assertRaisesRegex(ValueError, "offset"):
            layout_descriptor(datamodel, "little")

    def test_layout_descriptor_needs_a_known_endianness(self):
        with self.assertRaisesRegex(ValueError, "endianness"):
            layout_descriptor(make_model(make_block()), "middle")


class TestWhatTheIdentitiesCover(unittest.TestCase):
    """What is included in an identity, and what isn't"""

    def test_data_identity_ignores_what_has_no_effect_on_storage(self):
        block = make_block()
        other = make_block(
            name="other",
            params=[Parameter(name="p1", description="described", data_type=Parameter.DataTypes.uint16, default_value=[7])],
            management_type=Block.ManagementTypes.MultiProfile,
            instance_count=3,
            data_recovery_strategy=Block.DataRecoveryStrategies.RecoverDefaults,
            compress_defaults=False,
        )

        self.assertEqual(data_identity(block), data_identity(other))

    def test_data_identity_covers_the_parameters(self):
        base = [Parameter(name="p1", data_type=Parameter.DataTypes.uint16), Parameter(name="p2", data_type=Parameter.DataTypes.uint8)]
        variants = {
            "order": [base[1], base[0]],
            "name": [Parameter(name="renamed", data_type=Parameter.DataTypes.uint16), base[1]],
            "type": [Parameter(name="p1", data_type=Parameter.DataTypes.int16), base[1]],
            "multiplicity": [Parameter(name="p1", data_type=Parameter.DataTypes.uint16, multiplicity=2, default_value=[0, 0]), base[1]],
            "bitfield": [Parameter(name="p1", data_type=Parameter.DataTypes.uint16, children=[Bitfield(name="b1")]), base[1]],
            "bitfield width": [Parameter(name="p1", data_type=Parameter.DataTypes.uint16, children=[Bitfield(name="b1", size_in_bits=2)]), base[1]],
        }
        identities = {"base": data_identity(make_block(params=base))}

        for change, params in variants.items():
            identities[change] = data_identity(make_block(params=params))

        self.assertEqual(len(set(identities.values())), len(identities), identities)

    def test_block_identity_covers_name_management_type_instance_count_and_data(self):
        base = make_block(management_type=Block.ManagementTypes.MultiProfile, instance_count=2)
        variants = [
            make_block(name="renamed", management_type=Block.ManagementTypes.MultiProfile, instance_count=2),
            make_block(management_type=Block.ManagementTypes.WearLeveling, instance_count=2),
            make_block(management_type=Block.ManagementTypes.MultiProfile, instance_count=3),
            make_block(
                params=[Parameter(name="p1", data_type=Parameter.DataTypes.uint32)], management_type=Block.ManagementTypes.MultiProfile, instance_count=2
            ),
        ]

        identities = [block_identity(base)] + [block_identity(block) for block in variants]

        self.assertEqual(len(set(identities)), len(identities))

    def test_block_identity_ignores_the_offset(self):
        block = make_block()
        moved = copy.deepcopy(block)
        moved.offset_in_eeprom = 100

        self.assertEqual(block_identity(block), block_identity(moved))

    def test_layout_identity_covers_offsets_and_endianness(self):
        datamodel = make_model(make_block("a"), make_block("b"))
        moved = make_model(make_block("a"), make_block("b"), first_offset=IDENTITY_HEADER_SIZE + 1)

        self.assertNotEqual(eeprom_layout_identity(datamodel, "little"), eeprom_layout_identity(moved, "little"))
        self.assertNotEqual(eeprom_layout_identity(datamodel, "little"), eeprom_layout_identity(datamodel, "big"))


class TestCollisions(unittest.TestCase):
    def test_equal_descriptors_are_no_collision(self):
        # Block02 ... Block05 of the neutral model share their data identity, and so do Block06 ... Block08
        datamodel = load_model_with_vector_offsets(load_vectors()["models"][0])
        same_layout_again = copy.deepcopy(datamodel)

        check_collisions([EepromLayout("neutral", datamodel, "little"), EepromLayout("again", same_layout_again, "little")])

    def test_different_parameter_lists_with_one_data_identity(self):
        datamodel = make_model(make_block("a"), make_block("b", params=[Parameter(name="other", data_type=Parameter.DataTypes.uint8)]))

        with mock.patch.object(identity, "crc16", return_value=0x1234):
            with self.assertRaises(Exception) as raised:
                check_collisions([EepromLayout("model.json", datamodel, "little")])

        message = str(raised.exception)
        self.assertIn("Data identity 0x1234 stands for 2 different parameter lists: block 'a' of model 'model.json', block 'b' of model 'model.json'", message)
        self.assertIn("Block identity 0x1234 stands for 2 different blocks: block 'a' of model 'model.json', block 'b' of model 'model.json'", message)

    def test_different_blocks_with_one_block_identity(self):
        # Same parameters, so the data identities don't collide, while the blocks differ in their names
        datamodel = make_model(make_block("a"), make_block("b"))
        real_crc16 = identity.crc16

        def crc16_with_block_collision(data: bytes) -> int:
            return 0xBEEF if data.startswith(b"mEEM-block-v2") else real_crc16(data)

        with mock.patch.object(identity, "crc16", side_effect=crc16_with_block_collision):
            with self.assertRaises(Exception) as raised:
                check_collisions([EepromLayout("model.json", datamodel, "little")])

        message = str(raised.exception)
        self.assertIn("Block identity 0xBEEF stands for 2 different blocks: block 'a' of model 'model.json', block 'b' of model 'model.json'", message)
        self.assertNotIn("Data identity", message)

    def test_different_layouts_with_one_layout_identity(self):
        current = make_model(make_block("a"))
        legacy = make_model(make_block("a"), make_block("b"))

        with mock.patch.object(identity, "crc32", return_value=0x89ABCDEF):
            with self.assertRaises(Exception) as raised:
                check_collisions([EepromLayout("current.json", current, "little"), EepromLayout("legacy.json", legacy, "little")])

        self.assertIn("EEPROM layout identity 0x89ABCDEF stands for 2 different layouts: model 'current.json', model 'legacy.json'", str(raised.exception))


if __name__ == "__main__":
    unittest.main()
