import json
from typing import Dict, List, Literal, Optional


class PlacementDirectives:
    def __init__(
        self,
        directive_for_defaults: Optional[str] = None,
        attribute_for_defaults: Optional[str] = None,
        directive_for_cache: Optional[str] = None,
        attribute_for_cache: Optional[str] = None,
    ):
        self.directive_for_defaults: Optional[str] = directive_for_defaults
        """Compiler-specific placement directive for the 'defaults' object. Will be placed just before the 'defaults' object definition.
        You either use a placement directive or an attribute, but not both at the same time."""

        self.attribute_for_defaults: Optional[str] = attribute_for_defaults
        """Compiler-specific placement attribute for the 'detaults' object. Will be added to the 'detaults' object definition.
        You either use a placement directive or an attribute, but not both at the same time."""

        self.directive_for_cache: Optional[str] = directive_for_cache
        """Compiler-specific placement directive for the cache object. Will be placed just before the 'cache' object definition.
        You either use a placement directive or an attribute, but not both at the same time."""

        self.attribute_for_cache: Optional[str] = attribute_for_cache
        """Compiler-specific placement attribute for the 'cache' object. Will be added to the 'cache' object definition.
        You either use a placement directive or an attribute, but not both at the same time."""


class CompilerDirectives:
    """Compiler-specific directives for packing and placement."""

    def __init__(
        self,
        opening_pack_directive: Optional[str] = None,
        closing_pack_directive: Optional[str] = None,
        pack_attribute: Optional[str] = None,
        block_placement_directives: Dict[str, PlacementDirectives] = {},
    ):
        self.opening_pack_directive: Optional[str] = opening_pack_directive
        """All caches and defaults are defined as packed structures with byte alignment.
        If provided, this directive will be placed at the start of block type definitions.
        Some compilers may not require this at all, like those for 8-bit CPUs.
        You can use either a pack directive or an attribute, but never both at the same time.
        """

        self.closing_pack_directive: Optional[str] = closing_pack_directive
        """Counterpart of the opening pack directive, where applicable."""

        self.pack_attribute: Optional[str] = pack_attribute
        """Has the same effect as 'opening_pack_directive', but will be inserted inline with the definitions of block data types."""

        self.block_placement_directives: Dict[str, PlacementDirectives] = block_placement_directives
        """Block-scoped placement directives. The keys are block names as defined in the datamodel."""


class PlatformSettings:
    """Collection of platform-specific settings."""

    def __init__(
        self,
        endianness: Literal["little", "big"] = "little",
        eeprom_size: int = 256,
        eeprom_page_size: int = 0,
        page_aligned_blocks: List[str] = ["*"],
        external_headers: List[str] = [],
        enter_critical_section_operation: Optional[str] = None,
        exit_critical_section_operation: Optional[str] = None,
        compiler_directives: CompilerDirectives = CompilerDirectives(),
    ):

        self.endianness: Literal["little", "big"] = endianness
        """Endianness of the target CPU"""

        self.eeprom_size: int = eeprom_size
        """Allocated EEPROM to the mEEM, in bytes. In some cases, it might not be the whole available EEPROM."""

        self.eeprom_page_size: int = eeprom_page_size
        """Size of the EEPROM's page, in bytes.
        Set to 0 for systems, that can write only one byte at a time, like most on-chip EEPROMs or if 'Flash EEPROM emulation' driver is used."""

        self.page_aligned_blocks: List[str] = page_aligned_blocks
        """List of block names, which you want aligned to EEPROM page boundaries. The names must be present in the datamodel.
        If not specified, defaults to ['*'] (align all blocks).
        Makes sense only if eeprom_page_size > 0."""

        self.external_headers: List[str] = external_headers
        """External header files, containing forward declarations for 'enter_critical_section_operation' and 'exit_critical_section_operation'."""

        self.enter_critical_section_operation: Optional[str] = enter_critical_section_operation
        """Function/macro for designating the start of an atomic code fragment in the mEEM."""

        self.exit_critical_section_operation: Optional[str] = exit_critical_section_operation
        """Function/macro for designating the end of an atomic code fragment in the mEEM."""

        self.compiler_directives: CompilerDirectives = compiler_directives
        """Global directives for packing and block-scoped directives for placement."""

    @staticmethod
    def load_from_file(path: str) -> "PlatformSettings":
        with open(path, "r", encoding="utf-8") as f:
            data = json.loads(s=f.read())

            # Handle nested CompilerDirectives
            if "compiler_directives" in data and data["compiler_directives"] is not None:
                compiler_data = data["compiler_directives"]

                # Handle nested PlacementDirectives within CompilerDirectives
                if "block_placement_directives" in compiler_data:
                    placement_dict = {}
                    for block_name, placement_data in compiler_data["block_placement_directives"].items():
                        placement_dict[block_name] = PlacementDirectives(**placement_data)
                    compiler_data["block_placement_directives"] = placement_dict

                data["compiler_directives"] = CompilerDirectives(**compiler_data)

            return PlatformSettings(**data)

    def save_to_file(self, path: str):
        with open(path, mode="w", encoding="utf-8") as f:
            f.write(json.dumps(obj=self, indent=4, default=lambda obj: obj.__dict__))

    def validate(self):
        """Validates the platform settings. Raises an Exception if something is not ok."""
        import re

        def is_valid_identifier(string: str) -> bool:
            """Checks if a string is valid C identifier"""
            if string is None or string == "":
                return False
            if not re.match(r"^[a-zA-Z_]", string):
                return False
            if not re.match(r"^[a-zA-Z0-9_]*$", string[1:]):
                return False
            return True

        def is_valid_filename(file_name: str) -> bool:
            if len(file_name) > 255:
                return False
            return re.match(r"^[a-zA-Z0-9_.-]+$", file_name.strip('<> "')) is not None

        def is_power_of_2(num: int):
            if num <= 0:
                return False
            return (num & (num - 1)) == 0

        errors = []

        if self.eeprom_page_size < 0 or (self.eeprom_page_size > 0 and not is_power_of_2(self.eeprom_page_size)):
            errors.append(f"EEPROM page size should be 0 or positive integer and power of 2!")

        if any(map(lambda h: not is_valid_filename(h), self.external_headers)):
            errors.append(f"Some of the external headers has invalid file name")

        if self.enter_critical_section_operation != None and not is_valid_identifier(self.enter_critical_section_operation):
            errors.append(f"'enter_critical_section_operation' is not a valid C-language identifier!")

        if self.exit_critical_section_operation != None and not is_valid_identifier(self.exit_critical_section_operation):
            errors.append(f"'exit_critical_section_operation' is not a valid C-language identifier!")

        if self.compiler_directives.pack_attribute and (self.compiler_directives.opening_pack_directive or self.compiler_directives.closing_pack_directive):
            errors.append(f"You can't have both compiler pack directive and attribute defined at the same time! Pick only one.")

        for block_name, placement in self.compiler_directives.block_placement_directives.items():
            if placement.directive_for_defaults and placement.attribute_for_defaults:
                errors.append(
                    f"Block '{block_name}': You can't have both placement directive and attribute for defaults defined at the same time! Pick only one."
                )
            if placement.directive_for_cache and placement.attribute_for_cache:
                errors.append(f"Block '{block_name}': You can't have both placement directive and attribute for cache defined at the same time! Pick only one.")

        if errors:
            raise Exception("\n".join(f"- {error}" for error in errors))
