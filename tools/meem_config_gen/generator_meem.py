import os
import sys

sys.path.append(os.path.dirname(__file__))
sys.path.append(os.path.dirname(os.path.dirname(__file__)))
from typing import Dict, List
from datetime import datetime
from common.data_model import *
from common.platform_settings import PlatformSettings
from generator_base import CodeGenerator


class CodeGen_MEEM(CodeGenerator):
    """C source code generator for the embedded EEPROM Manager library."""

    def __init__(self):
        def management_type_to_string(mt: Block.ManagementTypes) -> str:
            if mt == Block.ManagementTypes.Basic:
                return "MEEM_MGMT_BASIC"
            if mt == Block.ManagementTypes.BackupCopy:
                return "MEEM_MGMT_BACKUP_COPY"
            if mt == Block.ManagementTypes.MultiProfile:
                return "MEEM_MGMT_MULTI_PROFILE"
            if mt == Block.ManagementTypes.WearLeveling:
                return "MEEM_MGMT_WEAR_LEVELING"
            raise Exception(f"Not implemented item in {mt.__name__}!")

        def data_recovery_startegy_to_string(drs: Block.DataRecoveryStrategies) -> str:
            if drs == Block.DataRecoveryStrategies.RecoverDefaultsAndRepair:
                return "MEEM_RECOVER_DEFAULTS_AND_REPAIR"
            if drs == Block.DataRecoveryStrategies.RecoverDefaults:
                return "MEEM_RECOVER_DEFAULTS"
            raise Exception(f"Not implemented item in {drs.__name__}!")

        def data_type_to_string(dt: Parameter.DataTypes) -> str:
            if dt == Parameter.DataTypes.uint8:
                return "uint8_t"
            if dt == Parameter.DataTypes.int8:
                return "int8_t"
            if dt == Parameter.DataTypes.uint16:
                return "uint16_t"
            if dt == Parameter.DataTypes.int16:
                return "int16_t"
            if dt == Parameter.DataTypes.uint32:
                return "uint32_t"
            if dt == Parameter.DataTypes.int32:
                return "int32_t"
            if dt == Parameter.DataTypes.uint64:
                return "uint64_t"
            if dt == Parameter.DataTypes.int64:
                return "int64_t"
            if dt == Parameter.DataTypes.float32:
                return "float"
            if dt == Parameter.DataTypes.float64:
                return "double"
            raise Exception(f"Not implemented item in {dt.__name__}!")

        super().__init__()
        Parameter.DataTypes.__str__ = data_type_to_string  # type: ignore
        Block.ManagementTypes.__str__ = management_type_to_string  # type: ignore
        Block.DataRecoveryStrategies.__str__ = data_recovery_startegy_to_string  # type: ignore
        self.author = "Kaloyan Dimitrov"
        self.application_name = "mEEM - Micro EEPROM Manager"

    def generate(self, datamodel: DataModel, settings: PlatformSettings) -> Dict[str, str]:
        """Generates all necessary files as a dictionary in format file_name:file_content"""

        self._datamodel: DataModel = datamodel
        self._settings: PlatformSettings = settings

        meem_gen_config_h = self.compose_file(
            self._datamodel.name,
            "MEEM_GenConfig.h",
            self.generate_MEEM_GenConfig_h(),
        )
        meem_gen_config_c = self.compose_file(
            self._datamodel.name,
            "MEEM_GenConfig.c",
            self.generate_MEEM_GenConfig_c(),
        )
        meem_gen_interface_h = self.compose_file(
            self._datamodel.name,
            "MEEM_GenInterface.h",
            self.generate_MEEM_GenInterface_h(),
        )
        meem_gen_interface_c = self.compose_file(
            self._datamodel.name,
            "MEEM_GenInterface.c",
            self.generate_MEEM_GenInterface_c(),
        )

        return {
            "MEEM_GenConfig.h": meem_gen_config_h,
            "MEEM_GenConfig.c": meem_gen_config_c,
            "MEEM_GenInterface.h": meem_gen_interface_h,
            "MEEM_GenInterface.c": meem_gen_interface_c,
        }

    def generate_MEEM_GenConfig_h(self) -> str:
        hdr_strip_syms = ' "<>'
        last_block = self._datamodel.children[-1]

        txt = self.to_comment_box("   Dependencies", self.TextAlignment.Left) + "\n"
        txt += "#include <stdbool.h>\n"
        txt += "#include <stdint.h>\n"
        txt += "#include <stddef.h>\n"
        txt += '#include "MEEM_Linkage.h"\n'

        if len(self._settings.external_headers) > 0:
            txt += "/* User headers */\n"
            for h in self._settings.external_headers:
                txt += f'#include "{h.strip(hdr_strip_syms)}"\n'

        txt += "\n"
        txt += self.to_comment_box("   Macros", self.TextAlignment.Left) + "\n"

        txt += f"#define MEEM_GEN_TIMESTAMP             0x{self.generate_timestamp():08X}UL\n"
        txt += f"#define MEEM_AVAILABLE_EEPROM_BYTES    {self._settings.eeprom_size}U\n"
        txt += f"#define MEEM_USED_EEPROM_BYTES         {last_block.offset_in_eeprom + last_block.size_in_eeprom}U\n"  # type:ignore
        txt += "\n"
        txt += f"#define MEEM_BLOCK_COUNT               {len(self._datamodel.children)}\n"
        txt += f"#define MEEM_WORKBUFFER_SIZE           {self.calculate_workbuffer_size()}\n"
        txt += f"#define MEEM_MAX_WL_INSTANCE_COUNT     {self.get_max_wl_instance_count()}\n"
        txt += "\n"
        txt += "/* Internal optimizations control */\n"
        txt += f"#define MEEM_USING_BASIC_BLOCKS            {str(any([b for b in self._datamodel.children if b.management_type == Block.ManagementTypes.Basic])).lower()}\n"
        txt += f"#define MEEM_USING_BACKUP_COPY_BLOCKS      {str(any([b for b in self._datamodel.children if b.management_type == Block.ManagementTypes.BackupCopy])).lower()}\n"
        txt += f"#define MEEM_USING_MULTI_PROFILE_BLOCKS    {str(any([b for b in self._datamodel.children if b.management_type == Block.ManagementTypes.MultiProfile])).lower()}\n"
        txt += f"#define MEEM_USING_WEAR_LEVELING_BLOCKS    {str(any([b for b in self._datamodel.children if b.management_type == Block.ManagementTypes.WearLeveling])).lower()}\n"
        txt += "\n"

        txt += "/* Externals */\n"
        txt += self.generate_wrappers_of_external_operations() + "\n"

        txt += self.to_comment_box("   Types", self.TextAlignment.Left) + "\n"
        txt += f"typedef {str(self.get_checksum_data_type())}    MEEM_checksum_t;\n"
        txt += "\n"

        if self._settings.compiler_directives.opening_pack_directive:
            txt += self._settings.compiler_directives.opening_pack_directive + "\n"

        for block in self._datamodel.children:
            txt += self.generate_block_type(block) + "\n"

        if self._settings.compiler_directives.closing_pack_directive:
            txt += self._settings.compiler_directives.closing_pack_directive + "\n"

        txt += "\n"
        txt += self.to_comment_box("   Default values", self.TextAlignment.Left) + "\n"
        txt += self.generate_default_objects(True) + "\n"

        return txt

    def generate_MEEM_GenConfig_c(self) -> str:
        txt = self.to_comment_box("   Dependencies", self.TextAlignment.Left) + "\n"
        txt += '#include "MEEM_GenConfig.h"\n'
        txt += '#include "MEEM_Internal.h"\n'
        txt += '#include "MEEM_GenInterface.h"\n'
        txt += "#include <assert.h>\n"

        for hdr in self._settings.external_headers:
            txt += f"#include {hdr}\n"

        txt += "\n"
        txt += self.to_comment_box("   Validation routines", self.TextAlignment.Left) + "\n"
        txt += self.generate_config_validator_function() + "\n"
        txt += "\n"

        txt += self.to_comment_box("   Block configurations", self.TextAlignment.Left) + "\n"
        txt += self.generate_block_config_struct(False) + "\n"
        txt += "\n"

        txt += self.to_comment_box("   Default values", self.TextAlignment.Left) + "\n"
        txt += self.generate_default_objects(False) + "\n"

        return txt

    def generate_MEEM_GenInterface_h(self) -> str:
        txt = self.to_comment_box("   Dependencies", self.TextAlignment.Left) + "\n"
        txt += '#include "MEEM_GenConfig.h"\n'
        txt += "\n"

        txt += self.to_comment_line("----- Block IDs -----", self.TextAlignment.Left) + "\n"
        for i, block in enumerate(self._datamodel.children):
            txt += f"#define MEEM_BLOCK_{block.name}_ID    {i}\n"
        txt += "\n"

        txt += self.to_comment_box("   Parameter caches", self.TextAlignment.Left) + "\n"
        txt += self.generate_parameter_caches(True) + "\n"

        txt += self.to_comment_box("   Parameter access wrappers", self.TextAlignment.Left) + "\n"
        txt += self.to_comment_line("----- Getters -----", self.TextAlignment.Left) + "\n"
        for block in self._datamodel.children:
            for param in block.children:
                txt += self.generate_parameter_getter_function(block, param) + "\n"
        txt += "\n"
        txt += self.to_comment_line("----- Setters -----", self.TextAlignment.Left) + "\n"
        for block in self._datamodel.children:
            for param in block.children:
                txt += self.generate_parameter_setter_function(block, param) + "\n"

        return txt

    def generate_MEEM_GenInterface_c(self) -> str:
        txt = self.to_comment_box("   Dependencies", self.TextAlignment.Left) + "\n"
        txt += '#include "MEEM_GenInterface.h"\n'
        txt += '#include "MEEM_Linkage.h"\n'
        txt += "\n"

        txt += self.to_comment_box("   Public global variables", self.TextAlignment.Left) + "\n"
        txt += self.generate_parameter_caches(False) + "\n"
        return txt

    def generate_timestamp(self) -> int:
        return int((datetime.now() - datetime(year=2025, month=1, day=1)).total_seconds())

    def generate_block_type(self, block: Block) -> str:
        attr = (self._settings.compiler_directives.pack_attribute + " ") if self._settings.compiler_directives.pack_attribute else ""
        txt = f"/* {self.sanitize_description(block.description)} */\n" if block.description else ""
        txt += f"typedef struct {attr}{{\n"

        if block.management_type == Block.ManagementTypes.WearLeveling:
            txt += f"    {str(Parameter.DataTypes.uint8)}  do_not_use_me;\n"

        for param in block.children:
            array_suffix = f"[{param.multiplicity}]" if param.multiplicity > 1 else ""

            if len(param.children) > 0:
                txt += "    union {\n"
                txt += f"        {str(param.data_type)} all;\n"
                txt += "        struct {\n"
                for bitfield in param.children:
                    description = f" /* {self.sanitize_description(bitfield.description)} */" if bitfield.description else ""
                    txt += f"            {str(param.data_type)}  {bitfield.name}: {bitfield.size_in_bits};{description}\n"

                txt += "        };\n"
                txt += f"    }} {param.name}{array_suffix};\n"
            else:
                description = f" /* {self.sanitize_description(param.description)} */" if param.description else ""
                txt += f"    {str(param.data_type)}  {param.name}{array_suffix};{description}\n"

        txt += f"}} MEEM_params_{block.name}_t;\n"
        return txt

    def generate_parameter_getter_function(self, block: Block, param: Parameter) -> str:
        source_memory = f"MEEM_cache_{block.name}"
        array_suffix = "[index]" if param.multiplicity > 1 else ""
        array_index_type = f'{"uint16_t" if param.multiplicity > 255 else "uint8_t"}'
        array_arg = f'{(array_index_type + "  index") if param.multiplicity > 1 else ""}'
        txt = ""

        if len(param.children) == 0:
            txt += f"static inline {str(param.data_type)} MEEM_Get_{block.name}_{param.name}({array_arg}) {{\n"
            txt += f"    return {source_memory}.{param.name}{array_suffix};\n"
            txt += f"}}\n"
            return txt
        else:
            for bf in param.children:
                txt += f"static inline {str(param.data_type)} MEEM_Get_{block.name}_{param.name}_{bf.name}({array_arg}) {{\n"
                txt += f"    return {source_memory}.{param.name}{array_suffix}.{bf.name};\n"
                txt += f"}}\n"
        return txt

    def generate_parameter_setter_function(self, block: Block, param: Parameter) -> str:
        array_suffix = "[index]" if param.multiplicity > 1 else ""
        array_arg = f"{',index' if param.multiplicity > 1 else ''}"
        array_index_type = f'{"uint16_t" if param.multiplicity > 255 else "uint8_t"}'
        array_arg = f'{(", " + array_index_type + "  index") if param.multiplicity > 1 else ""}'
        txt = ""

        if len(param.children) == 0:
            txt += f"static inline void MEEM_Set_{block.name}_{param.name}({str(param.data_type)} value{array_arg}) {{\n"
            txt += f"    MEEM_cache_{block.name}.{param.name}{array_suffix} = value;\n"
            txt += f"}}\n"
        else:
            for bf in param.children:
                txt += f"static inline void MEEM_Set_{block.name}_{param.name}_{bf.name}({str(param.data_type)} value{array_arg}) {{\n"
                txt += f"    MEEM_cache_{block.name}.{param.name}{array_suffix}.{bf.name} = value;\n"
                txt += f"}}\n"
        return txt

    def generate_wrappers_of_external_operations(self) -> str:
        txt = f"#define MEEM_EnterCriticalSection()    {'' if self._settings.enter_critical_section_operation is None else self._settings.enter_critical_section_operation}\n"
        txt += f"#define MEEM_ExitCriticalSection()     {'' if self._settings.exit_critical_section_operation is None else self._settings.exit_critical_section_operation}\n"
        return txt

    def generate_parameter_caches(self, for_prototype: bool) -> str:
        txt = ""
        ext = "EXTERN_C " if for_prototype else ""

        for block in self._datamodel.children:
            if (block.name in self._settings.compiler_directives.block_placement_directives) and for_prototype:
                directive = self._settings.compiler_directives.block_placement_directives[block.name].directive_for_cache
                if directive:
                    txt += directive + "\n"

            txt += f"{ext}{self.generate_parameter_cache(block, for_prototype)};\n"
        return txt

    def generate_parameter_cache(self, block: Block, for_prototype: bool) -> str:
        attribute = ""
        if (block.name in self._settings.compiler_directives.block_placement_directives) and for_prototype:
            attr = self._settings.compiler_directives.block_placement_directives[block.name].attribute_for_cache
            if attr:
                attribute = attr + " "

        return f"MEEM_params_{block.name}_t {attribute}{self.generate_block_cache_object_name(block)}"

    def generate_default_objects(self, for_prototype: bool) -> str:
        txt = ""

        for block in self._datamodel.children:
            if block.default_pattern is None:
                txt += (
                    f"EXTERN_C const MEEM_params_{block.name}_t  MEEM_defaults_{block.name};"
                    if for_prototype
                    else self.generate_definition_of_defaults_as_struct(block)
                )
            else:
                txt += (
                    f"EXTERN_C const uint8_t  MEEM_defaults_{block.name}[{len(block.default_pattern)}];"
                    if for_prototype
                    else self.generate_definition_of_defaults_as_bytes(block)
                )
            txt += "\n"
        return txt

    def generate_definition_of_defaults_as_struct(self, block: Block) -> str:
        placement_attribute = ""
        if block.name in self._settings.compiler_directives.block_placement_directives:
            attr = self._settings.compiler_directives.block_placement_directives[block.name].attribute_for_defaults
            if attr is not None:
                placement_attribute = " " + attr

        txt = ""
        if block.name in self._settings.compiler_directives.block_placement_directives:
            directive = self._settings.compiler_directives.block_placement_directives[block.name].directive_for_defaults
            if directive is not None:
                txt += directive + "\n"

        txt += f"const MEEM_params_{block.name}_t{placement_attribute}  MEEM_defaults_{block.name} = {{\n"

        if block.management_type == Block.ManagementTypes.WearLeveling:
            txt += "    .do_not_use_me = 0,\n"

        for param in block.children:
            txt += self.generate_definition_of_defaults_for_parameter(param)

        txt = self.remove_last_occurrence_of(",", txt)
        txt += "};"
        return txt

    def generate_definition_of_defaults_as_bytes(self, block: Block) -> str:
        placement_attribute = ""
        if block.name in self._settings.compiler_directives.block_placement_directives:
            attr = self._settings.compiler_directives.block_placement_directives[block.name].attribute_for_defaults
            if attr is not None:
                placement_attribute = " " + attr

        placement_directive = ""
        if block.name in self._settings.compiler_directives.block_placement_directives:
            placement_directive = self._settings.compiler_directives.block_placement_directives[block.name].directive_for_defaults

        txt = ""
        if placement_directive is not None:
            txt += placement_directive + "\n"

        assert block.default_pattern != None
        txt += f'const uint8_t {placement_attribute}MEEM_defaults_{block.name}[{len(block.default_pattern)}] = {{ {", ".join(f"0x{b:02X}" for b in block.default_pattern)} }};'
        return txt

    def generate_definition_of_defaults_for_parameter(self, param: Parameter) -> str:
        txt = f"    .{param.name} = "

        if param.multiplicity > 1:
            txt += "{" + "\n"

            if len(param.children) > 0:
                for i in range(0, param.multiplicity):
                    txt += "        {\n"
                    for bf in param.children:
                        txt += f"            .{bf.name} = {self.get_default_for_bitfield(param= param, array_index= i, bitfield= bf)},\n"

                    txt = self.remove_last_occurrence_of(",", txt)
                    txt += "        },\n"
                txt = self.remove_last_occurrence_of(",", txt)
            else:
                txt += "        " + ", ".join([self.to_str(d) for d in param.default_value]) + "\n"

            txt += "    },\n"
        else:
            if len(param.children) > 0:
                txt += "    {\n"
                for bf in param.children:
                    txt += f"        .{bf.name} = {self.get_default_for_bitfield(param= param, array_index= 0, bitfield= bf)},\n"

                txt = self.remove_last_occurrence_of(",", txt)
                txt += "    },\n"
            else:
                txt += self.to_str(param.default_value[0]) + ",\n"

        return txt

    def generate_config_validator_function(self) -> str:
        txt = "void MEEM_ValidateConfiguration(void)\n"
        txt += "{\n"
        txt += (
            "    /* If any of the following assertions fails, you probably missed to specify a struct pack directive/attribute in the platform settings. */\n"
        )
        txt += "    /* Generated block types must be byte-aligned packed structures! */\n"
        for block in self._datamodel.children:
            txt += f"    assert(sizeof(MEEM_params_{block.name}_t) == {block.data_size});\n"
        txt += "}"
        return txt

    def generate_block_config_struct(self, for_prototype: bool) -> str:
        if for_prototype:
            return "EXTERN_C const MEEM_blockConfig_t   MEEM_block_config[ MEEM_BLOCK_COUNT ];"

        configs = []
        for block in self._datamodel.children:
            cast = "(const uint8_t*)&" if block.default_pattern is None else ""

            txt = f"    /* Block '{block.name}' */\n"
            txt += f"    {{\n"
            txt += f"        .cache = (uint8_t*)&MEEM_cache_{block.name},\n"
            txt += f"        .defaults = {cast}MEEM_defaults_{block.name},\n"
            txt += f"        .offset_in_eeprom = {self.to_str(block.offset_in_eeprom)},\n"  # type:ignore
            txt += f"        .data_size = {block.data_size},\n"
            txt += f"        .default_pattern_length = {0 if block.default_pattern is None else len(block.default_pattern)},\n"
            txt += f"        .instance_count = {block.instance_count},\n"
            txt += f"        .management_type = {str(block.management_type)},\n"
            txt += f"        .data_recovery_strategy = {str(block.data_recovery_strategy)}\n"
            txt += f"    }}"
            configs.append(txt)

        return "const MEEM_blockConfig_t   MEEM_block_config[ MEEM_BLOCK_COUNT ] = {\n" + ",\n".join(configs) + "\n};"

    def generate_block_cache_object_name(self, block: Block) -> str:
        return f"MEEM_cache_{block.name}"

    def sanitize_description(self, comment: str) -> str:
        return comment.replace("//", "").replace("/*", "").replace("*/", "").replace("\n", " ").replace("\r", " ").strip()

    def get_checksum_data_type(self) -> Parameter.DataTypes:
        if self._datamodel.checksum_size < 2:
            return Parameter.DataTypes.uint8
        if self._datamodel.checksum_size == 2:
            return Parameter.DataTypes.uint16
        return Parameter.DataTypes.uint32

    def get_max_wl_instance_count(self) -> int:
        instance_counts = [b.instance_count for b in self._datamodel.children if b.management_type == Block.ManagementTypes.WearLeveling]

        if instance_counts:
            return max(instance_counts)
        return 0

    def calculate_workbuffer_size(self) -> int:
        return self._datamodel.checksum_size + max([b.data_size for b in self._datamodel.children])

    def get_default_for_bitfield(self, param: Parameter, array_index: int, bitfield: Bitfield) -> int:
        offset = sum([bf.size_in_bits for bf in param.children[: param.children.index(bitfield)]])
        mask = (0xFFFFFFFFFFFFFFFF >> (64 - bitfield.size_in_bits)) << offset
        return param.default_value[array_index] & mask >> offset  # type:ignore

    def to_str(self, value: int | float) -> str:
        if isinstance(value, float):
            return str(value) + ("" if value > 3.4028235e38 else "f")

        return str(value) if (value < 10) else f"0x{value:X}"

    def remove_last_occurrence_of(self, char: str, string: str) -> str:
        """Remove last occurrence of a char in a string"""
        last_index = string.rfind(char)
        return string if last_index < 0 else string[:last_index] + string[last_index + 1 :]
