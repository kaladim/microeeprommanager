import os
import re
import sys
import unittest

sys.path.append(os.path.dirname(__file__))
sys.path.append(os.path.dirname(os.path.dirname(__file__)))
from common.data_model import *
from common.platform_settings import CodeGenSettings, PlatformSettings
from common.utils import attach_block_metadata
from generator_meem import CodeGen_MEEM


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
