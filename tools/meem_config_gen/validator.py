import os
import sys

sys.path.append(os.path.dirname(__file__))
sys.path.append(os.path.dirname(os.path.dirname(__file__)))
sys.path.append(os.path.dirname(os.path.dirname(os.path.dirname(__file__))))
from colorama import Fore
from common.data_model import *
from common.platform_settings import PlatformSettings
from common.utils import attach_block_metadata


class CodeGenValidator:
    @staticmethod
    def validate(datamodel: DataModel, settings: PlatformSettings):
        attach_block_metadata(datamodel, settings)
        datamodel.validate()
        settings.validate()

        errors = []

        if len(datamodel.children) > 254:
            errors.append(f"The mEEM doesn't support more than 254 blocks.")

        last_block = datamodel.children[-1]
        required_eeprom = last_block.offset_in_eeprom + last_block.size_in_eeprom  # type: ignore

        if required_eeprom > settings.eeprom_size:
            errors.append(f"Your datamodel requires {required_eeprom} bytes of EEPROM, but you have only {settings.eeprom_size} bytes available.")

        for block in [b for b in datamodel.children if b.default_pattern != None and len(b.default_pattern) > 255]:
            errors.append(
                f"Block '{block.name}' has too large default pattern (> 255 bytes)! You may either reduce the block size or disable the compression of defaults."
            )

        # Check block alignments to EEPROM's page
        if settings.eeprom_page_size > 0:
            block_names = [block.name for block in datamodel.children]
            align_all_blocks = "*" in settings.page_aligned_blocks

            for ab in settings.page_aligned_blocks:
                if (ab not in block_names) and (not align_all_blocks):
                    raise Exception(f"There's no block named '{ab}' in the data model!")

            for block in [b for b in datamodel.children if b.management_type == Block.ManagementTypes.WearLeveling]:
                instance_size = block.data_size + datamodel.checksum_size

                if (not align_all_blocks) and block.name not in settings.page_aligned_blocks:
                    print(
                        f"{Fore.YELLOW}:Warning: it's highly recommended block '{block.name}' to be aligned to EEPROM page boundary! Add it to platform settings -> page_aligned_blocks[]."
                    )
                if (align_all_blocks or (block.name in settings.page_aligned_blocks)) and ((instance_size % settings.eeprom_page_size) != 0):
                    print(
                        f"{Fore.YELLOW}:Warning: block '{block.name}'s data size is not multiple of EEPROM page size! You may add a dummy parameter with a size of {settings.eeprom_page_size - (instance_size % settings.eeprom_page_size)} bytes to fill in."
                    )

        if errors:
            raise Exception("\n".join(f"- {error}" for error in errors))

        # No exceptions at this point, print report
        print(f"{Fore.BLUE}Your configuration requires {required_eeprom} bytes of EEPROM")
