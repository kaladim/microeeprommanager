import contextlib
import io
import os
import sys
import json
import math
import tempfile
import unittest

sys.path.append(os.path.dirname(os.path.dirname(os.path.dirname(__file__))))
from common.py.data_model import *
from common.py.platform_settings import PlatformSettings
from common.py.utils import attach_block_metadata

TEST_CONFIG_DIR = os.path.join(os.path.dirname(__file__), "..", "..", "..", "test", "meem_config")


class TestChecksumSize(unittest.TestCase):
    def test_data_model_with_checksum_size_is_rejected(self):
        with open(os.path.join(TEST_CONFIG_DIR, "eeprom_datamodel.json"), mode="r", encoding="utf-8") as f:
            datamodel_json = json.load(f) | {"checksum_size": 2}

        with tempfile.TemporaryDirectory() as tmp_dir:
            path = os.path.join(tmp_dir, "datamodel.json")
            with open(path, mode="w", encoding="utf-8") as f:
                json.dump(datamodel_json, f)

            with self.assertRaisesRegex(Exception, "checksum_size"):
                DataModel.load_from_file(path)

    def test_legacy_data_model_with_checksum_size_is_accepted(self):
        """Every legacy model that v1's configurator saved has 'checksum_size', which is ignored without any message"""
        with open(os.path.join(TEST_CONFIG_DIR, "eeprom_datamodel.json"), mode="r", encoding="utf-8") as f:
            datamodel_json = json.load(f) | {"checksum_size": 1}

        with tempfile.TemporaryDirectory() as tmp_dir:
            path = os.path.join(tmp_dir, "datamodel.json")
            with open(path, mode="w", encoding="utf-8") as f:
                json.dump(datamodel_json, f)

            output = io.StringIO()
            with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
                datamodel = DataModel.load_from_file(path, legacy=True)

        self.assertEqual(output.getvalue(), "")
        self.assertFalse(hasattr(datamodel, "checksum_size"))
        attach_block_metadata(datamodel, PlatformSettings())
        self.assertEqual(datamodel.children[0].size_in_eeprom, (2 + datamodel.children[0].data_size) * datamodel.children[0].instance_count)

    def test_every_instance_has_a_2_byte_checksum(self):
        datamodel = DataModel.load_from_file(os.path.join(TEST_CONFIG_DIR, "eeprom_datamodel.json"))
        attach_block_metadata(datamodel, PlatformSettings())

        self.assertEqual(CHECKSUM_SIZE, 2)
        for block in datamodel.children:
            self.assertEqual(block.size_in_eeprom, (2 + block.data_size) * block.instance_count, block.name)


class TestDefaultValues(unittest.TestCase):
    @staticmethod
    def load(data_type: int, default_value_json: str) -> DataModel:
        """Loads a data model file with one parameter, whose default values are given as JSON text"""
        text = (
            '{"name": "model", "children": [{"name": "block", "management_type": 0, "data_recovery_strategy": 0, "children": ['
            f'{{"name": "param", "data_type": {data_type}, "multiplicity": 1, "default_value": [{default_value_json}]}}]}}]}}'
        )
        with tempfile.TemporaryDirectory() as tmp_dir:
            path = os.path.join(tmp_dir, "datamodel.json")
            with open(path, mode="w", encoding="utf-8") as f:
                f.write(text)
            return DataModel.load_from_file(path)

    def test_nan_of_a_floating_point_parameter_is_loaded_as_a_float(self):
        for data_type in (Parameter.DataTypes.float32, Parameter.DataTypes.float64):
            with self.subTest(data_type=data_type.name):
                datamodel = self.load(data_type, '"NaN"')

                datamodel.validate()
                self.assertTrue(math.isnan(datamodel.children[0].children[0].default_value[0]))

    def test_nan_of_an_integer_parameter_is_an_error(self):
        for data_type in (Parameter.DataTypes.uint8, Parameter.DataTypes.int64):
            with self.subTest(data_type=data_type.name):
                with self.assertRaisesRegex(Exception, "contains default values, that doesn't match its data type"):
                    self.load(data_type, '"NaN"').validate()

    def test_anything_else_but_a_number_is_an_error(self):
        for default_value in ('"nan"', '"1.5"', '"text"', "null", "[1.5]", "{}"):
            for data_type in (Parameter.DataTypes.uint8, Parameter.DataTypes.float32):
                with self.subTest(default_value=default_value, data_type=data_type.name):
                    with self.assertRaisesRegex(Exception, "contains default values, that doesn't match its data type"):
                        self.load(data_type, default_value).validate()

    def test_infinity_is_an_error(self):
        for default_value in ("Infinity", "-Infinity", "1e999"):
            for data_type in (Parameter.DataTypes.float32, Parameter.DataTypes.float64):
                with self.subTest(default_value=default_value, data_type=data_type.name):
                    with self.assertRaisesRegex(Exception, "has default values that are out of range"):
                        self.load(data_type, default_value).validate()


class TestNames(unittest.TestCase):
    @staticmethod
    def model_with_name(name: str, kind: str) -> DataModel:
        """A valid data model, in which the node of the given kind has the given name"""
        bitfield = Bitfield(name=name if kind == "Bitfield" else "bitfield")
        param = Parameter(name=name if kind == "Parameter" else "param", children=[bitfield])
        return DataModel(name="model", children=[Block(name=name if kind == "Block" else "block", children=[param])])

    def assert_rejected(self, name: str, kind: str, legacy: bool):
        with self.subTest(name=name, kind=kind):
            with self.assertRaisesRegex(Exception, f"(?s){kind} '.*' .*has invalid name"):
                self.model_with_name(name, kind).validate(legacy=legacy)

    def test_c_identifiers_are_accepted(self):
        for kind in ("Block", "Parameter", "Bitfield"):
            for name in ("a", "_a", "A_1"):
                with self.subTest(name=name, kind=kind):
                    self.model_with_name(name, kind).validate()

    def test_surrounding_whitespace_is_an_error(self):
        for kind in ("Block", "Parameter", "Bitfield"):
            for name in (" a", "a ", " a ", "\ta", "a\n"):
                self.assert_rejected(name, kind, legacy=False)

    def test_surrounding_whitespace_of_a_legacy_model_is_accepted(self):
        for kind in ("Block", "Parameter", "Bitfield"):
            for name in (" a", "a ", " a ", "\ta", "a\n"):
                with self.subTest(name=name, kind=kind):
                    self.model_with_name(name, kind).validate(legacy=True)

    def test_other_invalid_names_are_errors_in_a_legacy_model_too(self):
        for kind in ("Block", "Parameter", "Bitfield"):
            for name in ("", " ", "1a", "a-b", " a b "):
                self.assert_rejected(name, kind, legacy=False)
                self.assert_rejected(name, kind, legacy=True)


if __name__ == "__main__":
    unittest.main()
