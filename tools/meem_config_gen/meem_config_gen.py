import os
import sys
import argparse

sys.path.append(os.path.dirname(__file__))
sys.path.append(os.path.dirname(os.path.dirname(__file__)))
from colorama import Fore
from common.data_model import *
from common.platform_settings import PlatformSettings
from validator import CodeGenValidator
from generator_meem import CodeGen_MEEM

if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="mEEM configuration code generator",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )

    parser.add_argument("datamodel", help="Path to a datamodel.json")
    parser.add_argument("settings", help="Path to a platform_settings.json")
    parser.add_argument("output_dir", help="Output directory")
    args = parser.parse_args()

    try:
        datamodel = DataModel.load_from_file(path=args.datamodel)
        settings = PlatformSettings.load_from_file(path=args.settings)

        CodeGenValidator.validate(datamodel=datamodel, settings=settings)
        generated_source_files = CodeGen_MEEM().generate(datamodel=datamodel, settings=settings)

        for file_name in generated_source_files:
            with open(
                os.path.join(
                    args.output_dir,
                    file_name,
                ),
                "w",
            ) as f:
                f.write(generated_source_files[file_name])

        print(f"{Fore.GREEN}mEEM_config_gen: success{Fore.RESET}")
        exit_code = 0
    except Exception as x:
        import traceback

        print(f"{Fore.RED}mEEM_config_gen :: error :: {x}{traceback.print_exc()}{Fore.RESET}")
        exit_code = 1

    sys.exit(exit_code)
