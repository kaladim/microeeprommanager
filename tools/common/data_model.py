import json
from abc import ABC
from enum import IntEnum
from typing import Any, Dict, List, Optional, Sequence
from functools import cached_property


class ProtoNode(ABC):
    """Base type for all objects in the data model"""

    def __init__(self, name: str = "", description: Optional[str] = None):
        self.name: str = name
        self.description: Optional[str] = description
        self.children: Sequence["ProtoNode"] = []


class Bitfield(ProtoNode):
    """Represents a bitfield. Can only be contained by an EEPROM parameter!"""

    def __init__(self, name: str = "", description: Optional[str] = None, size_in_bits: int = 1):
        super().__init__(name=name, description=description)
        self.size_in_bits: int = size_in_bits
        del self.children


class Parameter(ProtoNode):
    """Represents an EEPROM parameter - the basic storage unit."""

    class DataTypes(IntEnum):
        uint8 = 0
        int8 = 1
        uint16 = 2
        int16 = 3
        uint32 = 4
        int32 = 5
        uint64 = 6
        int64 = 7
        float32 = 8
        float64 = 9

        @property
        def size(self) -> int:
            """Size of the type in bytes"""
            if self == Parameter.DataTypes.uint8 or self == Parameter.DataTypes.int8:
                return 1
            if self == Parameter.DataTypes.uint16 or self == Parameter.DataTypes.int16:
                return 2
            if self == Parameter.DataTypes.uint32 or self == Parameter.DataTypes.int32 or self == Parameter.DataTypes.float32:
                return 4
            return 8

    def __init__(
        self,
        name: str = "",
        description: Optional[str] = None,
        children: List[Bitfield] = [],
        data_type=DataTypes.uint8,
        multiplicity: int = 1,
        default_value: List[int | float] = [0],
    ):

        super().__init__(name=name, description=description)
        self.children: List[Bitfield] = children  # type:ignore

        self.data_type: Parameter.DataTypes = data_type
        """Data type: integer or floating-point"""

        self.multiplicity: int = multiplicity
        """If > 1, defines the parameter as an array"""

        self.default_value: List[int | float] = default_value
        """Default value. Always a list is expected, with number of elements, equal to the 'multiplicity'"""

    @property
    def size(self) -> int:
        return self.multiplicity * self.data_type.size


class Block(ProtoNode):
    """Represents a group of related EEPROM parameters."""

    class ManagementTypes(IntEnum):
        Basic = 0
        BackupCopy = 1
        MultiProfile = 2
        WearLeveling = 3

    class DataRecoveryStrategies(IntEnum):
        """Defines the strategy on data integrity failure during init"""

        RecoverDefaultsAndRepair = 0
        RecoverDefaults = 1

    def __init__(
        self,
        name: str = "",
        description: Optional[str] = None,
        children: List[Parameter] = [],
        management_type: ManagementTypes = ManagementTypes.Basic,
        instance_count: int = 1,
        data_recovery_strategy: DataRecoveryStrategies = DataRecoveryStrategies.RecoverDefaultsAndRepair,
        compress_defaults: bool = True,
    ):

        super().__init__(name=name, description=description)
        self.children: List[Parameter] = children  # type:ignore

        self.management_type: Block.ManagementTypes = management_type
        """Defines how the block's EEPROM area is managed."""

        self.instance_count: int = instance_count
        """Count of instances in the EEPROM.
        Basic blocks have always 1, backup copy - always 2, multi-profile and wear-leveling blocks have user-defined count in the range[2..15]
        """

        self.data_recovery_strategy: Block.DataRecoveryStrategies = data_recovery_strategy
        """Defines the behaviour during the init phase when block's checksum verification fails."""

        self.compress_defaults: bool = compress_defaults
        """Allow reduction of defaults to shortest possible pattern. The reduction is not guaranteed - it depends on the content of default values."""

        self.offset_in_eeprom: Optional[int] = None
        """Auto-calculated. Not for user data."""

        self.size_in_eeprom: Optional[int] = None
        """Auto-calculated. Not for user data."""

        self.default_pattern: Optional[bytes] = None
        """Auto-calculated. Not for user data."""

    @cached_property
    def data_size(self) -> int:
        """Gets the aggregate size of all parameters in the block, in bytes. For wear-leveling blocks, the size is +1, because of the extra sequence counter."""
        return sum(param.size for param in self.children) + int(self.management_type == Block.ManagementTypes.WearLeveling)


class DataModel(ProtoNode):
    """Top-level container for blocks, plus some global parameters."""

    def __init__(
        self,
        name: str = "",
        description: Optional[str] = None,
        checksum_size: int = 1,
        children: List[Block] = [],
    ):

        super().__init__(name=name, description=description)
        self.children: List[Block] = children  # type:ignore

        self.checksum_size: int = checksum_size
        """Checksum size in bytes. Applies to all blocks."""

    @staticmethod
    def load_from_file(path: str) -> "DataModel":
        def hook(dict: Dict[Any, Any]) -> Any:
            if "size_in_bits" in dict:
                obj = Bitfield()
                obj.__dict__.update(dict)
                return obj
            if "data_type" in dict:
                obj = Parameter()
                obj.__dict__.update(dict)
                setattr(obj, "data_type", Parameter.DataTypes(dict["data_type"]))
                return obj
            if "management_type" in dict:
                obj = Block()
                obj.__dict__.update(dict)
                setattr(
                    obj,
                    "management_type",
                    Block.ManagementTypes(dict["management_type"]),
                )
                setattr(
                    obj,
                    "data_recovery_strategy",
                    Block.DataRecoveryStrategies(dict["data_recovery_strategy"]),
                )
                return obj
            return dict

        with open(path, "r", encoding="utf-8") as f:
            return DataModel(**json.loads(s=f.read(), object_hook=hook))

    def save_to_file(self, path: str):
        with open(path, mode="w", encoding="utf-8") as f:
            f.write(
                json.dumps(
                    obj=self,
                    indent=4,
                    default=lambda obj: (obj.__dict__ if isinstance(obj, ProtoNode) else obj),
                )
            )

    def validate(self):
        """Validates the data model. Raises an exception if validation fails."""
        import re

        errors = []  # Container for validation messages
        all_blocks = self.children

        def is_valid_identifier(string: str) -> bool:
            """Checks if a string is valid C language identifier"""
            if string is None or string == "":
                return False
            if not re.match(r"^[a-zA-Z_]", string):
                return False
            if not re.match(r"^[a-zA-Z0-9_]*$", string[1:]):
                return False
            return True

        def get_duplicate_names(nodes: Sequence[ProtoNode]) -> List[str]:
            seen = set()
            duplicates = set()
            for n in nodes:
                if n.name in seen:
                    duplicates.add(n.name)
                seen.add(n.name)
            return list(duplicates)

        def have_type_mismatch(val: float | int, param_type: Parameter.DataTypes) -> bool:
            if isinstance(val, float) and (param_type != Parameter.DataTypes.float32) and (param_type != Parameter.DataTypes.float64):
                return True
            if isinstance(val, int) and ((param_type == Parameter.DataTypes.float32) or (param_type == Parameter.DataTypes.float64)):
                return True
            return False

        def is_out_of_range(val: float | int, param_type: Parameter.DataTypes) -> bool:
            if isinstance(val, int):
                if (param_type == Parameter.DataTypes.uint8) and (val < 0 or val > 0xFF):
                    return True
                if (param_type == Parameter.DataTypes.uint16) and (val < 0 or val > 0xFFFF):
                    return True
                if (param_type == Parameter.DataTypes.uint32) and (val < 0 or val > 0xFFFFFFFF):
                    return True
                if (param_type == Parameter.DataTypes.uint64) and (val < 0 or val > 0xFFFFFFFFFFFFFFFF):
                    return True
                if (param_type == Parameter.DataTypes.int8) and (val < -128 or val > 127):
                    return True
                if (param_type == Parameter.DataTypes.int16) and (val < -32768 or val > 32767):
                    return True
                if (param_type == Parameter.DataTypes.int32) and (val < -2147483648 or val > 2147483647):
                    return True
                if (param_type == Parameter.DataTypes.int64) and (val < -9223372036854775808 or val > 9223372036854775807):
                    return True
            if isinstance(val, float):
                if (param_type == Parameter.DataTypes.float32) and (val < -3.4e38 or val > 3.4e38):
                    return True
            return False

        def is_instance_count_valid(block: Block) -> bool:
            if block.instance_count < 1:
                return False
            if block.management_type == Block.ManagementTypes.Basic and block.instance_count != 1:
                return False
            if block.management_type == Block.ManagementTypes.BackupCopy and block.instance_count != 2:
                return False
            if block.management_type == Block.ManagementTypes.MultiProfile and (block.instance_count < 2 or block.instance_count > 15):
                return False
            if block.management_type == Block.ManagementTypes.WearLeveling and (block.instance_count < 2 or block.instance_count > 15):
                return False
            return True

        def report_accumulated_errors():
            if errors:
                raise Exception("\n".join(f"- {error}" for error in errors))

        # Top-level checks
        if self.checksum_size != 1 and self.checksum_size != 2 and self.checksum_size != 4:
            errors.append(f"The checksum size can be: 1,2 or 4 bytes")

        if len(self.children) == 0:
            errors.append("The datamodel must contain at least one block!")

        for node in self.children:
            if not isinstance(node, Block):
                errors.append(f"The datamodel contains nodes, which are not 'Blocks'!")
                report_accumulated_errors()

            for p in node.children:
                if not isinstance(p, Parameter):
                    errors.append(f"Block '{node.name}' contains nodes, which are not 'Parameters'!")
                    report_accumulated_errors()

                for bf in p.children:
                    if not isinstance(bf, Bitfield):
                        errors.append(f"Parameter '{p.name}' in block '{node.name}' contains nodes, which are not 'Bitfields'!")
                        report_accumulated_errors()

        duplicate_names = get_duplicate_names(all_blocks)
        if len(duplicate_names) > 0:
            errors.append(f"Found blocks with duplicate names: {duplicate_names}")

        # ---- Block checks ----
        for block in all_blocks:
            if not is_valid_identifier(block.name.strip()):
                errors.append(f"Block '{block.name}' has invalid name! A valid C-language identifier is expected.")

            if not is_instance_count_valid(block):
                errors.append(f"Block '{block.name}' has invalid 'instance_count': {block.instance_count}")

            if len(block.children) == 0:
                errors.append(f"Block '{block.name}' must contain at least 1 parameter!")

            duplicate_names = get_duplicate_names(block.children)
            if len(duplicate_names) > 0:
                errors.append(f"Block '{block.name}' contains parameters with duplicate names: {duplicate_names}")

            # ---- Parameter checks ----
            for param in block.children:
                if not is_valid_identifier(param.name.strip()):
                    errors.append(f"Parameter '{param.name}' in block '{block.name}' has invalid name! A valid C-language identifier is expected.")

                duplicate_names = get_duplicate_names(param.children)
                if len(duplicate_names) > 0:
                    errors.append(f"Parameter '{param.name}' in block '{block.name}' contains bitfields with duplicate names: {duplicate_names}")

                if param.multiplicity < 1:
                    errors.append(f"Parameter '{param.name}' in block '{block.name}' must have multiplicity of at least 1!")

                if len(param.default_value) != param.multiplicity:
                    errors.append(f"Parameter '{param.name}' in block '{block.name}' has count of default values, which differ from its multiplicity!")

                if (len(param.children) > 0) and (param.data_type == Parameter.DataTypes.float32 or param.data_type == Parameter.DataTypes.float64):
                    errors.append(
                        f"Parameter '{param.name}' in block '{block.name}' must be integer. Floating point types can't be used as containers for bitfields!"
                    )

                if any(map(lambda d: have_type_mismatch(d, param.data_type), param.default_value)):
                    errors.append(f"Parameter '{param.name}' in block '{block.name}' contains default values, that doesn't match its data type!")

                if any(map(lambda d: is_out_of_range(d, param.data_type), param.default_value)):
                    errors.append(f"Parameter '{param.name}' in block '{block.name}' has default values that are out of range")

                if sum([bf.size_in_bits for bf in param.children]) > (param.data_type.size * 8):
                    errors.append(f"Parameter '{param.name}' in block '{block.name}': total bitfield size exceeds parameter size")

                # ---- Bitfield checks ----
                for bf in param.children:
                    if not is_valid_identifier(bf.name.strip()):
                        errors.append(
                            f"Bitfield '{bf.name}' in parameter '{param.name}' in block '{block.name}' has invalid name! A valid C-language identifier is expected."
                        )

                    if bf.size_in_bits < 1 or bf.size_in_bits > (param.data_type.size * 8):
                        errors.append(f"Bitfield '{bf.name}' in parameter '{param.name}' in block '{block.name}' has invalid size: {bf.size_in_bits} bits")

        report_accumulated_errors()
