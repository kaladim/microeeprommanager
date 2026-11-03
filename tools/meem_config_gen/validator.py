import os
import sys

sys.path.append(os.path.dirname(__file__))
sys.path.append(os.path.dirname(os.path.dirname(__file__)))
sys.path.append(os.path.dirname(os.path.dirname(os.path.dirname(__file__))))
from colorama import Fore
from common.py.data_model import *
from typing import List, Sequence
from common.py.identity import IDENTITY_HEADER_SIZE, EepromLayout, check_collisions, layout_descriptor
from common.py.platform_settings import PlatformSettings
from common.py.utils import attach_block_metadata
from legacy import LegacyModel


class CodeGenValidator:
    @staticmethod
    def validate(datamodel: DataModel, settings: PlatformSettings, legacy_models: Sequence[LegacyModel] = ()) -> List[LegacyModel]:
        """Validates the current model and the legacy models, and lays each of them out with its own platform settings.
        Returns the legacy models to generate: all but those with the same EEPROM layout as the current model or an earlier legacy model."""
        attach_block_metadata(datamodel, settings)
        datamodel.validate()
        settings.validate()

        errors = []

        if len(datamodel.children) > 254:
            errors.append(f"The mEEM doesn't support more than 254 blocks.")

        last_block = datamodel.children[-1]
        required_eeprom = last_block.offset_in_eeprom + last_block.size_in_eeprom  # type: ignore

        if required_eeprom > settings.eeprom_size:
            errors.append(
                f"Your datamodel requires {required_eeprom} bytes of EEPROM, including the {IDENTITY_HEADER_SIZE}-byte identity header, "
                f"but you have only {settings.eeprom_size} bytes available."
            )

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
                instance_size = block.data_size + CHECKSUM_SIZE

                if (not align_all_blocks) and block.name not in settings.page_aligned_blocks:
                    print(
                        f"{Fore.YELLOW}:Warning: it's highly recommended block '{block.name}' to be aligned to EEPROM page boundary! Add it to platform settings -> page_aligned_blocks[]."
                    )
                if (align_all_blocks or (block.name in settings.page_aligned_blocks)) and ((instance_size % settings.eeprom_page_size) != 0):
                    print(
                        f"{Fore.YELLOW}:Warning: block '{block.name}'s data size is not multiple of EEPROM page size! You may add a dummy parameter with a size of {settings.eeprom_page_size - (instance_size % settings.eeprom_page_size)} bytes to fill in."
                    )

        for legacy in legacy_models:
            errors.extend(CodeGenValidator.validate_legacy_model(legacy, settings))

        if errors:
            raise Exception("\n".join(f"- {error}" for error in errors))

        # Across all models: a legacy model is told by its identity header, and its blocks are paired by their descriptors
        check_collisions(
            [EepromLayout(datamodel.name, datamodel, settings.endianness)]
            + [EepromLayout(legacy.path, legacy.datamodel, legacy.settings.endianness) for legacy in legacy_models]
        )
        distinct_legacy_models = CodeGenValidator.drop_identical_legacy_models(datamodel, settings, legacy_models)

        # No exceptions at this point, print report
        print(f"{Fore.BLUE}Your configuration requires {required_eeprom} bytes of EEPROM")
        return distinct_legacy_models

    @staticmethod
    def validate_legacy_model(legacy: LegacyModel, settings: PlatformSettings) -> List[str]:
        """Validates a legacy model and lays it out with its own platform settings, whose 'crc_implementation' is ignored.
        Returns the errors, each of which names the model."""
        attach_block_metadata(legacy.datamodel, legacy.settings)

        try:
            legacy.datamodel.validate(legacy=True)
            legacy.settings.validate()
        except Exception as x:
            return [f"Legacy model '{legacy.path}':\n{x}"]

        errors = []
        last_block = legacy.datamodel.children[-1]
        required_eeprom = last_block.offset_in_eeprom + last_block.size_in_eeprom  # type: ignore

        if required_eeprom > legacy.settings.eeprom_size:
            errors.append(
                f"Legacy model '{legacy.path}' requires {required_eeprom} bytes of EEPROM, including the {IDENTITY_HEADER_SIZE}-byte identity header, "
                f"but its platform settings have only {legacy.settings.eeprom_size} bytes available."
            )

        if legacy.settings.endianness != settings.endianness:
            errors.append(
                f"Legacy model '{legacy.path}' is for a {legacy.settings.endianness}-endian target, the current model for a {settings.endianness}-endian one: "
                f"the firmware couldn't read its instances."
            )
        return errors

    @staticmethod
    def drop_identical_legacy_models(datamodel: DataModel, settings: PlatformSettings, legacy_models: Sequence[LegacyModel]) -> List[LegacyModel]:
        """A legacy model with the same EEPROM layout as the current model or an earlier legacy model has the same identity header,
        so it produces no configuration"""
        seen = {layout_descriptor(datamodel, settings.endianness): "the current model"}
        distinct = []

        for legacy in legacy_models:
            descriptor = layout_descriptor(legacy.datamodel, legacy.settings.endianness)

            if descriptor in seen:
                print(f"{Fore.BLUE}Info: legacy model '{legacy.path}' has the same EEPROM layout as {seen[descriptor]}, so it's ignored.{Fore.RESET}")
            else:
                seen[descriptor] = f"legacy model '{legacy.path}'"
                distinct.append(legacy)
        return distinct
