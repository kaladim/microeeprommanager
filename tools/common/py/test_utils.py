import json
import os
import sys
import unittest

sys.path.append(os.path.dirname(os.path.dirname(os.path.dirname(__file__))))
from common.py.data_model import *
from common.py.identity import IDENTITY_HEADER_SIZE
from common.py.platform_settings import PlatformSettings
from common.py.utils import attach_block_metadata

ROOT = os.path.join(os.path.dirname(__file__), "..", "..", "..")


def make_model(*sizes: int) -> DataModel:
    """A data model of basic blocks with the given data sizes, named b0, b1, ..."""
    blocks = [
        Block(name=f"b{i}", children=[Parameter(name="p", data_type=Parameter.DataTypes.uint8, multiplicity=size, default_value=[0] * size)])
        for i, size in enumerate(sizes)
    ]
    return DataModel(name="model", children=blocks)


class TestLayout(unittest.TestCase):
    """The blocks lie behind the identity header"""

    def test_blocks_follow_the_identity_header(self):
        datamodel = make_model(4, 10)

        attach_block_metadata(datamodel, PlatformSettings())

        self.assertEqual([b.offset_in_eeprom for b in datamodel.children], [IDENTITY_HEADER_SIZE, IDENTITY_HEADER_SIZE + 2 + 4])

    def test_offsets_of_the_neutral_model_are_those_of_the_vector_file(self):
        with open(os.path.join(ROOT, "test", "vectors", "identity_vectors.json"), "r", encoding="utf-8") as f:
            model_vectors = json.load(f)["models"][0]
        datamodel = DataModel.load_from_file(os.path.join(ROOT, model_vectors["datamodel"]))
        settings = PlatformSettings.load_from_file(os.path.join(ROOT, "test", "footprint", "production_like", "platform_settings_xc8.json"))

        attach_block_metadata(datamodel, settings)

        self.assertEqual([b.offset_in_eeprom for b in datamodel.children], [b["offset"] for b in model_vectors["blocks"]])

    def test_page_aligned_first_block_starts_at_the_first_page_boundary(self):
        datamodel = make_model(4, 10)

        attach_block_metadata(datamodel, PlatformSettings(eeprom_page_size=16, page_aligned_blocks=["b0"]))

        self.assertEqual([b.offset_in_eeprom for b in datamodel.children], [16, 16 + 2 + 4])

    def test_page_aligned_later_block_starts_at_the_next_page_boundary(self):
        datamodel = make_model(4, 10, 3)

        attach_block_metadata(datamodel, PlatformSettings(eeprom_page_size=16, page_aligned_blocks=["b1"]))

        self.assertEqual([b.offset_in_eeprom for b in datamodel.children], [IDENTITY_HEADER_SIZE, 16, 16 + 2 + 10])

    def test_all_blocks_page_aligned(self):
        datamodel = make_model(4, 10, 3)

        attach_block_metadata(datamodel, PlatformSettings(eeprom_page_size=16, page_aligned_blocks=["*"]))

        self.assertEqual([b.offset_in_eeprom for b in datamodel.children], [16, 32, 48])


if __name__ == "__main__":
    unittest.main()
