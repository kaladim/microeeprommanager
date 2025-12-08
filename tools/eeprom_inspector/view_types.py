from dataclasses import dataclass
from typing import List, Literal


@dataclass
class NumView:
    """Represents a numeric value in dec, hex formats + address"""

    value_dec: str
    value_hex: str
    address: str


@dataclass
class InstanceView:
    is_valid: bool
    is_most_recent: bool
    data: List[NumView]


@dataclass
class ParameterView:
    name: str
    data_type: str
    description: str
    instances: List[InstanceView]


@dataclass
class BlockView:
    ManagementTypes = Literal["Basic", "Backup copy", "Multi-profile", "Wear-leveling"]

    name: str
    management_type: ManagementTypes
    description: str
    instance_count: int
    total_size: int
    params: List[ParameterView]
