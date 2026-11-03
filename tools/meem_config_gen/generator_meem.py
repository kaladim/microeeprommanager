import math
import os
import sys

sys.path.append(os.path.dirname(__file__))
sys.path.append(os.path.dirname(os.path.dirname(__file__)))
from typing import Dict, List, Optional, Sequence, Tuple
from colorama import Fore
from common.py.crc import CRC16_TABLE
from common.py.data_model import *
from common.py.identity import IDENTITY_HEADER_SIZE, eeprom_layout_identity, identity_header
from common.py.platform_settings import PlatformSettings
from common.py.utils import extract_defaults
from generator_base import CodeGenerator
from legacy import SOURCE_NONE, SOURCE_ROUTINE, SOURCE_UNCHANGED, LegacyConfiguration, LegacyModel, build_legacy_configuration
from routines import FLOAT64_TO_FLOAT32, Routine, RoutineWriter, collect_routines, helper_definitions


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

    def generate(self, datamodel: DataModel, settings: PlatformSettings, legacy_models: Sequence[LegacyModel] = ()) -> Dict[str, str]:
        """Generates all necessary files as a dictionary in format file_name:file_content.
        The legacy models are those that CodeGenValidator.validate() returned, laid out with their own platform settings."""

        self._datamodel: DataModel = datamodel
        self._settings: PlatformSettings = settings
        self._layout_identity: int = eeprom_layout_identity(datamodel, settings.endianness)
        self._legacy: List[LegacyConfiguration] = [build_legacy_configuration(datamodel, model) for model in legacy_models]
        self._routines: List[Routine] = collect_routines(datamodel, self._legacy)

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
        meem_migration_h = self.compose_file(
            self._datamodel.name,
            "MEEM_Migration.h",
            self.generate_MEEM_Migration_h(),
        )
        meem_migration_c = self.compose_file(
            self._datamodel.name,
            "MEEM_Migration.c",
            self.generate_MEEM_Migration_c(),
        )

        return {
            "MEEM_GenConfig.h": meem_gen_config_h,
            "MEEM_GenConfig.c": meem_gen_config_c,
            "MEEM_GenInterface.h": meem_gen_interface_h,
            "MEEM_GenInterface.c": meem_gen_interface_c,
            "MEEM_Migration.h": meem_migration_h,
            "MEEM_Migration.c": meem_migration_c,
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

        txt += f"#define MEEM_EEPROM_LAYOUT_IDENTITY    0x{self._layout_identity:08X}UL\n"
        txt += f"#define MEEM_AVAILABLE_EEPROM_BYTES    {self._settings.eeprom_size}U\n"
        txt += f"#define MEEM_USED_EEPROM_BYTES         {last_block.offset_in_eeprom + last_block.size_in_eeprom}U\n"  # type:ignore
        txt += "\n"
        txt += f"#define MEEM_BLOCK_COUNT               {len(self._datamodel.children)}\n"
        txt += f"#define MEEM_WORKBUFFER_SIZE           {self.calculate_workbuffer_size()}\n"
        txt += f"#define MEEM_MAX_WL_INSTANCE_COUNT     {self.get_max_wl_instance_count()}\n"
        txt += f"#define MEEM_CRC_IMPLEMENTATION        {self.get_crc_implementation_macro()}\n"
        txt += "\n"
        txt += "/* Internal optimizations control */\n"
        txt += f"#define MEEM_USING_BASIC_BLOCKS            {str(any([b for b in self._datamodel.children if b.management_type == Block.ManagementTypes.Basic])).lower()}\n"
        txt += f"#define MEEM_USING_BACKUP_COPY_BLOCKS      {str(any([b for b in self._datamodel.children if b.management_type == Block.ManagementTypes.BackupCopy])).lower()}\n"
        txt += f"#define MEEM_USING_MULTI_PROFILE_BLOCKS    {str(any([b for b in self._datamodel.children if b.management_type == Block.ManagementTypes.MultiProfile])).lower()}\n"
        txt += f"#define MEEM_USING_WEAR_LEVELING_BLOCKS    {str(any([b for b in self._datamodel.children if b.management_type == Block.ManagementTypes.WearLeveling])).lower()}\n"
        txt += "\n"
        txt += "/* Management types among the legacy sources that the core reads, whose readers it compiles */\n"
        for macro, management_type in (
            ("MEEM_LEGACY_USING_BASIC_BLOCKS", Block.ManagementTypes.Basic),
            ("MEEM_LEGACY_USING_BACKUP_COPY_BLOCKS", Block.ManagementTypes.BackupCopy),
            ("MEEM_LEGACY_USING_MULTI_PROFILE_BLOCKS", Block.ManagementTypes.MultiProfile),
            ("MEEM_LEGACY_USING_WEAR_LEVELING_BLOCKS", Block.ManagementTypes.WearLeveling),
        ):
            used = any(source.management_type == management_type for source in self.get_legacy_sources())
            txt += f"#define {macro:<42} {str(used).lower()}\n"
        txt += "/* Whether a legacy model has MultiProfile blocks from MultiProfile sources: then the core compiles pass 2, whose order follows the source tables */\n"
        txt += f"#define {'MEEM_LEGACY_USING_PASS_2':<42} {str(self.uses_pass_2()).lower()}\n"
        txt += "\n"

        txt += "/* Externals */\n"
        txt += self.generate_wrappers_of_external_operations() + "\n"

        txt += self.to_comment_box("   Types", self.TextAlignment.Left) + "\n"

        if self._settings.compiler_directives.opening_pack_directive:
            txt += self._settings.compiler_directives.opening_pack_directive + "\n"

        for block in self._datamodel.children:
            txt += self.generate_block_type(block) + "\n"

        if self._settings.compiler_directives.closing_pack_directive:
            txt += self._settings.compiler_directives.closing_pack_directive + "\n"

        txt += "\n"
        txt += self.to_comment_box("   Default values", self.TextAlignment.Left) + "\n"
        txt += self.generate_default_objects(True) + "\n"

        txt += "\n"
        txt += self.to_comment_box("   Identity header", self.TextAlignment.Left) + "\n"
        txt += self.generate_identity_header(True) + "\n"

        if self._settings.crc_implementation == "table":
            txt += "\n"
            txt += self.to_comment_box("   CRC table", self.TextAlignment.Left) + "\n"
            txt += self.generate_crc_table(True) + "\n"

        return txt

    def generate_MEEM_GenConfig_c(self) -> str:
        txt = self.to_comment_box("   Dependencies", self.TextAlignment.Left) + "\n"
        txt += '#include "MEEM_GenConfig.h"\n'
        txt += '#include "MEEM_Internal.h"\n'
        txt += '#include "MEEM_GenInterface.h"\n'
        txt += "#include <assert.h>\n"
        # The external headers are already included by MEEM_GenConfig.h
        txt += "\n"
        txt += self.to_comment_box("   Validation routines", self.TextAlignment.Left) + "\n"
        txt += self.generate_config_validator_function() + "\n"
        txt += "\n"

        txt += self.to_comment_box("   Block configurations", self.TextAlignment.Left) + "\n"
        txt += self.generate_block_config_struct(False) + "\n"
        txt += "\n"

        txt += self.to_comment_box("   Default values", self.TextAlignment.Left) + "\n"
        txt += self.generate_default_objects(False) + "\n"

        txt += "\n"
        txt += self.to_comment_box("   Identity header", self.TextAlignment.Left) + "\n"
        txt += self.generate_identity_header(False) + "\n"

        if self._settings.crc_implementation == "table":
            txt += "\n"
            txt += self.to_comment_box("   CRC table", self.TextAlignment.Left) + "\n"
            txt += self.generate_crc_table(False) + "\n"

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

        if self._settings.code_gen_settings.cache_layout == "monolith":
            txt += self.to_comment_line("----- Block cache accessors -----", self.TextAlignment.Left) + "\n"
            txt += self.generate_cache_access_macros() + "\n"

        txt += self.to_comment_box("   Parameter access wrappers", self.TextAlignment.Left) + "\n"
        txt += self.to_comment_line("----- Getters -----", self.TextAlignment.Left) + "\n"
        for block in self._datamodel.children:
            for param in block.children:
                txt += self.generate_parameter_getter_function(block, param)
        txt += "\n"
        txt += self.to_comment_line("----- Setters -----", self.TextAlignment.Left) + "\n"
        for block in self._datamodel.children:
            for param in block.children:
                txt += self.generate_parameter_setter_function(block, param)

        return txt

    def generate_MEEM_GenInterface_c(self) -> str:
        txt = self.to_comment_box("   Dependencies", self.TextAlignment.Left) + "\n"
        txt += '#include "MEEM_GenInterface.h"\n'
        txt += '#include "MEEM_Linkage.h"\n'
        txt += "\n"

        txt += self.to_comment_box("   Public global variables", self.TextAlignment.Left) + "\n"
        txt += self.generate_parameter_caches(False) + "\n"
        return txt

    def generate_MEEM_Migration_h(self) -> str:
        txt = self.to_comment_box("   Dependencies", self.TextAlignment.Left) + "\n"
        txt += '#include "MEEM_GenConfig.h"\n'
        if self._legacy:
            txt += '#include "MEEM_Internal.h"\n'
        txt += "\n"

        txt += self.to_comment_box("   Macros", self.TextAlignment.Left) + "\n"
        txt += "/* Count of the legacy models given to the generator, from which MEEM_Init() migrates */\n"
        txt += f"#define MEEM_LEGACY_MODEL_COUNT    {len(self._legacy)}u\n"

        if self._legacy:
            txt += "\n"
            txt += self.to_comment_box("   Legacy configurations", self.TextAlignment.Left) + "\n"
            txt += "EXTERN_C const MEEM_legacyModel_t MEEM_legacy_models[MEEM_LEGACY_MODEL_COUNT];\n"
            txt += "\n"
            txt += self.to_comment_box("   Migration routines", self.TextAlignment.Left) + "\n"
            txt += "/*!\n"
            txt += " * \\brief  Converts the source of a block with MEEM_SOURCE_ROUTINE into its cache. The core restores the defaults beforehand.\n"
            txt += " * \\param  model - Index of the legacy model in MEEM_legacy_models\n"
            txt += " * \\param  block_id - Current block ID\n"
            txt += " * \\param  profile - Target profile of a MultiProfile block, else 0\n"
            txt += " * \\retval false if the source has no valid instance, or if the block has no routine for the model\n"
            txt += " */\n"
            txt += "EXTERN_C bool MEEM_MigrateBlock(uint8_t model, uint8_t block_id, uint8_t profile);\n"
        return txt

    def generate_MEEM_Migration_c(self) -> str:
        if not self._legacy:
            txt = self.to_comment_box("   Dependencies", self.TextAlignment.Left) + "\n"
            txt += '#include "MEEM_Migration.h"\n'
            txt += "\n"
            txt += "/* No legacy models: nothing to migrate */\n"
            return txt

        # The routines first, for what they include
        writer = RoutineWriter(self.generate_block_cache_access)
        routines = ""
        for routine in self._routines:
            definition, warnings = writer.write(routine)
            routines += self.generate_legacy_params_type(routine) + "\n" + definition + "\n"
            for warning in warnings:
                print(f"{Fore.YELLOW}Warning: block '{routine.target.name.strip()}', migrated from {routine.model_names}: {warning}.{Fore.RESET}")

        txt = self.to_comment_box("   Dependencies", self.TextAlignment.Left) + "\n"
        txt += '#include "MEEM_Migration.h"\n'
        if self._routines or self.uses_pass_2():
            txt += '#include "MEEM_GenInterface.h"\n'
        if self._routines:
            txt += "#include <assert.h>\n"
        if FLOAT64_TO_FLOAT32 in writer.helpers:
            txt += "#include <float.h>\n"
        if writer.uses_memcpy:
            txt += "#include <string.h>\n"
        txt += "\n"

        txt += self.to_comment_box("   Legacy configurations", self.TextAlignment.Left) + "\n"
        for k, configuration in enumerate(self._legacy):
            txt += self.generate_legacy_configuration(k, configuration) + "\n"

        models = []
        for k, configuration in enumerate(self._legacy):
            header = ", ".join(f"0x{b:02X}u" for b in configuration.identity_header)
            blocks = f"MEEM_legacy_blocks_{k}" if configuration.entries else "NULL"
            models.append(f"    {{ {{ {header} }}, MEEM_legacy_sources_{k}, {blocks} }}, /* {configuration.model.name} */")

        txt += "const MEEM_legacyModel_t MEEM_legacy_models[MEEM_LEGACY_MODEL_COUNT] = {\n" + "\n".join(models) + "\n};\n"
        txt += "\n"

        if writer.helpers:
            txt += self.to_comment_box("   Conversion helpers", self.TextAlignment.Left) + "\n"
            txt += helper_definitions(writer.helpers)

        if self._routines:
            txt += self.to_comment_box("   Migration routines", self.TextAlignment.Left) + "\n"
            txt += routines

        txt += self.to_comment_box("   Dispatcher", self.TextAlignment.Left) + "\n"
        txt += self.generate_dispatcher()
        return txt

    def generate_legacy_params_type(self, routine: Routine) -> str:
        """The typedef of a routine's old parameters, packed like the caches"""
        txt = f"/* {routine.target.name.strip()} parameters with data identity 0x{routine.data_identity:04X} */\n"

        if self._settings.compiler_directives.opening_pack_directive:
            txt += self._settings.compiler_directives.opening_pack_directive + "\n"
        txt += self.generate_params_struct(routine.source.children, routine.params_type, False)
        if self._settings.compiler_directives.closing_pack_directive:
            txt += self._settings.compiler_directives.closing_pack_directive + "\n"
        return txt

    def generate_dispatcher(self) -> str:
        """MEEM_MigrateBlock(): a switch over the model, then over the block, whose cases call the routines with their legacy entries"""
        txt = "bool MEEM_MigrateBlock(uint8_t model, uint8_t block_id, uint8_t profile)\n"
        txt += "{\n"

        # Per legacy model: the case label of each block with a routine, and the call
        cases_by_model: Dict[int, List[Tuple[str, str]]] = {k: [] for k in range(len(self._legacy))}
        for routine in self._routines:
            for case in routine.cases:
                cases_by_model[case.model].append(
                    (f"case MEEM_BLOCK_{routine.target.name.strip()}_ID:", f"return {routine.name}(&MEEM_legacy_blocks_{case.model}[{case.entry}], profile);")
                )

        def block_switch(cases: List[Tuple[str, str]], indent: str) -> str:
            width = max(len(label) for label, _ in cases + [("default:", "")])
            txt = f"{indent}switch (block_id)\n"
            txt += f"{indent}{{\n"
            txt += "".join(f"{indent}    {label:<{width}} {call}\n" for label, call in cases)
            txt += f"{indent}    {'default:':<{width}} break;\n"
            txt += f"{indent}}}\n"
            return txt

        if not self._routines:
            txt += "    (void) model;\n"
            txt += "    (void) block_id;\n"
            txt += "    (void) profile;\n"
            txt += "    return false; /* No block has a routine */\n"
        elif len(self._legacy) == 1:
            txt += "    (void) model; /* Only one legacy model */\n"
            txt += "\n"
            txt += block_switch(cases_by_model[0], "    ")
            txt += "    return false;\n"
        else:
            txt += "    switch (model)\n"
            txt += "    {\n"
            for k, cases in cases_by_model.items():
                if cases:
                    txt += f"        case {k}u:\n"
                    txt += block_switch(cases, "            ")
                    txt += "            break;\n"
                    txt += "\n"
            txt += "        default:\n"
            txt += "            break;\n"
            txt += "    }\n"
            txt += "    return false;\n"
        txt += "}\n"
        return txt

    def generate_legacy_configuration(self, k: int, configuration: LegacyConfiguration) -> str:
        """The legacy block array and the source table of legacy model k"""
        sources = configuration.sources
        txt = f"/* Legacy model {k}: {configuration.model.name}, EEPROM layout identity 0x{configuration.layout_identity:08X}. "
        txt += f"{sources.count(SOURCE_UNCHANGED)} unchanged, {len(configuration.entries)} read, {sources.count(SOURCE_NONE)} without a source */\n"

        if configuration.entries:
            rows = [
                (f"{e.source.offset_in_eeprom}u,", f"{e.source.data_size}u,", f"{e.source.instance_count}u,", str(e.source.management_type))
                for e in configuration.entries
            ]
            widths = [max(len(row[column]) for row in rows) for column in range(3)] + [max(len(row[3]) for row in rows)]

            txt += f"static const MEEM_legacyBlockConfig_t MEEM_legacy_blocks_{k}[] = {{\n"
            txt += "    /* offset, data_size, instance_count, management_type */\n"
            for i, (entry, row) in enumerate(zip(configuration.entries, rows)):
                fields = " ".join(f"{field:>{width}}" if column < 3 else f"{field:<{width}}" for column, (field, width) in enumerate(zip(row, widths)))
                txt += f"    {{ {fields} }}, /* [{i}] {entry.target.name}: {entry.describe()} */\n"
            txt += "};\n"
        else:
            txt += "/* No legacy block is read: there is no legacy block array */\n"

        names = {SOURCE_ROUTINE: "MEEM_SOURCE_ROUTINE", SOURCE_NONE: "MEEM_SOURCE_NONE", SOURCE_UNCHANGED: "MEEM_SOURCE_UNCHANGED"}
        txt += f"static const uint8_t MEEM_legacy_sources_{k}[{'' if self.uses_pass_2() else 'MEEM_BLOCK_COUNT'}] = {{\n"
        for block, source in zip(self._datamodel.children, sources):
            value = names.get(source, f"{source}u")
            txt += f"    {value + ',':<22} /* {block.name} */\n"

        if self.uses_pass_2():
            # The order of pass 2 follows the entries, a count, then each block with its direction
            count = len(configuration.pass_2)
            txt += f"    /* Pass 2: {count if count else 'no'} block{'' if count == 1 else 's'} */\n"
            txt += f"    {count}u,\n"
            for step in configuration.pass_2:
                txt += f"    MEEM_BLOCK_{step.target.name.strip()}_ID, {'MEEM_PASS2_DESCENDING' if step.descending else 'MEEM_PASS2_ASCENDING'},\n"
        txt += "};\n"
        return txt

    def generate_block_type(self, block: Block) -> str:
        txt = f"/* {self.sanitize_description(block.description)} */\n" if block.description else ""
        txt += self.generate_params_struct(block.children, f"MEEM_params_{block.name}_t", block.management_type == Block.ManagementTypes.WearLeveling)
        return txt

    def generate_params_struct(self, params: List[Parameter], type_name: str, with_sequence_counter: bool) -> str:
        """A packed struct of parameters: a cache type, or the old parameters of a migration routine"""
        attr = (self._settings.compiler_directives.pack_attribute + " ") if self._settings.compiler_directives.pack_attribute else ""
        txt = f"typedef struct {attr}{{\n"

        if with_sequence_counter:
            txt += f"    {str(Parameter.DataTypes.uint8)}  do_not_use_me;\n"

        for param in params:
            array_suffix = f"[{param.multiplicity}]" if param.multiplicity > 1 else ""

            if len(param.children) > 0:
                txt += "    union {\n"
                txt += f"        {str(param.data_type)} all;\n"
                txt += "        struct {\n"
                for bitfield in param.children:
                    description = f" /* {self.sanitize_description(bitfield.description)} */" if bitfield.description else ""
                    txt += f"            {str(param.data_type)}  {bitfield.name.strip()}: {bitfield.size_in_bits};{description}\n"

                txt += "        };\n"
                txt += f"    }} {param.name.strip()}{array_suffix};\n"
            else:
                description = f" /* {self.sanitize_description(param.description)} */" if param.description else ""
                txt += f"    {str(param.data_type)}  {param.name.strip()}{array_suffix};{description}\n"

        txt += f"}} {type_name};\n"
        return txt

    def generate_parameter_getter_function(self, block: Block, param: Parameter) -> str:
        cache = self.generate_block_cache_access(block)
        is_array = param.multiplicity > 1
        array_suffix = "[index]" if is_array else ""
        array_index_type = "uint16_t" if param.multiplicity > 255 else "uint8_t"
        use_macros = self._settings.code_gen_settings.accessor_style == "macros"
        txt = ""

        if len(param.children) == 0:
            targets = [(f"MEEM_Get_{block.name}_{param.name}", f"{cache}.{param.name}{array_suffix}")]
        else:
            targets = [(f"MEEM_Get_{block.name}_{param.name}_{bf.name}", f"{cache}.{param.name}{array_suffix}.{bf.name}") for bf in param.children]

        for func_name, access in targets:
            if use_macros:
                macro_args = "index" if is_array else ""
                txt += f"#define {func_name}({macro_args})    {access}\n"
            else:
                func_args = f"{array_index_type}  index" if is_array else ""
                txt += f"static inline {str(param.data_type)} {func_name}({func_args}) {{\n"
                txt += f"    return {access};\n"
                txt += f"}}\n"
        return txt

    def generate_parameter_setter_function(self, block: Block, param: Parameter) -> str:
        cache = self.generate_block_cache_access(block)
        is_array = param.multiplicity > 1
        array_suffix = "[index]" if is_array else ""
        array_index_type = "uint16_t" if param.multiplicity > 255 else "uint8_t"
        use_macros = self._settings.code_gen_settings.accessor_style == "macros"
        txt = ""

        if len(param.children) == 0:
            targets = [(f"MEEM_Set_{block.name}_{param.name}", f"{cache}.{param.name}{array_suffix}")]
        else:
            targets = [(f"MEEM_Set_{block.name}_{param.name}_{bf.name}", f"{cache}.{param.name}{array_suffix}.{bf.name}") for bf in param.children]

        for func_name, access in targets:
            if use_macros:
                macro_args = "value, index" if is_array else "value"
                txt += f"#define {func_name}({macro_args})    {access} = (value)\n"
            else:
                func_args = f"{str(param.data_type)} value" + (f", {array_index_type}  index" if is_array else "")
                txt += f"static inline void {func_name}({func_args}) {{\n"
                txt += f"    {access} = value;\n"
                txt += f"}}\n"
        return txt

    def generate_wrappers_of_external_operations(self) -> str:
        txt = f"#define MEEM_EnterCriticalSection()    {'' if self._settings.enter_critical_section_operation is None else self._settings.enter_critical_section_operation}\n"
        txt += f"#define MEEM_ExitCriticalSection()     {'' if self._settings.exit_critical_section_operation is None else self._settings.exit_critical_section_operation}\n"
        return txt

    def generate_parameter_caches(self, for_prototype: bool) -> str:
        if self._settings.code_gen_settings.cache_layout == "monolith":
            return self.generate_aggregate_parameter_cache(for_prototype)

        txt = ""
        ext = "EXTERN_C " if for_prototype else ""

        for block in self._datamodel.children:
            if (block.name in self._settings.compiler_directives.block_placement_directives) and for_prototype:
                directive = self._settings.compiler_directives.block_placement_directives[block.name].directive_for_cache
                if directive:
                    txt += directive + "\n"

            txt += f"{ext}{self.generate_parameter_cache(block, for_prototype)};\n"
        return txt

    def generate_aggregate_parameter_cache(self, for_prototype: bool) -> str:
        if not for_prototype:
            return "MEEM_cache_t  MEEM_cache;\n"

        attr = (self._settings.compiler_directives.pack_attribute + " ") if self._settings.compiler_directives.pack_attribute else ""
        txt = ""
        if self._settings.compiler_directives.opening_pack_directive:
            txt += self._settings.compiler_directives.opening_pack_directive + "\n"
        txt += f"typedef struct {attr}{{\n"
        for block in self._datamodel.children:
            txt += f"    MEEM_params_{block.name}_t  {block.name};\n"
        txt += "} MEEM_cache_t;\n"
        if self._settings.compiler_directives.closing_pack_directive:
            txt += self._settings.compiler_directives.closing_pack_directive + "\n"
        txt += "EXTERN_C MEEM_cache_t  MEEM_cache;\n"
        return txt

    def generate_parameter_cache(self, block: Block, for_prototype: bool) -> str:
        attribute = ""
        if (block.name in self._settings.compiler_directives.block_placement_directives) and for_prototype:
            attr = self._settings.compiler_directives.block_placement_directives[block.name].attribute_for_cache
            if attr:
                attribute = attr + " "

        return f"MEEM_params_{block.name}_t {attribute}{self.generate_block_cache_object_name(block)}"

    def defaults_as_bytes(self, block: Block) -> Optional[bytes]:
        """The bytes that the block's defaults are generated as: its default pattern or, if a default is NaN, all of its data,
        since no NaN constant works on every compiler. None if they are generated as a struct initializer."""
        if block.default_pattern is not None:
            return block.default_pattern

        if any(isinstance(value, float) and math.isnan(value) for param in block.children for value in param.default_value):
            sequence_counter = bytes(1) if block.management_type == Block.ManagementTypes.WearLeveling else b""
            return sequence_counter + bytes(extract_defaults(block, self._settings))
        return None

    def generate_default_objects(self, for_prototype: bool) -> str:
        txt = ""

        for block in self._datamodel.children:
            defaults = self.defaults_as_bytes(block)

            if defaults is None:
                txt += (
                    f"EXTERN_C const MEEM_params_{block.name}_t  MEEM_defaults_{block.name};"
                    if for_prototype
                    else self.generate_definition_of_defaults_as_struct(block)
                )
            else:
                txt += (
                    f"EXTERN_C const uint8_t  MEEM_defaults_{block.name}[{len(defaults)}];"
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
            txt += f"    {self.init_field('do_not_use_me')}0,\n"

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

        defaults = self.defaults_as_bytes(block)
        assert defaults != None
        txt += f'const uint8_t {placement_attribute}MEEM_defaults_{block.name}[{len(defaults)}] = {{ {", ".join(f"0x{b:02X}" for b in defaults)} }};'
        return txt

    def generate_definition_of_defaults_for_parameter(self, param: Parameter) -> str:
        txt = f"    {self.init_field(param.name)}"

        if param.multiplicity > 1:
            txt += "{" + "\n"

            if len(param.children) > 0:
                for i in range(0, param.multiplicity):
                    txt += f"        {{ {self.init_field('all')}{self.to_str(param.default_value[i], param.data_type)} }},\n"
                txt = self.remove_last_occurrence_of(",", txt)
            else:
                txt += "        " + ", ".join([self.to_str(d, param.data_type) for d in param.default_value]) + "\n"

            txt += "    },\n"
        else:
            if len(param.children) > 0:
                txt += f"{{ {self.init_field('all')}{self.to_str(param.default_value[0], param.data_type)} }},\n"
            else:
                txt += self.to_str(param.default_value[0], param.data_type) + ",\n"

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
        if self._settings.crc_implementation == "hardware":
            txt += self.generate_crc_unit_self_test()
        txt += "}"
        return txt

    def generate_crc_unit_self_test(self) -> str:
        """Part of MEEM_ValidateConfiguration() with the hardware CRC: runs the check input through the CRC unit. Debug builds only."""
        txt = "\n"
        txt += "#ifndef NDEBUG\n"
        txt += "    /* Self-test: the CRC unit must calculate CRC-16/IBM-3740, poly 0x1021, init 0xFFFF, no reflection, xorout 0 */\n"
        txt += "    {\n"
        txt += "        uint16_t       crc = 0u;\n"
        txt += "        CRCIF_status_t status;\n"
        txt += "\n"
        txt += "        do\n"
        txt += "        {\n"
        txt += '            status = CRCIF_BeginCalculation((const uint8_t*) "123456789", 9u);\n'
        txt += "        } while (CRCIF_BUSY == status);\n"
        txt += "\n"
        txt += "        if (CRCIF_OK == status)\n"
        txt += "        {\n"
        txt += "            do\n"
        txt += "            {\n"
        txt += "                status = CRCIF_GetResult(&crc);\n"
        txt += "            } while (CRCIF_BUSY == status);\n"
        txt += "        }\n"
        txt += "\n"
        txt += "        assert(CRCIF_NOT_INITIALIZED != status); /* The application initializes the CRC unit before MEEM_Init() */\n"
        txt += "        assert((CRCIF_OK == status) && (0x29B1u == crc)); /* The CRC unit or its driver is misconfigured */\n"
        txt += "    }\n"
        txt += "#endif\n"
        return txt

    def generate_block_config_struct(self, for_prototype: bool) -> str:
        if for_prototype:
            return "EXTERN_C const MEEM_blockConfig_t   MEEM_block_config[ MEEM_BLOCK_COUNT ];"

        configs = []
        for block in self._datamodel.children:
            cast = "(const uint8_t*)&" if self.defaults_as_bytes(block) is None else ""

            txt = f"    /* Block '{block.name}' */\n"
            txt += f"    {{\n"
            txt += f"        {self.init_field('cache')}(uint8_t*)&{self.generate_block_cache_access(block)},\n"
            txt += f"        {self.init_field('defaults')}{cast}MEEM_defaults_{block.name},\n"
            txt += f"        {self.init_field('offset_in_eeprom')}{self.to_str(block.offset_in_eeprom)},\n"  # type: ignore
            txt += f"        {self.init_field('data_size')}{block.data_size},\n"
            txt += f"        {self.init_field('default_pattern_length')}{0 if block.default_pattern is None else len(block.default_pattern)},\n"
            txt += f"        {self.init_field('instance_count')}{block.instance_count},\n"
            txt += f"        {self.init_field('management_type')}{str(block.management_type)},\n"
            txt += f"        {self.init_field('data_recovery_strategy')}{str(block.data_recovery_strategy)}\n"
            txt += f"    }}"
            configs.append(txt)

        return "const MEEM_blockConfig_t   MEEM_block_config[ MEEM_BLOCK_COUNT ] = {\n" + ",\n".join(configs) + "\n};"

    def generate_block_cache_object_name(self, block: Block) -> str:
        return f"MEEM_cache_{block.name}"

    def generate_block_cache_access(self, block: Block) -> str:
        """Returns the lvalue expression used to access a block's cache object."""
        if self._settings.code_gen_settings.cache_layout == "monolith":
            return f"MEEM_cache.{block.name}"
        return f"MEEM_cache_{block.name}"

    def generate_cache_access_macros(self) -> str:
        """Generates uniform macros to refer to a block's cache object, regardless of the configured cache objects type."""
        txt = ""
        for block in self._datamodel.children:
            txt += f"#define MEEM_cache_{block.name}    ({self.generate_block_cache_access(block)})\n"
        return txt

    def init_field(self, name: str) -> str:
        """Returns the struct field initializer prefix according to the configured language standard.
        'c99' emits a designated initializer, 'c90' emits a positional initializer with a documentation comment."""
        if self._settings.code_gen_settings.language_standard == "c99":
            return f".{name} = "
        return f"/* .{name} = */ "

    def sanitize_description(self, comment: str) -> str:
        return comment.replace("//", "").replace("/*", "").replace("*/", "").replace("\n", " ").replace("\r", " ").strip()

    def generate_crc_table(self, for_prototype: bool) -> str:
        """The lookup table of the built-in 'table' CRC routine, with its placement directive or attribute from the platform settings."""
        directives = self._settings.compiler_directives
        attribute = (directives.crc_table_placement_attribute + " ") if directives.crc_table_placement_attribute else ""

        if for_prototype:
            return f"EXTERN_C const uint16_t {attribute}MEEM_crc_table[256];"

        txt = "/* CRC-16/IBM-3740 of each byte value, for the instance checksum */\n"
        if directives.crc_table_placement_directive:
            txt += directives.crc_table_placement_directive + "\n"

        rows = [CRC16_TABLE[i : i + 8] for i in range(0, len(CRC16_TABLE), 8)]
        txt += f"const uint16_t {attribute}MEEM_crc_table[256] = {{\n"
        txt += ",\n".join("    " + ", ".join(f"0x{value:04X}" for value in row) for row in rows) + "\n"
        txt += "};"
        return txt

    def generate_identity_header(self, for_prototype: bool) -> str:
        """The identity header of the current model: the first bytes of the EEPROM, which the core compares and writes"""
        if for_prototype:
            return f"EXTERN_C const uint8_t MEEM_identity_header[{IDENTITY_HEADER_SIZE}];"

        header = ", ".join(f"0x{b:02X}" for b in identity_header(self._layout_identity))
        txt = f"/* The CRC-16 over the EEPROM layout identity 0x{self._layout_identity:08X}, then the identity, both little-endian */\n"
        txt += f"const uint8_t MEEM_identity_header[{IDENTITY_HEADER_SIZE}] = {{ {header} }};"
        return txt

    def get_crc_implementation_macro(self) -> str:
        if self._settings.crc_implementation == "table":
            return "MEEM_CRC_TABLE"
        if self._settings.crc_implementation == "bitwise":
            return "MEEM_CRC_BITWISE"
        if self._settings.crc_implementation == "hardware":
            return "MEEM_CRC_HARDWARE"
        raise Exception(f"Not implemented CRC implementation: {self._settings.crc_implementation}")

    def uses_pass_2(self) -> bool:
        """Whether a legacy model has a block in pass 2"""
        return any(configuration.pass_2 for configuration in self._legacy)

    def get_legacy_sources(self) -> List[Block]:
        """The legacy blocks that the core reads, the entries of every legacy block array. A removed or renamed legacy block isn't one."""
        return [entry.source for configuration in self._legacy for entry in configuration.entries]

    def get_max_wl_instance_count(self) -> int:
        """The sequence counters of the current 'wear-leveling' blocks, and of those among the legacy sources, which the reader searches"""
        blocks = self._datamodel.children + self.get_legacy_sources()
        instance_counts = [b.instance_count for b in blocks if b.management_type == Block.ManagementTypes.WearLeveling]

        if instance_counts:
            return max(instance_counts)
        return 0

    def calculate_workbuffer_size(self) -> int:
        """The work buffer holds the largest instance that is read, of a current block or of a legacy source,
        and the identity header, which is read and written through it"""
        blocks = self._datamodel.children + self.get_legacy_sources()
        return max(IDENTITY_HEADER_SIZE, CHECKSUM_SIZE + max([b.data_size for b in blocks]))

    def to_str(self, value: int | float, data_type: Optional[Parameter.DataTypes] = None) -> str:
        if isinstance(value, float):
            # Only 'float' literals need the suffix. Otherwise a 'double' would get initialized with a single-precision value.
            return str(value) + ("f" if data_type == Parameter.DataTypes.float32 else "")

        return str(value) if (value < 10) else f"0x{value:X}"

    def remove_last_occurrence_of(self, char: str, string: str) -> str:
        """Remove last occurrence of a char in a string"""
        last_index = string.rfind(char)
        return string if last_index < 0 else string[:last_index] + string[last_index + 1 :]
