import os
import sys
import unittest

sys.path.append(os.path.dirname(os.path.dirname(os.path.dirname(__file__))))
from common.py.platform_settings import CompilerDirectives, PlatformSettings

TEST_CONFIG_DIR = os.path.join(os.path.dirname(__file__), "..", "..", "..", "test", "meem_config")


class TestCrcSettings(unittest.TestCase):
    def test_table_is_the_default(self):
        self.assertEqual(PlatformSettings().crc_implementation, "table")

        # Settings files without the attribute need no edit
        settings = PlatformSettings.load_from_file(os.path.join(TEST_CONFIG_DIR, "platform_settings_gcc.json"))
        self.assertEqual(settings.crc_implementation, "table")
        settings.validate()

    def test_each_implementation_is_accepted(self):
        for crc_implementation in ("table", "bitwise", "hardware"):
            with self.subTest(crc_implementation=crc_implementation):
                PlatformSettings(crc_implementation=crc_implementation).validate()  # type: ignore[arg-type]

    def test_unknown_implementation_is_rejected(self):
        with self.assertRaisesRegex(Exception, "crc_implementation"):
            PlatformSettings(crc_implementation="software").validate()  # type: ignore[arg-type]

    def test_table_placement_directive_or_attribute(self):
        PlatformSettings(compiler_directives=CompilerDirectives(crc_table_placement_directive="#pragma section")).validate()
        PlatformSettings(compiler_directives=CompilerDirectives(crc_table_placement_attribute="__flash")).validate()

    def test_table_placement_directive_and_attribute_are_rejected_together(self):
        directives = CompilerDirectives(crc_table_placement_directive="#pragma section", crc_table_placement_attribute="__flash")

        with self.assertRaisesRegex(Exception, "CRC table"):
            PlatformSettings(compiler_directives=directives).validate()

    def test_table_placement_needs_the_table(self):
        for crc_implementation in ("bitwise", "hardware"):
            for directives in (
                CompilerDirectives(crc_table_placement_directive="#pragma section"),
                CompilerDirectives(crc_table_placement_attribute="__flash"),
            ):
                with self.subTest(crc_implementation=crc_implementation), self.assertRaisesRegex(Exception, "CRC table"):
                    PlatformSettings(crc_implementation=crc_implementation, compiler_directives=directives).validate()  # type: ignore[arg-type]


if __name__ == "__main__":
    unittest.main()
