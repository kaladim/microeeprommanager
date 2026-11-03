"""Migration routines: the generated code that converts a legacy source with another parameter list into the cache.

There is one routine per current block and source data identity, shared by every legacy model whose source of the block has that
data identity. It gets the source's parameters from MEEM_ReadLegacyInstance() through a packed typedef of the old parameters,
and converts them into the cache, matched by name:
- integer -> integer: clamped to the target range;
- float -> integer: rounded half away from zero, then clamped. NaN gives 0;
- integer -> float, float64 -> float32: the nearest value, clamped to +-max. NaN stays NaN, +-inf saturates;
- the first min(old, new) elements of an array;
- bitfields by name, clamped to their width. A parameter that gains or loses all its bitfields converts its whole value.

Parameters that a routine doesn't set keep the defaults that the core restores before calling it. Unchanged parameters that are
contiguous on both sides are copied together. The helpers of the 64-bit and floating-point conversions are generated only when a
routine needs them.
"""

import os
import sys
from dataclasses import dataclass, field
from typing import Callable, Dict, List, Optional, Sequence, Set, Tuple

sys.path.append(os.path.dirname(os.path.dirname(__file__)))
from common.py.data_model import Bitfield, Block, DataModel, Parameter
from common.py.identity import data_identity
from legacy import LegacyConfiguration

DataTypes = Parameter.DataTypes

C_TYPES = {
    DataTypes.uint8: "uint8_t",
    DataTypes.int8: "int8_t",
    DataTypes.uint16: "uint16_t",
    DataTypes.int16: "int16_t",
    DataTypes.uint32: "uint32_t",
    DataTypes.int32: "int32_t",
    DataTypes.uint64: "uint64_t",
    DataTypes.int64: "int64_t",
    DataTypes.float32: "float",
    DataTypes.float64: "double",
}

FLOAT_TYPES = (DataTypes.float32, DataTypes.float64)
SIGNED_TYPES = (DataTypes.int8, DataTypes.int16, DataTypes.int32, DataTypes.int64)
WIDE_TYPES = (DataTypes.uint64, DataTypes.int64)

MANTISSA_BITS = {DataTypes.float32: 24, DataTypes.float64: 53}
"""The integers up to 2^bits are exactly representable"""

COPY_BY_ASSIGNMENT_MAX_SIZE = 2
"""Unchanged parameters are copied with one memcpy(), unless they're a single value of up to this size, which an assignment copies.
On the PIC18, the call takes about 20 B of code, a copy through the source pointer 11-13 B per byte."""

CLAMP_INT64 = ("clamp", DataTypes.int64)
CLAMP_UINT64 = ("clamp", DataTypes.uint64)
FLOAT64_TO_FLOAT32 = ("float", DataTypes.float64, DataTypes.float32)


def type_name(data_type: DataTypes) -> str:
    """The name of a data type in function names, e.g. 'Uint8'"""
    return DataTypes(data_type).name.capitalize()


def limit_macros(data_type: DataTypes) -> Tuple[str, str]:
    """The <stdint.h> macros of an integer type's range"""
    name = DataTypes(data_type).name.upper()
    return ("0", f"{name}_MAX") if data_type not in SIGNED_TYPES else (f"{name}_MIN", f"{name}_MAX")


def integer_range(data_type: DataTypes, bits: Optional[int] = None) -> Tuple[int, int]:
    """The range of an integer type, or of a bitfield of the given width in a parameter of that type"""
    if bits is None:
        bits = data_type.size * 8
    if data_type in SIGNED_TYPES:
        return -(1 << (bits - 1)), (1 << (bits - 1)) - 1
    return 0, (1 << bits) - 1


def numeric_literal(value: int) -> str:
    """An integer constant with the suffix that gives it a wide enough type on any target, also where int has 16 bits"""
    if -32767 <= value <= 32767:
        return str(value)
    if -(2**31) < value < 2**31:
        return f"{value}L"
    if 0 <= value < 2**32:
        return f"{value}UL"
    if value == -(2**63):
        return "INT64_MIN"
    if -(2**63) < value < 2**63:
        return f"{value}LL"
    return f"{value}ULL"


def bitfields_of(param: Parameter) -> List[Tuple[str, int]]:
    return [(bitfield.name.strip(), bitfield.size_in_bits) for bitfield in param.children]


def are_elements_identical(old: Parameter, new: Parameter) -> bool:
    """The elements hold the same bytes in the same way: the same type and bitfields"""
    return (old.data_type == new.data_type) and (bitfields_of(old) == bitfields_of(new))


def are_identical(old: Parameter, new: Parameter) -> bool:
    return are_elements_identical(old, new) and (old.multiplicity == new.multiplicity)


@dataclass(frozen=True)
class Value:
    """What a routine reads or writes: a parameter, one of its elements, or one of their bitfields, as a C expression"""

    expression: str
    data_type: DataTypes
    """Its type, the parameter's type for a bitfield"""

    bits: Optional[int] = None
    """A bitfield's width, None for a whole value"""

    @property
    def is_float(self) -> bool:
        return self.data_type in FLOAT_TYPES

    @property
    def range(self) -> Tuple[int, int]:
        return integer_range(self.data_type, self.bits)

    def describe(self) -> str:
        """E.g. 'uint16', or 'uint8:3' for a bitfield"""
        name = DataTypes(self.data_type).name
        return name if self.bits is None else f"{name}:{self.bits}"

    def literal(self, bound: int) -> str:
        """A bound of this value's range: the <stdint.h> macro of a whole value's type, a number for a bitfield"""
        if self.bits is not None:
            return numeric_literal(bound)
        minimum, maximum = limit_macros(self.data_type)
        return minimum if bound == self.range[0] else maximum


@dataclass
class RoutineCase:
    """A dispatcher case of a routine: a legacy model whose source of the block has the routine's data identity"""

    model: int
    """Index of the legacy model in MEEM_legacy_models"""

    entry: int
    """Index of the source in the legacy model's block array"""

    model_name: str


@dataclass
class Routine:
    """The routine of a current block and a source data identity"""

    target: Block
    """The current block"""

    source: Block
    """The legacy block, the first one with this data identity"""

    cases: List[RoutineCase] = field(default_factory=list)

    @property
    def data_identity(self) -> int:
        return data_identity(self.source)

    @property
    def name(self) -> str:
        return f"MEEM_Migrate_{self.target.name.strip()}_{self.data_identity:04X}"

    @property
    def params_type(self) -> str:
        """The typedef of the old parameters"""
        return f"MEEM_params_{self.target.name.strip()}_{self.data_identity:04X}_t"

    @property
    def model_names(self) -> str:
        """The legacy models that the routine serves, for comments and messages. One data model may be given with several platform settings."""
        return ", ".join(f"'{name}'" for name in dict.fromkeys(case.model_name for case in self.cases))


def collect_routines(current: DataModel, configurations: Sequence[LegacyConfiguration]) -> List[Routine]:
    """The routines of all legacy models, one per current block and source data identity, in the order of the current blocks"""
    routines: Dict[Tuple[str, int], Routine] = {}

    for k, configuration in enumerate(configurations):
        for index, entry in enumerate(configuration.entries):
            if not entry.copied:
                key = (entry.target.name.strip(), data_identity(entry.source))
                routines.setdefault(key, Routine(target=entry.target, source=entry.source)).cases.append(RoutineCase(k, index, configuration.model.name))

    block_order = {block.name.strip(): i for i, block in enumerate(current.children)}
    return sorted(routines.values(), key=lambda routine: (block_order[routine.target.name.strip()], routine.cases[0].model))


class RoutineWriter:
    """Writes the routines, and collects what they need: the conversion helpers, memcpy(), and the warnings"""

    def __init__(self, cache_access: Callable[[Block], str]):
        self.cache_access = cache_access
        """The lvalue expression of a block's cache"""

        self.helpers: Set[tuple] = set()
        self.uses_memcpy = False

    def write(self, routine: Routine) -> Tuple[str, List[str]]:
        """The routine's definition, and the warnings about what it drops or narrows"""
        old_params = routine.source.children
        new_params = routine.target.children
        old_index = {param.name.strip(): i for i, param in enumerate(old_params)}
        new_names = {param.name.strip() for param in new_params}
        cache = self.cache_access(routine.target)

        self.warnings: List[str] = [f"parameter '{p.name.strip()}' is dropped" for p in old_params if p.name.strip() not in new_names]
        self.loop_limit = 0
        statements: List[str] = []
        run: List[Tuple[int, int]] = []  # Unchanged parameters, contiguous on both sides: (old index, new index)

        for new_i, new in enumerate(new_params):
            old_i = old_index.get(new.name.strip())

            if (old_i is not None) and are_identical(old_params[old_i], new):
                if not (run and run[-1] == (old_i - 1, new_i - 1)):
                    statements += self.copy_run(run, old_params, new_params, cache)
                    run = []
                run.append((old_i, new_i))
                continue

            statements += self.copy_run(run, old_params, new_params, cache)
            run = []
            if old_i is not None:
                statements += self.convert_parameter(old_params[old_i], new, cache)
        statements += self.copy_run(run, old_params, new_params, cache)

        dropped = [p.name.strip() for p in old_params if p.name.strip() not in new_names]
        defaults = [p.name.strip() for p in new_params if p.name.strip() not in old_index]
        notes = ([", ".join(dropped) + ": dropped"] if dropped else []) + ([", ".join(defaults) + ": defaults"] if defaults else [])
        loop_index_type = "uint16_t" if self.loop_limit > 255 else "uint8_t"

        txt = f"/* {routine.target.name.strip()} from the parameters with data identity 0x{routine.data_identity:04X}, of {routine.model_names} */\n"
        txt += f"static bool {routine.name}(const MEEM_legacyBlockConfig_t* source, uint8_t profile)\n"
        txt += "{\n"
        txt += f"    const {routine.params_type}* src = (const {routine.params_type}*) MEEM_ReadLegacyInstance(source, profile);\n"
        if self.loop_limit > 0:
            txt += f"    {loop_index_type}  i;\n"
        txt += "\n"
        txt += "    /* If this assertion fails, you probably missed to specify a struct pack directive/attribute in the platform settings. */\n"
        txt += f"    assert(sizeof({routine.params_type}) == {sum(param.size for param in old_params)}u);\n"
        txt += "\n"
        txt += "    if (NULL == src)\n"
        txt += "    {\n"
        txt += "        return false;\n"
        txt += "    }\n"
        txt += "".join(f"    {statement}\n" for statement in statements)
        txt += "    return true;" + (" /* " + ". ".join(notes) + " */" if notes else "") + "\n"
        txt += "}\n"
        return txt, self.warnings

    def copy_run(self, run: List[Tuple[int, int]], old_params: List[Parameter], new_params: List[Parameter], cache: str) -> List[str]:
        """Copies unchanged parameters that are contiguous on both sides"""
        if not run:
            return []

        first_old, first_new = old_params[run[0][0]], new_params[run[0][1]]
        size = sum(old_params[old_i].size for old_i, _ in run)

        if len(run) == 1 and first_new.multiplicity == 1 and size <= COPY_BY_ASSIGNMENT_MAX_SIZE:
            member = ".all" if first_new.children else ""
            return [f"{cache}.{first_new.name.strip()}{member} = src->{first_old.name.strip()}{member};"]

        self.uses_memcpy = True
        names = ", ".join(new_params[new_i].name.strip() for _, new_i in run)
        return [f"(void) memcpy(&{cache}.{first_new.name.strip()}, &src->{first_old.name.strip()}, {size}u); /* {names} */"]

    def convert_parameter(self, old: Parameter, new: Parameter, cache: str) -> List[str]:
        """Converts the first min(old, new) elements of a parameter"""
        name = new.name.strip()
        count = min(old.multiplicity, new.multiplicity)

        if old.multiplicity == new.multiplicity + 1:
            self.warnings.append(f"element {new.multiplicity} of parameter '{name}' is dropped")
        elif old.multiplicity > new.multiplicity:
            self.warnings.append(f"elements {new.multiplicity} to {old.multiplicity - 1} of parameter '{name}' are dropped")

        if old.children and new.children:
            new_bitfields = {bitfield.name.strip() for bitfield in new.children}
            self.warnings += [f"bitfield '{name}.{b.name.strip()}' is dropped" for b in old.children if b.name.strip() not in new_bitfields]

        def element(base: str, param: Parameter, index: str) -> str:
            return f"{base}{param.name.strip()}[{index}]" if param.multiplicity > 1 else f"{base}{param.name.strip()}"

        if are_elements_identical(old, new):
            size = count * old.data_type.size
            if count == 1 and size <= COPY_BY_ASSIGNMENT_MAX_SIZE:
                member = ".all" if new.children else ""
                return [f"{element(cache + '.', new, '0')}{member} = {element('src->', old, '0')}{member};"]
            self.uses_memcpy = True
            return [f"(void) memcpy(&{cache}.{name}, &src->{old.name.strip()}, {size}u); /* {name}: the first {count} elements */"]

        index = "i" if count > 1 else "0"
        body = self.convert_element(old, new, element("src->", old, index), element(cache + ".", new, index), name)

        if count == 1 or not body:
            return body

        self.loop_limit = max(self.loop_limit, count)
        return [f"for (i = 0u; i < {count}u; i++)", "{"] + [f"    {statement}" for statement in body] + ["}"]

    def convert_element(self, old: Parameter, new: Parameter, source: str, target: str, name: str) -> List[str]:
        """Converts an element: its bitfields by name if both sides have bitfields, else its whole value"""
        if old.children and new.children:
            old_bitfields: Dict[str, Bitfield] = {bitfield.name.strip(): bitfield for bitfield in old.children}
            statements = []

            for new_bitfield in new.children:
                old_bitfield = old_bitfields.get(new_bitfield.name.strip())
                if old_bitfield is not None:
                    statements.append(
                        self.assignment(
                            Value(f"{source}.{old_bitfield.name.strip()}", old.data_type, old_bitfield.size_in_bits),
                            Value(f"{target}.{new_bitfield.name.strip()}", new.data_type, new_bitfield.size_in_bits),
                            f"bitfield '{name}.{new_bitfield.name.strip()}'",
                        )
                    )
            return statements

        return [
            self.assignment(
                Value(source + (".all" if old.children else ""), old.data_type),
                Value(target + (".all" if new.children else ""), new.data_type),
                f"parameter '{name}'",
            )
        ]

    def assignment(self, source: Value, target: Value, what: str) -> str:
        expression, effect = self.convert(source, target)
        comment = ""

        if source.describe() != target.describe():
            comment = f" /* {source.describe()} -> {target.describe()}" + (f", {effect}" if effect else "") + " */"
        if effect:
            self.warnings.append(f"{what} narrows from {source.describe()} to {target.describe()}: its value is {effect}")
        return f"{target.expression} = {expression};{comment}"

    def convert(self, source: Value, target: Value) -> Tuple[str, Optional[str]]:
        """The expression that converts the source value into the target's type, and what it does to values that don't fit"""
        target_type = C_TYPES[target.data_type]

        if source.is_float and target.is_float:
            if source.data_type == DataTypes.float64 and target.data_type == DataTypes.float32:
                self.helpers.add(FLOAT64_TO_FLOAT32)
                return f"MEEM_ConvertFloat64ToFloat32({source.expression})", "rounded and clamped"
            return source.expression, None

        if source.is_float:
            self.helpers.add(("float", source.data_type, target.data_type))
            return f"MEEM_Convert{type_name(source.data_type)}To{type_name(target.data_type)}({source.expression})", "rounded and clamped"

        if target.is_float:
            exact = max(abs(bound) for bound in source.range) <= (1 << MANTISSA_BITS[target.data_type])
            return f"({target_type}) {source.expression}", None if exact else "rounded"

        return self.convert_integer(source, target)

    def convert_integer(self, source: Value, target: Value) -> Tuple[str, Optional[str]]:
        """Integer -> integer: clamped to the target range. A source of 64 bits is clamped by a helper."""
        (source_min, source_max), (target_min, target_max) = source.range, target.range
        clamp_min, clamp_max = source_min < target_min, source_max > target_max
        target_type = C_TYPES[target.data_type]
        value = source.expression

        if not (clamp_min or clamp_max):
            widening = (source.bits is None) and (target.bits is None) and (target.data_type.size >= source.data_type.size)
            return (value if widening or source.data_type == target.data_type else f"({target_type}) {value}"), None

        # Each bound comes from the narrower range of the two
        lower = target.literal(target_min) if clamp_min else source.literal(source_min)
        upper = target.literal(target_max) if clamp_max else source.literal(source_max)

        if source.data_type in WIDE_TYPES:
            if source.data_type in SIGNED_TYPES:
                self.helpers.add(CLAMP_INT64)
                return f"({target_type}) MEEM_ClampInt64({value}, {lower}, {upper})", "clamped"
            self.helpers.add(CLAMP_UINT64)
            return f"({target_type}) MEEM_ClampUint64({value}, {upper})", "clamped"

        if clamp_min and clamp_max:
            return f"({target_type}) (({value} < {lower}) ? {lower} : (({value} > {upper}) ? {upper} : {value}))", "clamped"
        if clamp_min:
            return f"({target_type}) (({value} < {lower}) ? {lower} : {value})", "clamped"
        return f"({target_type}) (({value} > {upper}) ? {upper} : {value})", "clamped"


def helper_definitions(helpers: Set[tuple]) -> str:
    """The conversion helpers that the routines need, in a fixed order"""
    txt = ""

    if CLAMP_INT64 in helpers:
        txt += "/* Clamps a value into [min, max], for the conversions from int64 */\n"
        txt += "static int64_t MEEM_ClampInt64(int64_t value, int64_t min, int64_t max)\n"
        txt += "{\n"
        txt += "    if (value < min)\n"
        txt += "    {\n"
        txt += "        return min;\n"
        txt += "    }\n"
        txt += "    return (value > max) ? max : value;\n"
        txt += "}\n\n"

    if CLAMP_UINT64 in helpers:
        txt += "/* Clamps a value to max, for the conversions from uint64 */\n"
        txt += "static uint64_t MEEM_ClampUint64(uint64_t value, uint64_t max)\n"
        txt += "{\n"
        txt += "    return (value > max) ? max : value;\n"
        txt += "}\n\n"

    if FLOAT64_TO_FLOAT32 in helpers:
        txt += "/* float64 -> float32: the nearest value, clamped to +-FLT_MAX. NaN stays NaN, +-inf saturates to +-FLT_MAX. */\n"
        txt += "static float MEEM_ConvertFloat64ToFloat32(double value)\n"
        txt += "{\n"
        txt += "    if (value > FLT_MAX)\n"
        txt += "    {\n"
        txt += "        return FLT_MAX;\n"
        txt += "    }\n"
        txt += "    if (value < -FLT_MAX)\n"
        txt += "    {\n"
        txt += "        return -FLT_MAX;\n"
        txt += "    }\n"
        txt += "    return (float) value;\n"
        txt += "}\n\n"

    float_to_integer = sorted((h for h in helpers if h[0] == "float" and h != FLOAT64_TO_FLOAT32), key=lambda h: (h[1], h[2]))
    for source in sorted({source for _, source, _ in float_to_integer}):
        txt += is_nan_helper(source) + "\n"
    for _, source, target in float_to_integer:
        txt += float_to_integer_helper(source, target) + "\n"
    return txt


def is_nan_helper(source: DataTypes) -> str:
    """Tells a NaN by its bits, which works also where comparisons don't follow IEEE 754: to XC8, a NaN compares like an infinity,
    and value != value is false. The bits are read in the byte order of the target, which is the same for both union members."""
    bits_type, mask, infinity = (
        ("uint32_t", "0x7FFFFFFFUL", "0x7F800000UL") if source == DataTypes.float32 else ("uint64_t", "0x7FFFFFFFFFFFFFFFULL", "0x7FF0000000000000ULL")
    )

    txt = f"/* Whether a {source.name} value is a NaN, told by its bits, since not every compiler's comparisons follow IEEE 754 */\n"
    txt += f"static bool MEEM_IsNan{type_name(source)}({C_TYPES[source]} value)\n"
    txt += "{\n"
    txt += "    union\n"
    txt += "    {\n"
    txt += f"        {C_TYPES[source]} value;\n"
    txt += f"        {bits_type} bits;\n"
    txt += "    } number;\n"
    txt += "\n"
    txt += "    number.value = value;\n"
    txt += f"    return (number.bits & {mask}) > {infinity};\n"
    txt += "}\n"
    return txt


def float_to_integer_helper(source: DataTypes, target: DataTypes) -> str:
    """Rounds half away from zero, then clamps. The limits are compared as the floating-point type, in which the maximum of a wide
    integer type rounds up to the next power of two: every smaller value is then an integer that fits."""
    float_type, integer_type = C_TYPES[source], C_TYPES[target]
    suffix = "f" if source == DataTypes.float32 else ""
    minimum, maximum = limit_macros(target)
    is_nan = f"MEEM_IsNan{type_name(source)}(value)"

    txt = f"/* {source.name} -> {target.name}: rounded half away from zero, then clamped. NaN gives 0. */\n"
    txt += f"static {integer_type} MEEM_Convert{type_name(source)}To{type_name(target)}({float_type} value)\n"
    txt += "{\n"
    txt += f"    {integer_type} result;\n"
    txt += "\n"

    if target in SIGNED_TYPES:
        txt += f"    if ({is_nan})\n"
        txt += "    {\n"
        txt += "        return 0;\n"
        txt += "    }\n"
        txt += f"    if (value >= ({float_type}) {maximum})\n"
        txt += "    {\n"
        txt += f"        return {maximum};\n"
        txt += "    }\n"
        txt += f"    if (value <= ({float_type}) {minimum})\n"
        txt += "    {\n"
        txt += f"        return {minimum};\n"
        txt += "    }\n"
        txt += f"    result = ({integer_type}) value; /* Towards zero */\n"
        txt += f"    value -= ({float_type}) result;\n"
        txt += f"    if (value >= 0.5{suffix})\n"
        txt += "    {\n"
        txt += "        result++;\n"
        txt += "    }\n"
        txt += f"    else if (value <= -0.5{suffix})\n"
        txt += "    {\n"
        txt += "        result--;\n"
        txt += "    }\n"
    else:
        txt += f"    if ({is_nan} || (value <= 0.0{suffix}))\n"
        txt += "    {\n"
        txt += "        return 0u;\n"
        txt += "    }\n"
        txt += f"    if (value >= ({float_type}) {maximum})\n"
        txt += "    {\n"
        txt += f"        return {maximum};\n"
        txt += "    }\n"
        txt += f"    result = ({integer_type}) value; /* Towards zero */\n"
        txt += f"    if ((value - ({float_type}) result) >= 0.5{suffix})\n"
        txt += "    {\n"
        txt += "        result++;\n"
        txt += "    }\n"

    txt += "    return result;\n"
    txt += "}\n"
    return txt
