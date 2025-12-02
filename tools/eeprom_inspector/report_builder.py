import os
import struct
import sys
from datetime import datetime

sys.path.append(os.path.dirname(__file__))
sys.path.append(os.path.dirname(os.path.dirname(__file__)))
from typing import List, Literal
from jinja2 import Environment, FileSystemLoader, Template
from tzlocal import get_localzone
from dataclasses import dataclass
from common.data_model import *
from common.platform_settings import *
from common.utils import param_type_to_format, attach_block_metadata, find_index_of_most_recent_sequence_counter
from common.checksum_algo import *
from view_types import BlockView, InstanceView, ParameterView, NumView


class ReportBuilder:
    def __init__(
        self, datamodel: DataModel, settings: PlatformSettings, eeprom_image: bytearray | bytes, checksum_algo: ChecksumAlgorithm, paths: Dict[str, str]
    ) -> None:
        self.datamodel: DataModel = datamodel
        self.settings: PlatformSettings = settings
        self.eeprom: bytes = bytes(eeprom_image)
        self.checksum_algo = checksum_algo
        self.paths: Dict[str, str] = paths

    def create_report(self) -> str:
        templateEnv = Environment(loader=FileSystemLoader(searchpath=os.path.dirname(__file__)), trim_blocks=True)
        template = templateEnv.get_template("report_template.html")
        return template.render(report_header=self._create_report_header(), block_views=self._create_block_views())

    def _create_report_header(self) -> Dict[str, Any]:
        return {
            "date": datetime.now(tz=get_localzone()).strftime("%Y-%m-%d %H:%M:%S %Z (UTC%z)"),
            "bin_file_path": self.paths["bin"],
            "datamodel_path": self.paths["datamodel"],
            "settings_path": self.paths["settings"],
            "checksum_params": self.checksum_algo.parameters,
        }

    def _create_block_views(self) -> List[BlockView]:
        to_bv_management_type: dict[Block.ManagementTypes, BlockView.ManagementTypes] = {
            Block.ManagementTypes.Basic: "Basic",
            Block.ManagementTypes.BackupCopy: "Backup copy",
            Block.ManagementTypes.MultiProfile: "Multi-profile",
            Block.ManagementTypes.WearLeveling: "Wear-leveling",
        }
        blockViews: List[BlockView] = []

        for block in self.datamodel.children:
            offset = block.offset_in_eeprom
            paramViews: List[ParameterView] = []
            checksum_instances = self._collect_checksum_instances(block=block)

            paramViews.append(
                ParameterView(
                    name="Checksum ",
                    data_type=self._get_checksum_type().name,
                    description="Actual checksum",
                    instances=checksum_instances,
                )
            )
            offset += self.datamodel.checksum_size  # type:ignore[assignment]

            if block.management_type == Block.ManagementTypes.WearLeveling:
                paramViews.append(
                    ParameterView(
                        data_type=Parameter.DataTypes.uint8.name,
                        name="Sequence counter",
                        description="",
                        instances=self._collect_sequence_counter_instances(block=block, checksums=checksum_instances),
                    )
                )
                offset += 1

            for param in block.children:
                paramViews.append(
                    ParameterView(
                        name=param.name,
                        data_type=param.data_type.name,
                        description=param.description if param.description else "",
                        instances=self._collect_data_instances(block=block, offset=offset, data_type=param.data_type, multiplicity=param.multiplicity),
                    )
                )
                offset += param.size

            blockViews.append(
                BlockView(
                    name=block.name,
                    management_type=to_bv_management_type[block.management_type],
                    description=block.description if block.description else "",
                    instance_count=block.instance_count,
                    total_size=block.size_in_eeprom,  # type:ignore[assignment]
                    params=paramViews,
                )
            )
        return blockViews

    def _collect_checksum_instances(self, block: Block) -> List[InstanceView]:
        ci = self._collect_data_instances(block, block.offset_in_eeprom, self._get_checksum_type())  # type:ignore
        offset = block.offset_in_eeprom + self.datamodel.checksum_size  # type:ignore[assignment]
        step = block.data_size + self.datamodel.checksum_size

        for instance in ci:
            calculated = self.checksum_algo.calculate(self.eeprom[offset : offset + block.data_size])
            raw = int(instance.data[0].value_dec)
            instance.is_valid = calculated == raw
            offset += step

        return ci

    def _collect_sequence_counter_instances(self, block: Block, checksums: List[InstanceView]) -> List[InstanceView]:
        sci = self._collect_data_instances(block, block.offset_in_eeprom + self.datamodel.checksum_size, Parameter.DataTypes.uint8)  # type:ignore[assignment]
        counters: List[int] = sum([[int(n.value_dec) for n in c.data] for c in sci], [])

        for i, cs in enumerate(checksums):
            if not cs.is_valid:
                counters[i] = 0xFF  # Invalidate the respective counter if the instance is not valid

        most_recent_index = find_index_of_most_recent_sequence_counter(bytes(counters))

        if most_recent_index is not None:
            sci[most_recent_index].is_most_recent = True
        return sci

    def _collect_data_instances(self, block: Block, offset: int, data_type: Parameter.DataTypes, multiplicity: int = 1) -> List[InstanceView]:
        """Collects values from all instances, starting at given initial offset. All values are regarded as arrays, no matter their multiplicity."""
        instances: List[InstanceView] = []
        step: int = block.data_size + self.datamodel.checksum_size

        for _ in range(0, block.instance_count):
            num_array: List[NumView] = []
            offs = offset
            for _ in range(0, multiplicity):
                num_array.append(
                    NumView(
                        value_dec=str(self._get_normal_value(offs, data_type)),
                        value_hex=f"0x{bytes(reversed(self._get_raw_value(offs, data_type))).hex().upper()}",
                        address=f"0x{offs:04X}",
                    )
                )
                offs += data_type.size
            instances.append(InstanceView(data=num_array, is_valid=True, is_most_recent=False))
            offset += step
        return instances

    def _get_raw_value(self, offset: int, data_type: Parameter.DataTypes) -> bytes:
        return self.eeprom[offset : offset + data_type.size]

    def _get_normal_value(self, offset: int, data_type: Parameter.DataTypes) -> int | float:
        fmt = f'{">" if self.settings.endianness == "big" else "<"}{param_type_to_format[data_type]}'
        return struct.unpack(fmt, self._get_raw_value(offset, data_type))[0]

    def _get_checksum_type(self) -> Parameter.DataTypes:
        if self.datamodel.checksum_size == 1:
            return Parameter.DataTypes.uint8
        if self.datamodel.checksum_size == 2:
            return Parameter.DataTypes.uint16
        return Parameter.DataTypes.uint32
