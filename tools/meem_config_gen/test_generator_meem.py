import contextlib
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from typing import Optional, Sequence, Tuple
from unittest import mock

sys.path.append(os.path.dirname(__file__))
sys.path.append(os.path.dirname(os.path.dirname(__file__)))
from common.py import identity
from common.py.crc import CRC16_TABLE
from common.py.data_model import *
from common.py.platform_settings import CodeGenSettings, CompilerDirectives, PlatformSettings
from common.py.utils import attach_block_metadata
from generator_meem import CodeGen_MEEM
from legacy import SOURCE_NONE, SOURCE_ROUTINE, SOURCE_UNCHANGED, LegacyModel, build_legacy_configuration
from validator import CodeGenValidator

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
NEUTRAL_MODEL = os.path.join(ROOT, "test", "footprint", "production_like", "eeprom_datamodel.json")
NEUTRAL_SETTINGS = os.path.join(ROOT, "test", "footprint", "production_like", "platform_settings_xc8.json")
NEUTRAL_MIGRATION_MODEL = os.path.join(ROOT, "test", "footprint", "production_like", "eeprom_datamodel_migration.json")
TEST_CONFIG_DIR = os.path.join(ROOT, "test", "meem_config")
GENERATOR = os.path.join(os.path.dirname(__file__), "meem_config_gen.py")


def generate_neutral_model(**settings_overrides) -> Dict[str, str]:
    """Validates and generates the neutral golden model of test/footprint/production_like/, as the generator's CLI does"""
    datamodel = DataModel.load_from_file(NEUTRAL_MODEL)
    settings = PlatformSettings.load_from_file(NEUTRAL_SETTINGS)
    for name, value in settings_overrides.items():
        setattr(settings, name, value)

    CodeGenValidator.validate(datamodel=datamodel, settings=settings)
    return CodeGen_MEEM().generate(datamodel=datamodel, settings=settings)


def generate(params: List[Parameter], settings: PlatformSettings) -> Dict[str, str]:
    """Generates the sources of a single basic block. The defaults are not compressed, so they are generated as a struct initializer."""
    block = Block(name="block", children=params, compress_defaults=False)
    datamodel = DataModel(name="datamodel", children=[block])
    attach_block_metadata(datamodel=datamodel, settings=settings)
    return CodeGen_MEEM().generate(datamodel=datamodel, settings=settings)


class TestDefaultValues(unittest.TestCase):
    def test_float_literal_suffix_matches_the_parameter_type(self):
        params = [
            Parameter(name="f32", data_type=Parameter.DataTypes.float32, default_value=[0.1]),
            Parameter(name="f64", data_type=Parameter.DataTypes.float64, default_value=[0.1]),
            Parameter(name="f64_array", data_type=Parameter.DataTypes.float64, multiplicity=3, default_value=[0.5, 1e300, -1e300]),
        ]
        settings = PlatformSettings(code_gen_settings=CodeGenSettings(language_standard="c99"))

        config_c = generate(params, settings)["MEEM_GenConfig.c"]

        self.assertIn(".f32 = 0.1f,", config_c)
        self.assertIn(".f64 = 0.1,", config_c)  # A 'float' literal would initialize the 'double' with a single-precision value
        self.assertRegex(config_c, r"0\.5, 1e\+300, -1e\+300\s")

    def test_block_with_a_nan_default_is_generated_as_all_of_its_bytes(self):
        """A plain-copy defaults object, so that the firmware needs no NaN constant: the core copies it as it copies a struct"""
        nan = float("nan")
        params = [
            Parameter(name="counter", data_type=Parameter.DataTypes.uint16, default_value=[0x1234]),
            Parameter(name="f32", data_type=Parameter.DataTypes.float32, multiplicity=2, default_value=[nan, 1.5]),
            Parameter(name="f64", data_type=Parameter.DataTypes.float64, default_value=[nan]),
        ]
        little = "0x34, 0x12, 0x00, 0x00, 0xC0, 0x7F, 0x00, 0x00, 0xC0, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF8, 0x7F"
        big = "0x12, 0x34, 0x7F, 0xC0, 0x00, 0x00, 0x3F, 0xC0, 0x00, 0x00, 0x7F, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00"

        for endianness, expected in (("little", little), ("big", big)):
            with self.subTest(endianness=endianness):
                settings = PlatformSettings(endianness=endianness, code_gen_settings=CodeGenSettings(language_standard="c99"))
                files = generate(params, settings)
                config_c = files["MEEM_GenConfig.c"]

                self.assertIn(f"const uint8_t MEEM_defaults_block[18] = {{ {expected} }};", config_c)
                self.assertIn("EXTERN_C const uint8_t  MEEM_defaults_block[18];", files["MEEM_GenConfig.h"])
                self.assertIn(".defaults = MEEM_defaults_block,", config_c)
                self.assertIn(".default_pattern_length = 0,", config_c)
                self.assertNotRegex(config_c.lower(), r"\bnanf?\b")

    def test_wear_leveling_block_with_a_nan_default_keeps_a_byte_for_its_sequence_counter(self):
        param = Parameter(name="f32", data_type=Parameter.DataTypes.float32, default_value=[float("nan")])
        other = Parameter(name="other", default_value=[7])
        block = Block(name="block", children=[param, other], management_type=Block.ManagementTypes.WearLeveling, instance_count=2, compress_defaults=False)
        datamodel = DataModel(name="datamodel", children=[block])
        settings = PlatformSettings()
        attach_block_metadata(datamodel=datamodel, settings=settings)

        config_c = CodeGen_MEEM().generate(datamodel=datamodel, settings=settings)["MEEM_GenConfig.c"]

        self.assertIn("const uint8_t MEEM_defaults_block[6] = { 0x00, 0x00, 0x00, 0xC0, 0x7F, 0x07 };", config_c)

    def test_compressed_nan_defaults_use_the_default_pattern(self):
        param = Parameter(name="f32", data_type=Parameter.DataTypes.float32, multiplicity=4, default_value=[float("nan")] * 4)
        datamodel = DataModel(name="datamodel", children=[Block(name="block", children=[param])])
        settings = PlatformSettings()
        attach_block_metadata(datamodel=datamodel, settings=settings)

        config_c = CodeGen_MEEM().generate(datamodel=datamodel, settings=settings)["MEEM_GenConfig.c"]

        self.assertIn("const uint8_t MEEM_defaults_block[4] = { 0x00, 0x00, 0xC0, 0x7F };", config_c)

    def test_block_without_a_nan_default_stays_a_struct(self):
        params = [Parameter(name="f32", data_type=Parameter.DataTypes.float32, default_value=[1.5])]

        config_c = generate(params, PlatformSettings())["MEEM_GenConfig.c"]

        self.assertIn("(const uint8_t*)&MEEM_defaults_block", config_c)
        self.assertNotIn("const uint8_t MEEM_defaults_block", config_c)


class TestChecksum(unittest.TestCase):
    params = [Parameter(name="param", data_type=Parameter.DataTypes.uint32, multiplicity=3, default_value=[0, 0, 0])]

    def test_checksum_type_is_not_generated(self):
        files = generate(self.params, PlatformSettings())

        self.assertNotIn("MEEM_checksum_t", files["MEEM_GenConfig.h"])
        self.assertIn("#define MEEM_WORKBUFFER_SIZE           14\n", files["MEEM_GenConfig.h"])  # 2 B checksum + 12 B data

    def test_crc_implementation_macro(self):
        for crc_implementation, macro in (("table", "MEEM_CRC_TABLE"), ("bitwise", "MEEM_CRC_BITWISE"), ("hardware", "MEEM_CRC_HARDWARE")):
            with self.subTest(crc_implementation=crc_implementation):
                files = generate(self.params, PlatformSettings(crc_implementation=crc_implementation))  # type: ignore[arg-type]

                self.assertIn(f"#define MEEM_CRC_IMPLEMENTATION        {macro}\n", files["MEEM_GenConfig.h"])

    def test_table_is_generated_only_for_its_implementation(self):
        files = generate(self.params, PlatformSettings(crc_implementation="table"))

        self.assertIn("EXTERN_C const uint16_t MEEM_crc_table[256];", files["MEEM_GenConfig.h"])
        match = re.search(r"const uint16_t MEEM_crc_table\[256\] = \{([^}]*)\};", files["MEEM_GenConfig.c"])
        assert match is not None
        self.assertEqual([int(value, 16) for value in match.group(1).replace(",", " ").split()], CRC16_TABLE)

        for crc_implementation in ("bitwise", "hardware"):
            with self.subTest(crc_implementation=crc_implementation):
                files = generate(self.params, PlatformSettings(crc_implementation=crc_implementation))  # type: ignore[arg-type]

                self.assertNotIn("MEEM_crc_table", files["MEEM_GenConfig.h"])
                self.assertNotIn("MEEM_crc_table", files["MEEM_GenConfig.c"])

    def test_crc_unit_self_test_is_generated_only_for_hardware(self):
        files = generate(self.params, PlatformSettings(crc_implementation="hardware"))
        validator = re.search(r"void MEEM_ValidateConfiguration\(void\)\n\{(.*?)\n\}", files["MEEM_GenConfig.c"], re.DOTALL)
        assert validator is not None

        self_test = re.search(r"#ifndef NDEBUG\n(.*?)#endif", validator.group(1), re.DOTALL)
        assert self_test is not None, "The self-test must compile out with NDEBUG"
        self.assertIn('CRCIF_BeginCalculation((const uint8_t*) "123456789", 9u)', self_test.group(1))
        self.assertIn("CRCIF_GetResult(&crc)", self_test.group(1))
        self.assertIn("assert((CRCIF_OK == status) && (0x29B1u == crc));", self_test.group(1))

        for crc_implementation in ("table", "bitwise"):
            with self.subTest(crc_implementation=crc_implementation):
                files = generate(self.params, PlatformSettings(crc_implementation=crc_implementation))  # type: ignore[arg-type]

                self.assertNotIn("CRCIF_", files["MEEM_GenConfig.c"])

    def test_table_placement_directive(self):
        settings = PlatformSettings(compiler_directives=CompilerDirectives(crc_table_placement_directive='#pragma location = "CRC_TABLE"'))

        files = generate(self.params, settings)

        self.assertIn('#pragma location = "CRC_TABLE"\nconst uint16_t MEEM_crc_table[256] = {', files["MEEM_GenConfig.c"])
        self.assertIn("EXTERN_C const uint16_t MEEM_crc_table[256];", files["MEEM_GenConfig.h"])

    def test_table_placement_attribute_goes_into_definition_and_declaration(self):
        settings = PlatformSettings(compiler_directives=CompilerDirectives(crc_table_placement_attribute="__flash"))

        files = generate(self.params, settings)

        self.assertIn("const uint16_t __flash MEEM_crc_table[256] = {", files["MEEM_GenConfig.c"])
        self.assertIn("EXTERN_C const uint16_t __flash MEEM_crc_table[256];", files["MEEM_GenConfig.h"])


class TestIdentityHeader(unittest.TestCase):
    def test_golden_header_of_the_neutral_model(self):
        files = generate_neutral_model()

        self.assertIn("#define MEEM_EEPROM_LAYOUT_IDENTITY    0x6C987786UL\n", files["MEEM_GenConfig.h"])
        self.assertIn("EXTERN_C const uint8_t MEEM_identity_header[6];", files["MEEM_GenConfig.h"])
        self.assertIn("const uint8_t MEEM_identity_header[6] = { 0x91, 0x1F, 0x86, 0x77, 0x98, 0x6C };", files["MEEM_GenConfig.c"])

    def test_usage_counts_the_header(self):
        files = generate_neutral_model(eeprom_size=655)

        self.assertIn("#define MEEM_USED_EEPROM_BYTES         655U\n", files["MEEM_GenConfig.h"])
        self.assertIn(".offset_in_eeprom = 6,", files["MEEM_GenConfig.c"])

    def test_usage_beyond_the_eeprom_size_is_an_error(self):
        with self.assertRaisesRegex(Exception, "requires 655 bytes of EEPROM, including the 6-byte identity header, but you have only 654"):
            generate_neutral_model(eeprom_size=654)

    def test_identity_collision_is_an_error(self):
        # Every data and block identity becomes the same, while the blocks differ
        with mock.patch.object(identity, "crc16", return_value=0x1234):
            with self.assertRaisesRegex(Exception, "Data identity 0x1234 stands for"):
                generate_neutral_model()


class TestWorkBuffer(unittest.TestCase):
    def test_work_buffer_holds_the_identity_header(self):
        """With 1-byte blocks only, the identity header is the largest thing the work buffer holds"""
        files = generate([Parameter(name="param", data_type=Parameter.DataTypes.uint8, default_value=[0])], PlatformSettings())

        self.assertIn("#define MEEM_WORKBUFFER_SIZE           6\n", files["MEEM_GenConfig.h"])

    def test_work_buffer_holds_the_largest_instance(self):
        params = [Parameter(name="param", data_type=Parameter.DataTypes.uint8, multiplicity=5, default_value=[0] * 5)]

        files = generate(params, PlatformSettings())

        self.assertIn("#define MEEM_WORKBUFFER_SIZE           7\n", files["MEEM_GenConfig.h"])  # 2 B checksum + 5 B data


class TestMigrationFiles(unittest.TestCase):
    def test_generated_without_legacy_models(self):
        files = generate_neutral_model()

        self.assertIn("#define MEEM_LEGACY_MODEL_COUNT    0u\n", files["MEEM_Migration.h"])
        self.assertIn('#include "MEEM_Migration.h"\n', files["MEEM_Migration.c"])


class TestReproducibleOutput(unittest.TestCase):
    def test_two_runs_give_identical_files(self):
        first = generate_neutral_model()
        time.sleep(1.1)  # The timestamp and the \date line that v1 generated changed every second
        second = generate_neutral_model()

        self.assertEqual(first.keys(), second.keys())
        for file_name in first:
            with self.subTest(file=file_name):
                self.assertEqual(first[file_name], second[file_name])

    def test_no_timestamp(self):
        for file_name, content in generate_neutral_model().items():
            with self.subTest(file=file_name):
                self.assertNotIn("\\date", content)
                self.assertNotIn("MEEM_GEN_TIMESTAMP", content)


def block(name: str, size: int, management_type=Block.ManagementTypes.Basic, instance_count: int = 1) -> Block:
    """A block with one uint8 array parameter of the given size"""
    return Block(
        name=name,
        children=[Parameter(name="param", multiplicity=size, default_value=[0] * size)],
        management_type=management_type,
        instance_count=instance_count,
    )


def generate_with_legacy(current: DataModel, settings: PlatformSettings, legacy_models: List[LegacyModel]) -> Tuple[Dict[str, str], str]:
    """Validates and generates as the generator's CLI does. Returns the files and what the generator printed."""
    output = io.StringIO()
    with contextlib.redirect_stdout(output):
        legacy_models = CodeGenValidator.validate(datamodel=current, settings=settings, legacy_models=legacy_models)
        files = CodeGen_MEEM().generate(datamodel=current, settings=settings, legacy_models=legacy_models)
    return files, output.getvalue()


def legacy_block_lines(migration_c: str, k: int) -> List[str]:
    """The entries of the legacy block array of model k, without their comments"""
    match = re.search(rf"MEEM_legacy_blocks_{k}\[\] = \{{\n    /\*[^\n]*\*/\n(.*?)\n\}};", migration_c, re.DOTALL)
    assert match is not None, f"No legacy block array {k}"
    return [re.sub(r"\s+", " ", line.split("/*")[0]).strip() for line in match.group(1).splitlines()]


def source_table_lines(migration_c: str, k: int) -> Tuple[List[str], List[str]]:
    """The lines of the source table of model k: its entries, and the order of pass 2 after them if any"""
    match = re.search(rf"MEEM_legacy_sources_{k}\[(?:MEEM_BLOCK_COUNT)?\] = \{{\n(.*?)\n\}};", migration_c, re.DOTALL)
    assert match is not None, f"No source table {k}"
    lines = match.group(1).splitlines()
    pass_2 = next((i for i, line in enumerate(lines) if line.strip().startswith("/* Pass 2")), len(lines))
    return lines[:pass_2], lines[pass_2:]


def source_table(migration_c: str, k: int) -> List[str]:
    """The entries of the source table of model k, without their comments"""
    return [line.split(",")[0].strip() for line in source_table_lines(migration_c, k)[0]]


def pass_2_order(migration_c: str, k: int) -> List[str]:
    """The order of pass 2 after the source table of model k: the comment, the count, then a line per block"""
    return [line.strip() for line in source_table_lines(migration_c, k)[1]]


class TestLegacyTables(unittest.TestCase):
    @staticmethod
    def neutral_model_with_block09_changed() -> DataModel:
        """The neutral model with the change of a production application's ConfigCommon applied to Block09: p02 (uint8) is replaced by
        p06 (uint16), p04 widens to uint32, and p07 (uint8) is new"""
        datamodel = DataModel.load_from_file(NEUTRAL_MODEL)
        block09 = next(b for b in datamodel.children if b.name == "Block09")
        params = {p.name: p for p in block09.children}
        params["p04"].data_type = Parameter.DataTypes.uint32

        block09.children = [
            params["p01"],
            Parameter(name="p06", data_type=Parameter.DataTypes.uint16, default_value=[20]),
            params["p03"],
            params["p04"],
            params["p05"],
            Parameter(name="p07", data_type=Parameter.DataTypes.uint8, default_value=[128]),
        ]
        return datamodel

    def test_the_migration_fixture_is_the_neutral_model_with_block09_changed(self):
        """eeprom_datamodel_migration.json, the current model of the footprint target pic18f46k22-production-like-migration and of the
        -MigrationExample tests, holds exactly this change"""

        def blocks(datamodel: DataModel) -> str:
            return json.dumps(datamodel.children, default=lambda obj: obj.__dict__)

        self.assertEqual(blocks(DataModel.load_from_file(NEUTRAL_MIGRATION_MODEL)), blocks(self.neutral_model_with_block09_changed()))

    def test_golden_tables_of_the_neutral_model(self):
        """The unchanged neutral model as the legacy model of the changed one"""
        current = self.neutral_model_with_block09_changed()
        settings = PlatformSettings.load_from_file(NEUTRAL_SETTINGS)
        legacy = LegacyModel.load_from_files(NEUTRAL_MODEL, NEUTRAL_SETTINGS)

        files, _ = generate_with_legacy(current, settings, [legacy])
        migration_c = files["MEEM_Migration.c"]

        self.assertIn("#define MEEM_LEGACY_MODEL_COUNT    1u\n", files["MEEM_Migration.h"])
        self.assertIn("EXTERN_C const MEEM_legacyModel_t MEEM_legacy_models[MEEM_LEGACY_MODEL_COUNT];", files["MEEM_Migration.h"])
        self.assertEqual(source_table(migration_c, 0), ["MEEM_SOURCE_UNCHANGED"] * 8 + ["MEEM_SOURCE_ROUTINE", "1u", "2u", "3u"])
        self.assertEqual(
            legacy_block_lines(migration_c, 0),
            [
                "{ 450u, 8u, 1u, MEEM_MGMT_BASIC },",  # Block09: routine
                "{ 460u, 39u, 1u, MEEM_MGMT_BASIC },",  # Block10: moved
                "{ 501u, 16u, 1u, MEEM_MGMT_BASIC },",  # Block11: moved
                "{ 519u, 32u, 4u, MEEM_MGMT_WEAR_LEVELING },",  # Block12: moved
            ],
        )
        self.assertIn("{ { 0x91u, 0x1Fu, 0x86u, 0x77u, 0x98u, 0x6Cu }, MEEM_legacy_sources_0, MEEM_legacy_blocks_0 }, /* eeprom_datamodel.json */", migration_c)
        self.assertIn("#define MEEM_USED_EEPROM_BYTES         659U\n", files["MEEM_GenConfig.h"])

    def test_each_classification(self):
        settings = PlatformSettings(eeprom_page_size=0)
        current = DataModel(
            name="current",
            children=[
                block("Block_U", 2),
                block("Block_M", 3),
                block("Block_C", 4, Block.ManagementTypes.MultiProfile, 3),
                block("Block_T", 5),
                Block(name="Block_R", children=[Parameter(name="a", data_type=Parameter.DataTypes.uint16, default_value=[0])]),
                block("Block_N", 6),
            ],
        )
        legacy = LegacyModel(
            "legacy.json",
            DataModel(
                name="legacy",
                children=[
                    block("Block_U", 2),  # The same descriptor at the same offset
                    block("Block_X", 7),  # Removed, so Block_M moves
                    block("Block_M", 3),
                    block("Block_C", 4, Block.ManagementTypes.MultiProfile, 2),
                    block("Block_T", 5, Block.ManagementTypes.BackupCopy, 2),
                    Block(name="Block_R", children=[Parameter(name="a", data_type=Parameter.DataTypes.uint8, default_value=[0])]),
                ],
            ),
            PlatformSettings(eeprom_page_size=0),
        )

        files, _ = generate_with_legacy(current, settings, [legacy])
        configuration = build_legacy_configuration(current, legacy)

        self.assertEqual(configuration.sources, [SOURCE_UNCHANGED, 0, 1, 2, SOURCE_ROUTINE, SOURCE_NONE])
        self.assertEqual(
            [(e.source.name, e.target.name, e.copied) for e in configuration.entries],
            [(n, n, n != "Block_R") for n in ("Block_M", "Block_C", "Block_T", "Block_R")],
        )
        self.assertEqual(
            [e.describe() for e in configuration.entries],
            ["copied, moved", "copied, moved, 2 -> 3 instances", "copied, moved, BackupCopy -> Basic, 2 -> 1 instances", "routine"],
        )
        self.assertEqual(
            source_table(files["MEEM_Migration.c"], 0),
            ["MEEM_SOURCE_UNCHANGED", "0u", "1u", "2u", "MEEM_SOURCE_ROUTINE", "MEEM_SOURCE_NONE"],
        )
        self.assertEqual(
            legacy_block_lines(files["MEEM_Migration.c"], 0),
            [
                "{ 19u, 3u, 1u, MEEM_MGMT_BASIC },",
                "{ 24u, 4u, 2u, MEEM_MGMT_MULTI_PROFILE },",
                "{ 36u, 5u, 2u, MEEM_MGMT_BACKUP_COPY },",
                "{ 50u, 1u, 1u, MEEM_MGMT_BASIC },",
            ],
        )

    def test_legacy_model_is_laid_out_with_its_own_platform_settings(self):
        current = DataModel(name="current", children=[block("Block_A", 2), block("Block_B", 3)])
        legacy = LegacyModel("legacy.json", DataModel(name="legacy", children=[block("Block_A", 2), block("Block_B", 3)]), PlatformSettings(eeprom_page_size=0))

        files, _ = generate_with_legacy(current, PlatformSettings(eeprom_page_size=32, page_aligned_blocks=["*"]), [legacy])

        # The current blocks start at 32 and 64, those of the legacy model at 6 and 10
        self.assertEqual(legacy_block_lines(files["MEEM_Migration.c"], 0), ["{ 6u, 2u, 1u, MEEM_MGMT_BASIC },", "{ 10u, 3u, 1u, MEEM_MGMT_BASIC },"])

    def test_no_legacy_block_array_if_no_legacy_block_is_read(self):
        current = DataModel(name="current", children=[block("Block_A", 2)])
        legacy = LegacyModel("legacy.json", DataModel(name="legacy", children=[block("Block_A", 2), block("Block_Removed", 3)]), PlatformSettings())

        files, _ = generate_with_legacy(current, PlatformSettings(), [legacy])

        self.assertEqual(source_table(files["MEEM_Migration.c"], 0), ["MEEM_SOURCE_UNCHANGED"])
        self.assertNotIn("MEEM_legacy_blocks_0", files["MEEM_Migration.c"])
        self.assertRegex(files["MEEM_Migration.c"], r"\{ \{ 0x[0-9A-F]{2}u(, 0x[0-9A-F]{2}u){5} \}, MEEM_legacy_sources_0, NULL \}")

    def test_order_of_the_legacy_models_is_kept(self):
        current = DataModel(name="current", children=[block("Block_A", 2)])
        first = LegacyModel("first.json", DataModel(name="first", children=[block("Block_A", 3)]), PlatformSettings())
        second = LegacyModel("second.json", DataModel(name="second", children=[block("Block_A", 4)]), PlatformSettings())

        files, _ = generate_with_legacy(current, PlatformSettings(), [second, first])

        self.assertIn("#define MEEM_LEGACY_MODEL_COUNT    2u\n", files["MEEM_Migration.h"])
        self.assertRegex(files["MEEM_Migration.c"], r"MEEM_legacy_blocks_0 \}, /\* second\.json \*/\n.*MEEM_legacy_blocks_1 \}, /\* first\.json \*/")


def routine_of(migration_c: str, name: str) -> str:
    """The definition of a routine, from its comment to its closing brace"""
    match = re.search(rf"/\* \w+ from the parameters[^\n]*\*/\nstatic bool {name}\(.*?\n\}}\n", migration_c, re.DOTALL)
    assert match is not None, f"No routine {name}"
    return match.group(0)


def statements_of(migration_c: str, name: str) -> List[str]:
    """The statements of a routine after its check of the source, without their comments"""
    body = routine_of(migration_c, name).split("        return false;\n    }\n")[1]
    return [re.sub(r"\s*/\*.*\*/$", "", line.strip()) for line in body.splitlines()[:-2]]


def param(name: str, data_type: Parameter.DataTypes, multiplicity: int = 1, bitfields: Sequence[Tuple[str, int]] = ()) -> Parameter:
    default: List[int | float] = [0.0 if data_type >= Parameter.DataTypes.float32 else 0] * multiplicity
    return Parameter(
        name=name,
        data_type=data_type,
        multiplicity=multiplicity,
        default_value=default,
        children=[Bitfield(name=n, size_in_bits=bits) for n, bits in bitfields],
    )


def generate_routine(old: List[Parameter], new: List[Parameter], settings: Optional[PlatformSettings] = None) -> Tuple[Dict[str, str], str]:
    """Generates the routine of a single block, converted from the old parameters into the new ones"""
    current = DataModel(name="current", children=[Block(name="Block_A", children=new)])
    legacy = LegacyModel("legacy.json", DataModel(name="legacy", children=[Block(name="Block_A", children=old)]), PlatformSettings())
    return generate_with_legacy(current, settings or PlatformSettings(), [legacy])


U8, I8, U16, I16, U32, I32, U64, I64, F32, F64 = Parameter.DataTypes


class TestMigrationRoutines(unittest.TestCase):
    def test_golden_routine_of_the_neutral_model(self):
        """The neutral model with Block09 changed: p02 (uint8) is replaced by p06 (uint16), p04 widens to uint32, p07 is new"""
        current = TestLegacyTables.neutral_model_with_block09_changed()
        settings = PlatformSettings.load_from_file(NEUTRAL_SETTINGS)
        legacy = LegacyModel.load_from_files(NEUTRAL_MODEL, NEUTRAL_SETTINGS)

        files, output = generate_with_legacy(current, settings, [legacy])
        migration_c = files["MEEM_Migration.c"]

        self.assertIn(
            "/* Block09 parameters with data identity 0x67C3 */\n"
            "typedef struct {\n"
            "    uint8_t  p01;\n"
            "    uint8_t  p02;\n"
            "    uint16_t  p03;\n"
            "    uint16_t  p04;\n"
            "    uint16_t  p05;\n"
            "} MEEM_params_Block09_67C3_t;\n"
            "\n"
            "/* Block09 from the parameters with data identity 0x67C3, of 'eeprom_datamodel.json' */\n"
            "static bool MEEM_Migrate_Block09_67C3(const MEEM_legacyBlockConfig_t* source, uint8_t profile)\n"
            "{\n"
            "    const MEEM_params_Block09_67C3_t* src = (const MEEM_params_Block09_67C3_t*) MEEM_ReadLegacyInstance(source, profile);\n"
            "\n"
            "    /* If this assertion fails, you probably missed to specify a struct pack directive/attribute in the platform settings. */\n"
            "    assert(sizeof(MEEM_params_Block09_67C3_t) == 8u);\n"
            "\n"
            "    if (NULL == src)\n"
            "    {\n"
            "        return false;\n"
            "    }\n"
            "    MEEM_cache_Block09.p01 = src->p01;\n"
            "    MEEM_cache_Block09.p03 = src->p03;\n"
            "    MEEM_cache_Block09.p04 = src->p04; /* uint16 -> uint32 */\n"
            "    MEEM_cache_Block09.p05 = src->p05;\n"
            "    return true; /* p02: dropped. p06, p07: defaults */\n"
            "}\n",
            migration_c,
        )
        self.assertIn(
            "bool MEEM_MigrateBlock(uint8_t model, uint8_t block_id, uint8_t profile)\n"
            "{\n"
            "    (void) model; /* Only one legacy model */\n"
            "\n"
            "    switch (block_id)\n"
            "    {\n"
            "        case MEEM_BLOCK_Block09_ID: return MEEM_Migrate_Block09_67C3(&MEEM_legacy_blocks_0[0], profile);\n"
            "        default:                    break;\n"
            "    }\n"
            "    return false;\n"
            "}\n",
            migration_c,
        )
        self.assertIn("EXTERN_C bool MEEM_MigrateBlock(uint8_t model, uint8_t block_id, uint8_t profile);", files["MEEM_Migration.h"])
        self.assertIn('#include "MEEM_Migration.h"\n#include "MEEM_GenInterface.h"\n#include <assert.h>\n\n', migration_c)

        # The only warning: the widening of p04 loses nothing
        warnings = [line for line in output.splitlines() if "Warning" in line]
        self.assertEqual(len(warnings), 1)
        self.assertIn("Warning: block 'Block09', migrated from 'eeprom_datamodel.json': parameter 'p02' is dropped.", warnings[0])

    def test_two_legacy_models_share_a_routine(self):
        """The routine depends only on the old and the current parameters, so legacy models with the same source data identity share it"""
        current = DataModel(name="current", children=[block("Block_X", 1), Block(name="Block_A", children=[param("a", I16)])])

        def legacy(path: str, a: Parameter, x_type: Block.ManagementTypes, x_count: int) -> LegacyModel:
            blocks = [block("Block_X", 1, x_type, x_count), Block(name="Block_A", children=[a])]
            return LegacyModel(path, DataModel(name=path, children=blocks), PlatformSettings())

        # first and second differ in Block_X, which has two copies in second and so moves Block_A, but not in Block_A's parameters.
        # third has other parameters.
        first = legacy("first.json", param("a", U8), Block.ManagementTypes.Basic, 1)
        second = legacy("second.json", param("a", U8), Block.ManagementTypes.BackupCopy, 2)
        third = legacy("third.json", param("a", I8), Block.ManagementTypes.Basic, 1)

        files, output = generate_with_legacy(current, PlatformSettings(), [first, second, third])
        migration_c = files["MEEM_Migration.c"]

        self.assertEqual(len(re.findall(r"static bool MEEM_Migrate_Block_A_", migration_c)), 2)
        shared = re.search(r"static bool (MEEM_Migrate_Block_A_[0-9A-F]{4})\(", migration_c).group(1)  # type: ignore[union-attr]
        self.assertIn(f"/* Block_A from the parameters with data identity 0x{shared[-4:]}, of 'first.json', 'second.json' */", migration_c)

        # Each case passes its own model's source: Block_X is unchanged in first and third, copied in second
        self.assertIn(f"case MEEM_BLOCK_Block_A_ID: return {shared}(&MEEM_legacy_blocks_0[0], profile);", migration_c)
        self.assertIn(f"case MEEM_BLOCK_Block_A_ID: return {shared}(&MEEM_legacy_blocks_1[1], profile);", migration_c)
        self.assertRegex(
            migration_c, rf"case MEEM_BLOCK_Block_A_ID: return MEEM_Migrate_Block_A_(?!{shared[-4:]})[0-9A-F]{{4}}\(&MEEM_legacy_blocks_2\[0\], profile\);"
        )
        self.assertEqual(legacy_block_lines(migration_c, 0), ["{ 9u, 1u, 1u, MEEM_MGMT_BASIC },"])
        self.assertEqual(legacy_block_lines(migration_c, 1), ["{ 6u, 1u, 2u, MEEM_MGMT_BACKUP_COPY },", "{ 12u, 1u, 1u, MEEM_MGMT_BASIC },"])

        # Block_X is copied: it has no case
        self.assertNotIn("MEEM_BLOCK_Block_X_ID", migration_c)
        self.assertIn("    switch (model)\n    {\n        case 0u:\n", migration_c)
        self.assertNotIn("Warning", output)

    def test_without_routines_the_dispatcher_returns_false(self):
        current = DataModel(name="current", children=[block("Block_A", 2), block("Block_B", 3)])
        legacy = LegacyModel("legacy.json", DataModel(name="legacy", children=[block("Block_B", 3), block("Block_A", 2)]), PlatformSettings())

        files, _ = generate_with_legacy(current, PlatformSettings(), [legacy])
        migration_c = files["MEEM_Migration.c"]

        self.assertNotIn("static bool MEEM_Migrate_", migration_c)
        self.assertNotIn("MEEM_GenInterface.h", migration_c)
        self.assertIn("    return false; /* No block has a routine */\n}\n", migration_c)
        self.assertIn("EXTERN_C bool MEEM_MigrateBlock(uint8_t model, uint8_t block_id, uint8_t profile);", files["MEEM_Migration.h"])

    def test_no_dispatcher_without_legacy_models(self):
        files = generate_neutral_model()

        self.assertNotIn("MEEM_MigrateBlock", files["MEEM_Migration.h"])
        self.assertNotIn("MEEM_MigrateBlock", files["MEEM_Migration.c"])

    def test_integer_conversions_are_clamped_inline(self):
        old = [param("a", U16), param("b", I8), param("c", I16), param("d", U32), param("e", U8), param("f", I32)]
        new = [param("a", U8), param("b", U8), param("c", I8), param("d", I32), param("e", I16), param("f", U64)]

        files, _ = generate_routine(old, new)

        self.assertEqual(
            statements_of(files["MEEM_Migration.c"], "MEEM_Migrate_Block_A_[0-9A-F]{4}"),
            [
                "MEEM_cache_Block_A.a = (uint8_t) ((src->a > UINT8_MAX) ? UINT8_MAX : src->a);",
                "MEEM_cache_Block_A.b = (uint8_t) ((src->b < 0) ? 0 : src->b);",
                "MEEM_cache_Block_A.c = (int8_t) ((src->c < INT8_MIN) ? INT8_MIN : ((src->c > INT8_MAX) ? INT8_MAX : src->c));",
                "MEEM_cache_Block_A.d = (int32_t) ((src->d > INT32_MAX) ? INT32_MAX : src->d);",
                "MEEM_cache_Block_A.e = src->e;",
                "MEEM_cache_Block_A.f = (uint64_t) ((src->f < 0) ? 0 : src->f);",
            ],
        )
        self.assertNotIn("Conversion helpers", files["MEEM_Migration.c"])

    def test_helpers_only_for_64_bit_sources_and_floats(self):
        cases = [
            (I64, I8, "(int8_t) MEEM_ClampInt64(src->a, INT8_MIN, INT8_MAX)", "static int64_t MEEM_ClampInt64("),
            (I64, U64, "(uint64_t) MEEM_ClampInt64(src->a, 0, INT64_MAX)", "static int64_t MEEM_ClampInt64("),
            (U64, I64, "(int64_t) MEEM_ClampUint64(src->a, INT64_MAX)", "static uint64_t MEEM_ClampUint64("),
            (F64, F32, "MEEM_ConvertFloat64ToFloat32(src->a)", "static float MEEM_ConvertFloat64ToFloat32("),
            (F32, U16, "MEEM_ConvertFloat32ToUint16(src->a)", "static uint16_t MEEM_ConvertFloat32ToUint16("),
            (F64, I64, "MEEM_ConvertFloat64ToInt64(src->a)", "static int64_t MEEM_ConvertFloat64ToInt64("),
        ]
        for source, target, expression, helper in cases:
            with self.subTest(source=source.name, target=target.name):
                files, _ = generate_routine([param("a", source)], [param("a", target)])
                migration_c = files["MEEM_Migration.c"]

                self.assertEqual(statements_of(migration_c, "MEEM_Migrate_Block_A_[0-9A-F]{4}"), [f"MEEM_cache_Block_A.a = {expression};"])
                self.assertEqual(len(re.findall(r"^static (?!bool|const)", migration_c, re.MULTILINE)), 1, "Only the helper that the routine needs")
                self.assertIn(helper, migration_c)
                self.assertEqual("#include <float.h>" in migration_c, (source, target) == (F64, F32))

        # Widening and integer -> float need none
        for source, target in ((I32, I64), (U64, F32), (F32, F64), (I8, F64)):
            with self.subTest(source=source.name, target=target.name):
                files, _ = generate_routine([param("a", source)], [param("a", target)])
                self.assertNotIn("Conversion helpers", files["MEEM_Migration.c"])

    def test_float_to_integer_helper_rounds_and_clamps(self):
        files, _ = generate_routine([param("a", F32), param("b", F64)], [param("a", I16), param("b", U8)])
        migration_c = files["MEEM_Migration.c"]

        self.assertIn(
            "static int16_t MEEM_ConvertFloat32ToInt16(float value)\n"
            "{\n"
            "    int16_t result;\n"
            "\n"
            "    if (MEEM_IsNanFloat32(value))\n"
            "    {\n"
            "        return 0;\n"
            "    }\n"
            "    if (value >= (float) INT16_MAX)\n"
            "    {\n"
            "        return INT16_MAX;\n"
            "    }\n"
            "    if (value <= (float) INT16_MIN)\n"
            "    {\n"
            "        return INT16_MIN;\n"
            "    }\n"
            "    result = (int16_t) value; /* Towards zero */\n"
            "    value -= (float) result;\n"
            "    if (value >= 0.5f)\n"
            "    {\n"
            "        result++;\n"
            "    }\n"
            "    else if (value <= -0.5f)\n"
            "    {\n"
            "        result--;\n"
            "    }\n"
            "    return result;\n"
            "}\n",
            migration_c,
        )
        self.assertIn(
            "static uint8_t MEEM_ConvertFloat64ToUint8(double value)\n"
            "{\n"
            "    uint8_t result;\n"
            "\n"
            "    if (MEEM_IsNanFloat64(value) || (value <= 0.0))\n"
            "    {\n"
            "        return 0u;\n"
            "    }\n"
            "    if (value >= (double) UINT8_MAX)\n"
            "    {\n"
            "        return UINT8_MAX;\n"
            "    }\n"
            "    result = (uint8_t) value; /* Towards zero */\n"
            "    if ((value - (double) result) >= 0.5)\n"
            "    {\n"
            "        result++;\n"
            "    }\n"
            "    return result;\n"
            "}\n",
            migration_c,
        )

    def test_nan_is_told_by_its_bits(self):
        """Not by comparing: to XC8, a NaN compares like an infinity, so value != value is false"""
        files, _ = generate_routine([param("a", F32), param("b", F64)], [param("a", U8), param("b", U8)])
        migration_c = files["MEEM_Migration.c"]

        self.assertIn(
            "static bool MEEM_IsNanFloat32(float value)\n"
            "{\n"
            "    union\n"
            "    {\n"
            "        float value;\n"
            "        uint32_t bits;\n"
            "    } number;\n"
            "\n"
            "    number.value = value;\n"
            "    return (number.bits & 0x7FFFFFFFUL) > 0x7F800000UL;\n"
            "}\n",
            migration_c,
        )
        self.assertIn(
            "static bool MEEM_IsNanFloat64(double value)\n"
            "{\n"
            "    union\n"
            "    {\n"
            "        double value;\n"
            "        uint64_t bits;\n"
            "    } number;\n"
            "\n"
            "    number.value = value;\n"
            "    return (number.bits & 0x7FFFFFFFFFFFFFFFULL) > 0x7FF0000000000000ULL;\n"
            "}\n",
            migration_c,
        )
        self.assertNotIn("value != value", migration_c)

    def test_nan_test_is_generated_only_for_the_float_types_converted_to_an_integer(self):
        def is_nan_helpers(old: Parameter, new: Parameter) -> List[str]:
            return re.findall(r"static bool (MEEM_IsNan\w+)\(", generate_routine([old], [new])[0]["MEEM_Migration.c"])

        self.assertEqual(is_nan_helpers(param("a", F32), param("a", U8)), ["MEEM_IsNanFloat32"])
        self.assertEqual(is_nan_helpers(param("a", F64), param("a", I16)), ["MEEM_IsNanFloat64"])
        self.assertEqual(is_nan_helpers(param("a", F64), param("a", F32)), [])
        self.assertEqual(is_nan_helpers(param("a", U8), param("a", F32)), [])
        self.assertEqual(is_nan_helpers(param("a", U8), param("a", U16)), [])

    def test_bitfields_by_name_and_clamped(self):
        old = [
            param("p", U8, bitfields=[("a", 3), ("b", 3), ("gone", 2)]),
            param("s", I8, bitfields=[("v", 4)]),
            param("gains", U8),
            param("loses", U16, bitfields=[("x", 8)]),
            param("w", U64, bitfields=[("big", 48)]),
        ]
        new = [
            param("p", U8, bitfields=[("b", 5), ("a", 2), ("new", 1)]),
            param("s", U16, bitfields=[("v", 3)]),
            param("gains", U16, bitfields=[("lo", 8), ("hi", 8)]),
            param("loses", U8),
            param("w", U64, bitfields=[("big", 40)]),
        ]

        files, output = generate_routine(old, new)

        self.assertEqual(
            statements_of(files["MEEM_Migration.c"], "MEEM_Migrate_Block_A_[0-9A-F]{4}"),
            [
                "MEEM_cache_Block_A.p.b = src->p.b;",
                "MEEM_cache_Block_A.p.a = (uint8_t) ((src->p.a > 3) ? 3 : src->p.a);",
                "MEEM_cache_Block_A.s.v = (uint16_t) ((src->s.v < 0) ? 0 : src->s.v);",
                "MEEM_cache_Block_A.gains.all = src->gains;",
                "MEEM_cache_Block_A.loses = (uint8_t) ((src->loses.all > UINT8_MAX) ? UINT8_MAX : src->loses.all);",
                "MEEM_cache_Block_A.w.big = (uint64_t) MEEM_ClampUint64(src->w.big, 1099511627775LL);",
            ],
        )
        self.assertIn("bitfield 'p.gone' is dropped.", output)
        self.assertIn("bitfield 'p.a' narrows from uint8:3 to uint8:2: its value is clamped.", output)
        self.assertIn("bitfield 's.v' narrows from int8:4 to uint16:3: its value is clamped.", output)
        self.assertIn("parameter 'loses' narrows from uint16 to uint8: its value is clamped.", output)
        self.assertNotIn("'p.new'", output)
        self.assertNotIn("'p.b'", output)
        self.assertNotIn("'gains", output)

    def test_multiplicity_the_first_elements(self):
        old = [param("grow", U16, 2), param("shrink", U8, 4), param("conv", I32, 3), param("one", U8, 3), param("bits", U8, 3, [("a", 3)])]
        new = [param("grow", U16, 4), param("shrink", U8, 2), param("conv", I8, 2), param("one", U16), param("bits", U8, 2, [("a", 2)])]

        files, output = generate_routine(old, new)

        self.assertEqual(
            statements_of(files["MEEM_Migration.c"], "MEEM_Migrate_Block_A_[0-9A-F]{4}"),
            [
                "(void) memcpy(&MEEM_cache_Block_A.grow, &src->grow, 4u);",
                "(void) memcpy(&MEEM_cache_Block_A.shrink, &src->shrink, 2u);",
                "for (i = 0u; i < 2u; i++)",
                "{",
                "MEEM_cache_Block_A.conv[i] = (int8_t) ((src->conv[i] < INT8_MIN) ? INT8_MIN : ((src->conv[i] > INT8_MAX) ? INT8_MAX : src->conv[i]));",
                "}",
                "MEEM_cache_Block_A.one = src->one[0];",
                "for (i = 0u; i < 2u; i++)",
                "{",
                "MEEM_cache_Block_A.bits[i].a = (uint8_t) ((src->bits[i].a > 3) ? 3 : src->bits[i].a);",
                "}",
            ],
        )
        self.assertIn("    uint8_t  i;\n", files["MEEM_Migration.c"])
        self.assertIn("#include <string.h>\n", files["MEEM_Migration.c"])
        self.assertIn("elements 2 to 3 of parameter 'shrink' are dropped.", output)
        self.assertIn("element 2 of parameter 'conv' is dropped.", output)
        self.assertIn("elements 1 to 2 of parameter 'one' are dropped.", output)
        self.assertNotIn("'grow'", output)

    def test_unchanged_parameters_contiguous_on_both_sides_are_copied_together(self):
        old = [param("a", U8), param("b", U16), param("c", U8, 4), param("d", U32), param("e", U8), param("f", U8), param("g", U8), param("h", U32)]
        new = [param("n", U8), param("b", U16), param("c", U8, 4), param("d", U32), param("a", U8), param("f", U8), param("g", U8), param("h", U32)]

        files, _ = generate_routine(old, new)

        # A single value of up to 2 B is assigned, anything larger copied with memcpy(), which is smaller on the PIC18
        self.assertEqual(
            routine_of(files["MEEM_Migration.c"], "MEEM_Migrate_Block_A_[0-9A-F]{4}").split("    }\n")[1],
            "    (void) memcpy(&MEEM_cache_Block_A.b, &src->b, 10u); /* b, c, d */\n"
            "    MEEM_cache_Block_A.a = src->a;\n"
            "    (void) memcpy(&MEEM_cache_Block_A.f, &src->f, 6u); /* f, g, h */\n"
            "    return true; /* e: dropped. n: defaults */\n"
            "}\n",
        )

    def test_old_parameters_are_packed_like_the_caches(self):
        old = [param("a", U8), param("b", U32)]
        new = [param("a", U16)]

        for settings, expected in (
            (PlatformSettings(compiler_directives=CompilerDirectives(pack_attribute="__attribute__((packed))")), "typedef struct __attribute__((packed)) {\n"),
            (
                PlatformSettings(compiler_directives=CompilerDirectives(opening_pack_directive="#pragma pack(1)", closing_pack_directive="#pragma pack()")),
                "#pragma pack(1)\ntypedef struct {\n    uint8_t  a;\n    uint32_t  b;\n} MEEM_params_Block_A_",
            ),
        ):
            with self.subTest(expected=expected.splitlines()[0]):
                files, _ = generate_routine(old, new, settings)
                migration_c = files["MEEM_Migration.c"]

                self.assertIn(expected, migration_c)
                self.assertRegex(migration_c, r"assert\(sizeof\(MEEM_params_Block_A_[0-9A-F]{4}_t\) == 5u\);")
                if "#pragma" in expected:
                    self.assertRegex(migration_c, r"\} MEEM_params_Block_A_[0-9A-F]{4}_t;\n#pragma pack\(\)\n")

    def test_routine_of_a_wear_leveling_and_a_multi_profile_block(self):
        """The reader skips a source's sequence counter, and the routine leaves the cache's alone. The profile goes to the reader."""
        current = DataModel(
            name="current", children=[Block(name="Block_W", children=[param("v", U16)], management_type=Block.ManagementTypes.WearLeveling, instance_count=2)]
        )
        source = Block(name="Block_W", children=[param("v", U8)], management_type=Block.ManagementTypes.MultiProfile, instance_count=3)
        legacy = LegacyModel("legacy.json", DataModel(name="legacy", children=[source]), PlatformSettings())

        files, _ = generate_with_legacy(current, PlatformSettings(), [legacy])
        routine = routine_of(files["MEEM_Migration.c"], "MEEM_Migrate_Block_W_[0-9A-F]{4}")

        self.assertIn("MEEM_ReadLegacyInstance(source, profile);", routine)
        self.assertIn("    MEEM_cache_Block_W.v = src->v; /* uint8 -> uint16 */\n", routine)
        self.assertNotIn("do_not_use_me", routine)


def multi_profile(name: str, size: int, profiles: int) -> Block:
    """A MultiProfile block with one uint8 array parameter of the given size"""
    return block(name, size, Block.ManagementTypes.MultiProfile, profiles)


class TestPass2Order(unittest.TestCase):
    """The order of pass 2. Without page alignment, the first block starts at 6; an instance takes
    2 B of checksum and the block's data."""

    @staticmethod
    def generate(current: List[Block], *legacy_models: List[Block]) -> Tuple[Dict[str, str], List[str]]:
        """Generates the current blocks with legacy models of the given blocks. Returns the files and the order of pass 2 of the first model."""
        models = [LegacyModel(f"legacy_{k}.json", DataModel(name=f"legacy_{k}", children=blocks), PlatformSettings()) for k, blocks in enumerate(legacy_models)]
        files, _ = generate_with_legacy(DataModel(name="current", children=current), PlatformSettings(), models)
        return files, pass_2_order(files["MEEM_Migration.c"], 0)

    def test_a_block_moving_down_runs_from_profile_0(self):
        # Block_P moves from 12..24 down to 6..18: from the highest profile, profile 2 would overwrite source profile 1
        files, order = self.generate([multi_profile("Block_P", 2, 3)], [block("Block_Removed", 4), multi_profile("Block_P", 2, 3)])

        self.assertEqual(order, ["/* Pass 2: 1 block */", "1u,", "MEEM_BLOCK_Block_P_ID, MEEM_PASS2_ASCENDING,"])
        self.assertIn("#define MEEM_LEGACY_USING_PASS_2                   true\n", files["MEEM_GenConfig.h"])
        self.assertIn("static const uint8_t MEEM_legacy_sources_0[] = {\n", files["MEEM_Migration.c"])
        self.assertIn('#include "MEEM_GenInterface.h"\n', files["MEEM_Migration.c"])

    def test_a_block_moving_up_runs_from_the_highest_profile(self):
        # Block_P moves from 6..14 up to 12..24 and gains a profile: from profile 0, profile 0 would overwrite source profile 1.
        # From the highest, the new profile 2 is written first, over no source.
        _, order = self.generate([block("Block_New", 4), multi_profile("Block_P", 2, 3)], [multi_profile("Block_P", 2, 2)])

        self.assertEqual(order[2:], ["MEEM_BLOCK_Block_P_ID, MEEM_PASS2_DESCENDING,"])

    def test_a_growing_profile_size_over_its_own_source_decides_the_direction(self):
        # Block_P stays at 6, its profiles grow from 4 B to 6 B: from profile 0, profile 0 would overwrite source profile 1
        files, order = self.generate([multi_profile("Block_P", 4, 3)], [multi_profile("Block_P", 2, 3)])

        self.assertEqual(source_table(files["MEEM_Migration.c"], 0), ["MEEM_SOURCE_ROUTINE"])
        self.assertEqual(order[2:], ["MEEM_BLOCK_Block_P_ID, MEEM_PASS2_DESCENDING,"])

    def test_a_growing_profile_size_that_overwrites_its_own_source_either_way_is_an_error(self):
        # Block_P moves from 12..24 down to 6..30, its profiles grow from 4 B to 8 B. From profile 0, profile 1 (14..22) would overwrite
        # source profile 2 (20..24); from the highest, profile 1 would overwrite source profile 0 (12..16).
        with self.assertRaisesRegex(
            Exception,
            "Legacy model 'legacy_0.json': no order of its MultiProfile blocks keeps every old profile until it has been read:\n"
            "- block 'Block_P' would overwrite its own old profiles before reading them, in either direction\n",
        ):
            self.generate([multi_profile("Block_P", 6, 3)], [block("Block_Removed", 4), multi_profile("Block_P", 2, 3)])

    def test_two_blocks_that_swap_places_are_an_error(self):
        with self.assertRaisesRegex(
            Exception,
            "no order of its MultiProfile blocks keeps every old profile until it has been read:\n"
            "- block 'Block_B' would overwrite the old profiles of block 'Block_A'\n"
            "- block 'Block_A' would overwrite the old profiles of block 'Block_B'\n"
            "Change the layout",
        ):
            self.generate([multi_profile("Block_B", 2, 2), multi_profile("Block_A", 2, 2)], [multi_profile("Block_A", 2, 2), multi_profile("Block_B", 2, 2)])

    def test_a_block_runs_after_the_one_whose_old_place_it_overwrites(self):
        # Block_B (old place 22..30) moves down onto the old place of Block_A (6..14), which moves up onto Block_Gap's (14..22).
        # Block_B comes first in definition order, but must wait until Block_A has been read.
        _, order = self.generate(
            [multi_profile("Block_B", 2, 2), multi_profile("Block_A", 2, 2)],
            [multi_profile("Block_A", 2, 2), block("Block_Gap", 6), multi_profile("Block_B", 2, 2)],
        )

        self.assertEqual(
            order, ["/* Pass 2: 2 blocks */", "2u,", "MEEM_BLOCK_Block_A_ID, MEEM_PASS2_ASCENDING,", "MEEM_BLOCK_Block_B_ID, MEEM_PASS2_ASCENDING,"]
        )

    def test_only_multi_profile_blocks_from_multi_profile_sources(self):
        # Block_P becomes MultiProfile, Block_Q stops being MultiProfile: both are filled in pass 1. Block_U is unchanged.
        current = [multi_profile("Block_U", 2, 2), multi_profile("Block_P", 2, 3), block("Block_Q", 2)]
        legacy = [multi_profile("Block_U", 2, 2), block("Block_Removed", 1), block("Block_P", 2), multi_profile("Block_Q", 2, 2)]

        files, order = self.generate(current, legacy)

        self.assertEqual(order, [])
        self.assertEqual(source_table(files["MEEM_Migration.c"], 0), ["MEEM_SOURCE_UNCHANGED", "0u", "1u"])
        self.assertIn("static const uint8_t MEEM_legacy_sources_0[MEEM_BLOCK_COUNT] = {\n", files["MEEM_Migration.c"])
        self.assertIn("#define MEEM_LEGACY_USING_PASS_2                   false\n", files["MEEM_GenConfig.h"])
        self.assertNotIn("MEEM_GenInterface.h", files["MEEM_Migration.c"])

    def test_a_legacy_model_without_pass_2_gets_the_count_0(self):
        current = [multi_profile("Block_P", 2, 2)]

        files, order = self.generate(current, [block("Block_Removed", 1), multi_profile("Block_P", 2, 2)], [block("Block_P", 2)])

        self.assertEqual(order, ["/* Pass 2: 1 block */", "1u,", "MEEM_BLOCK_Block_P_ID, MEEM_PASS2_ASCENDING,"])
        self.assertEqual(pass_2_order(files["MEEM_Migration.c"], 1), ["/* Pass 2: no blocks */", "0u,"])

    def test_the_order_is_per_legacy_model(self):
        """A block moves down from one legacy model, and up from another"""
        current = [block("Block_X", 4), multi_profile("Block_P", 2, 3)]

        files, order = self.generate(current, [block("Block_X", 10), multi_profile("Block_P", 2, 3)], [block("Block_X", 1), multi_profile("Block_P", 2, 3)])

        self.assertEqual(order[2:], ["MEEM_BLOCK_Block_P_ID, MEEM_PASS2_ASCENDING,"])
        self.assertEqual(pass_2_order(files["MEEM_Migration.c"], 1)[2:], ["MEEM_BLOCK_Block_P_ID, MEEM_PASS2_DESCENDING,"])


class TestLegacyModelChecks(unittest.TestCase):
    current = DataModel(name="current", children=[block("Block_A", 2), block("Block_B", 3)])

    def test_legacy_model_identical_to_the_current_one_is_ignored_with_an_info(self):
        # Other defaults, the same layout
        same_layout = DataModel(name="same", children=[block("Block_A", 2), block("Block_B", 3)])
        same_layout.children[0].children[0].default_value = [1, 2]

        files, output = generate_with_legacy(self.current, PlatformSettings(), [LegacyModel("same.json", same_layout, PlatformSettings())])

        self.assertIn("Info: legacy model 'same.json' has the same EEPROM layout as the current model, so it's ignored.", output)
        self.assertNotIn("Warning", output)
        self.assertIn("#define MEEM_LEGACY_MODEL_COUNT    0u\n", files["MEEM_Migration.h"])
        self.assertNotIn("MEEM_legacy_models", files["MEEM_Migration.c"])

    def test_legacy_model_identical_to_another_one_is_ignored_with_an_info(self):
        def legacy(path: str) -> LegacyModel:
            return LegacyModel(path, DataModel(name="old", children=[block("Block_A", 2)]), PlatformSettings())

        files, output = generate_with_legacy(self.current, PlatformSettings(), [legacy("first.json"), legacy("second.json")])

        self.assertIn("Info: legacy model 'second.json' has the same EEPROM layout as legacy model 'first.json', so it's ignored.", output)
        self.assertNotIn("Warning", output)
        self.assertIn("#define MEEM_LEGACY_MODEL_COUNT    1u\n", files["MEEM_Migration.h"])
        self.assertIn("/* first.json */", files["MEEM_Migration.c"])
        self.assertNotIn("second.json", files["MEEM_Migration.c"])

    def test_legacy_model_with_another_endianness_is_an_error(self):
        legacy = LegacyModel("legacy.json", DataModel(name="old", children=[block("Block_A", 2)]), PlatformSettings(endianness="big"))

        with self.assertRaisesRegex(Exception, "Legacy model 'legacy.json' is for a big-endian target, the current model for a little-endian one"):
            generate_with_legacy(self.current, PlatformSettings(endianness="little"), [legacy])

    def test_legacy_usage_beyond_its_eeprom_size_is_an_error(self):
        legacy = LegacyModel("legacy.json", DataModel(name="old", children=[block("Block_A", 20)]), PlatformSettings(eeprom_size=27))

        with self.assertRaisesRegex(
            Exception, "Legacy model 'legacy.json' requires 28 bytes of EEPROM, including the 6-byte identity header, but its platform settings have only 27"
        ):
            generate_with_legacy(self.current, PlatformSettings(), [legacy])

    def test_invalid_legacy_model_is_an_error_that_names_it(self):
        legacy = LegacyModel("legacy.json", DataModel(name="old", children=[block("Block_A", 2, Block.ManagementTypes.BackupCopy, 3)]), PlatformSettings())

        with self.assertRaisesRegex(Exception, "Legacy model 'legacy.json':\n- Block 'Block_A' has invalid 'instance_count': 3"):
            generate_with_legacy(self.current, PlatformSettings(), [legacy])

    def test_surrounding_whitespace_in_a_name_of_the_current_model_is_an_error(self):
        current = DataModel(name="current", children=[block(" Block_A ", 2)])

        with self.assertRaisesRegex(Exception, "Block ' Block_A ' has invalid name"):
            generate_with_legacy(current, PlatformSettings(), [])

    def test_surrounding_whitespace_in_the_names_of_a_legacy_model_changes_nothing(self):
        settings_path = os.path.join(TEST_CONFIG_DIR, "platform_settings_msvc.json")

        for current_file, legacy_file in (
            ("eeprom_datamodel_conversions.json", "eeprom_datamodel_conversions_legacy.json"),
            ("eeprom_datamodel_migration.json", "eeprom_datamodel_migration_legacy_0.json"),
        ):
            with self.subTest(legacy=legacy_file):
                legacy_path = os.path.join(TEST_CONFIG_DIR, legacy_file)
                padded = LegacyModel.load_from_files(legacy_path, settings_path)
                for padded_block in padded.datamodel.children:
                    padded_block.name = f" {padded_block.name} "
                    for param in padded_block.children:
                        param.name = f" {param.name} "
                        for bitfield in param.children:
                            bitfield.name = f" {bitfield.name} "

                def generate_from(legacy: LegacyModel) -> Dict[str, str]:
                    current = DataModel.load_from_file(os.path.join(TEST_CONFIG_DIR, current_file))
                    return generate_with_legacy(current, PlatformSettings.load_from_file(settings_path), [legacy])[0]

                self.assertEqual(generate_from(padded), generate_from(LegacyModel.load_from_files(legacy_path, settings_path)))

    def test_identity_collision_across_models_is_an_error(self):
        legacy = LegacyModel("legacy.json", DataModel(name="old", children=[block("Block_A", 2), block("Block_B", 4)]), PlatformSettings())
        attach_block_metadata(legacy.datamodel, legacy.settings)
        current_descriptor = identity.data_descriptor(self.current.children[1]).encode("utf-8")
        legacy_descriptor = identity.data_descriptor(legacy.datamodel.children[1]).encode("utf-8")
        real_crc16 = identity.crc16

        # Block_B's parameter lists differ, but get the same data identity
        def colliding_crc16(data: bytes) -> int:
            return real_crc16(current_descriptor if data == legacy_descriptor else data)

        with mock.patch.object(identity, "crc16", side_effect=colliding_crc16):
            with self.assertRaisesRegex(
                Exception,
                r"Data identity 0x[0-9A-F]{4} stands for 2 different parameter lists: block 'Block_B' of model 'current', block 'Block_B' of model 'legacy.json'",
            ):
                generate_with_legacy(self.current, PlatformSettings(), [legacy])

    def test_checksum_size_of_a_legacy_model_is_ignored_without_any_message(self):
        with open(os.path.join(TEST_CONFIG_DIR, "eeprom_datamodel_migration_legacy_0.json"), mode="r", encoding="utf-8") as f:
            datamodel_json = json.load(f) | {"checksum_size": 1}

        with tempfile.TemporaryDirectory() as tmp_dir:
            path = os.path.join(tmp_dir, "legacy.json")
            with open(path, mode="w", encoding="utf-8") as f:
                json.dump(datamodel_json, f)

            output = io.StringIO()
            with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
                legacy = LegacyModel.load_from_files(path, os.path.join(TEST_CONFIG_DIR, "platform_settings_msvc.json"))
            current = DataModel.load_from_file(os.path.join(TEST_CONFIG_DIR, "eeprom_datamodel_migration.json"))
            files, generator_output = generate_with_legacy(
                current, PlatformSettings.load_from_file(os.path.join(TEST_CONFIG_DIR, "platform_settings_msvc.json")), [legacy]
            )

        self.assertEqual(output.getvalue(), "")
        self.assertNotIn("checksum", generator_output.lower())
        self.assertIn("#define MEEM_LEGACY_MODEL_COUNT    1u\n", files["MEEM_Migration.h"])

    def test_crc_implementation_of_a_legacy_model_is_ignored(self):
        legacy = LegacyModel("legacy.json", DataModel(name="old", children=[block("Block_A", 3)]), PlatformSettings(crc_implementation="hardware"))

        files, _ = generate_with_legacy(self.current, PlatformSettings(crc_implementation="table"), [legacy])

        self.assertIn("#define MEEM_CRC_IMPLEMENTATION        MEEM_CRC_TABLE\n", files["MEEM_GenConfig.h"])
        self.assertNotIn("CRCIF_", files["MEEM_GenConfig.c"])


class TestLegacySizes(unittest.TestCase):
    def test_work_buffer_holds_a_larger_legacy_source(self):
        current = DataModel(name="current", children=[block("Block_A", 2)])
        legacy = LegacyModel("legacy.json", DataModel(name="old", children=[block("Block_A", 10)]), PlatformSettings())

        files, _ = generate_with_legacy(current, PlatformSettings(), [legacy])

        self.assertIn("#define MEEM_WORKBUFFER_SIZE           12\n", files["MEEM_GenConfig.h"])  # 2 B checksum + 10 B of the routine's source

    def test_wear_leveling_count_covers_the_legacy_sources(self):
        current = DataModel(name="current", children=[block("Block_W", 3, Block.ManagementTypes.WearLeveling, 2)])
        legacy = LegacyModel("legacy.json", DataModel(name="old", children=[block("Block_W", 3, Block.ManagementTypes.WearLeveling, 6)]), PlatformSettings())

        files, _ = generate_with_legacy(current, PlatformSettings(), [legacy])
        config_h = files["MEEM_GenConfig.h"]

        self.assertIn("#define MEEM_MAX_WL_INSTANCE_COUNT     6\n", config_h)
        self.assertIn("#define MEEM_LEGACY_USING_WEAR_LEVELING_BLOCKS     true\n", config_h)
        for management_type in ("BASIC", "BACKUP_COPY", "MULTI_PROFILE"):
            self.assertRegex(config_h, rf"#define MEEM_LEGACY_USING_{management_type}_BLOCKS +false\n")

    def test_legacy_block_that_isnt_read_counts_for_nothing(self):
        """A removed legacy block grows neither size, and its management type compiles no reader"""
        current = DataModel(name="current", children=[block("Block_A", 2)])
        legacy = LegacyModel(
            "legacy.json",
            DataModel(
                name="old",
                children=[
                    block("Block_A", 2),
                    block("Block_Removed", 30, Block.ManagementTypes.WearLeveling, 15),
                    block("Block_Renamed", 20, Block.ManagementTypes.BackupCopy, 2),
                ],
            ),
            PlatformSettings(eeprom_size=1024),
        )

        files, _ = generate_with_legacy(current, PlatformSettings(), [legacy])
        config_h = files["MEEM_GenConfig.h"]

        self.assertIn("#define MEEM_LEGACY_MODEL_COUNT    1u\n", files["MEEM_Migration.h"])
        self.assertIn("#define MEEM_WORKBUFFER_SIZE           6\n", config_h)
        self.assertIn("#define MEEM_MAX_WL_INSTANCE_COUNT     0\n", config_h)
        self.assertEqual(re.findall(r"#define MEEM_LEGACY_USING_\w+ +(\w+)\n", config_h), ["false"] * 5)

    def test_without_legacy_models_no_reader_is_compiled(self):
        config_h = generate_neutral_model()["MEEM_GenConfig.h"]

        self.assertEqual(re.findall(r"#define MEEM_LEGACY_USING_\w+ +(\w+)\n", config_h), ["false"] * 5)


class TestLegacyCommandLine(unittest.TestCase):
    def run_generator(self, output_dir: str, legacy_args: List[str]) -> subprocess.CompletedProcess:
        return subprocess.run(
            [
                sys.executable,
                GENERATOR,
                os.path.join(TEST_CONFIG_DIR, "eeprom_datamodel_migration.json"),
                os.path.join(TEST_CONFIG_DIR, "platform_settings_msvc.json"),
                output_dir,
                *legacy_args,
            ],
            capture_output=True,
            text=True,
        )

    def test_legacy_is_repeatable_in_any_order(self):
        legacy_0 = [
            "--legacy",
            os.path.join(TEST_CONFIG_DIR, "eeprom_datamodel_migration_legacy_0.json"),
            os.path.join(TEST_CONFIG_DIR, "platform_settings_msvc.json"),
        ]
        legacy_1 = [
            "--legacy",
            os.path.join(TEST_CONFIG_DIR, "eeprom_datamodel_migration_legacy_1.json"),
            os.path.join(TEST_CONFIG_DIR, "platform_settings_migration_legacy_1.json"),
        ]

        for order, expected_first in ((legacy_0 + legacy_1, "legacy_0"), (legacy_1 + legacy_0, "legacy_1")):
            with self.subTest(first=expected_first), tempfile.TemporaryDirectory() as tmp_dir:
                result = self.run_generator(tmp_dir, order)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

                with open(os.path.join(tmp_dir, "MEEM_Migration.c"), encoding="utf-8") as f:
                    migration_c = f.read()
                with open(os.path.join(tmp_dir, "MEEM_Migration.h"), encoding="utf-8") as f:
                    self.assertIn("#define MEEM_LEGACY_MODEL_COUNT    2u\n", f.read())
                self.assertRegex(migration_c, rf"MEEM_legacy_blocks_0 \}}, /\* eeprom_datamodel_migration_{expected_first}\.json \*/")

    def make_legacy_dir(self, parent: str, models: dict) -> str:
        """A directory of models: for each subdirectory, the test configuration's data model and settings"""
        legacy_dir = os.path.join(parent, "legacy_models")
        os.makedirs(legacy_dir)
        with open(os.path.join(legacy_dir, "legacy-models.json"), "w", encoding="utf-8") as f:
            f.write("{}")  # Files at the top are ignored

        for subdirectory, (datamodel_file, settings_file) in models.items():
            os.makedirs(os.path.join(legacy_dir, subdirectory))
            for source, target in ((datamodel_file, "data-model.json"), (settings_file, "platform-settings.json")):
                if source is not None:
                    shutil.copyfile(os.path.join(TEST_CONFIG_DIR, source), os.path.join(legacy_dir, subdirectory, target))
        return legacy_dir

    def read_generated(self, output_dir: str, file_name: str) -> str:
        with open(os.path.join(output_dir, file_name), encoding="utf-8") as f:
            return f.read()

    def test_legacy_dir_holds_one_model_per_subdirectory_named_by_it(self):
        with tempfile.TemporaryDirectory() as tmp_dir:
            legacy_dir = self.make_legacy_dir(
                tmp_dir,
                {
                    "v2.0": ("eeprom_datamodel_migration_legacy_1.json", "platform_settings_migration_legacy_1.json"),
                    "v1.0": ("eeprom_datamodel_migration_legacy_0.json", "platform_settings_msvc.json"),
                },
            )
            output_dir = os.path.join(tmp_dir, "generated")
            os.makedirs(output_dir)

            result = self.run_generator(output_dir, ["--legacy-dir", legacy_dir])
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

            # In the order of the subdirectories' names, each named by its subdirectory in the generated code
            self.assertIn("#define MEEM_LEGACY_MODEL_COUNT    2u\n", self.read_generated(output_dir, "MEEM_Migration.h"))
            migration_c = self.read_generated(output_dir, "MEEM_Migration.c")
            self.assertRegex(migration_c, r"MEEM_legacy_blocks_0 \}, /\* v1\.0/data-model\.json \*/")
            self.assertRegex(migration_c, r"MEEM_legacy_blocks_1 \}, /\* v2\.0/data-model\.json \*/")

    def test_legacy_dir_goes_with_legacy(self):
        with tempfile.TemporaryDirectory() as tmp_dir:
            legacy_dir = self.make_legacy_dir(tmp_dir, {"v2.0": ("eeprom_datamodel_migration_legacy_1.json", "platform_settings_migration_legacy_1.json")})
            legacy_0 = [
                "--legacy",
                os.path.join(TEST_CONFIG_DIR, "eeprom_datamodel_migration_legacy_0.json"),
                os.path.join(TEST_CONFIG_DIR, "platform_settings_msvc.json"),
            ]

            result = self.run_generator(tmp_dir, ["--legacy-dir", legacy_dir, *legacy_0])
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("#define MEEM_LEGACY_MODEL_COUNT    2u\n", self.read_generated(tmp_dir, "MEEM_Migration.h"))

    def test_empty_legacy_dir_adds_no_model(self):
        with tempfile.TemporaryDirectory() as tmp_dir:
            legacy_dir = self.make_legacy_dir(tmp_dir, {})

            result = self.run_generator(tmp_dir, ["--legacy-dir", legacy_dir])
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("#define MEEM_LEGACY_MODEL_COUNT    0u\n", self.read_generated(tmp_dir, "MEEM_Migration.h"))

    def test_missing_legacy_dir_is_an_error(self):
        with tempfile.TemporaryDirectory() as tmp_dir:
            result = self.run_generator(tmp_dir, ["--legacy-dir", os.path.join(tmp_dir, "missing")])

            self.assertEqual(result.returncode, 1)
            self.assertIn("doesn't exist", result.stdout + result.stderr)

    def test_legacy_dir_subdirectory_without_both_files_is_an_error(self):
        with tempfile.TemporaryDirectory() as tmp_dir:
            legacy_dir = self.make_legacy_dir(tmp_dir, {"v1.0": ("eeprom_datamodel_migration_legacy_0.json", None)})

            result = self.run_generator(tmp_dir, ["--legacy-dir", legacy_dir])

            self.assertEqual(result.returncode, 1)
            self.assertIn("has no platform-settings.json", result.stdout + result.stderr)


class TestIncludes(unittest.TestCase):
    def test_external_headers_produce_valid_include_directives(self):
        params = [Parameter(name="param", data_type=Parameter.DataTypes.uint8, default_value=[0])]
        settings = PlatformSettings(external_headers=["critical_section.h", "<os.h>"])

        files = generate(params, settings)

        self.assertIn('#include "critical_section.h"', files["MEEM_GenConfig.h"])
        self.assertIn('#include "os.h"', files["MEEM_GenConfig.h"])

        for file_name, content in files.items():
            for line in content.splitlines():
                if line.startswith("#include"):
                    self.assertRegex(line, r'^#include ("[^"]+"|<[^>]+>)$', file_name)


if __name__ == "__main__":
    unittest.main()
