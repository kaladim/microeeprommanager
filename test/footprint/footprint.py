"""Measures the flash and RAM footprint of mEEM on XC8 targets (v1 baseline and v2 measurements).

Each target is built with xc8-cc from the mEEM core, a configuration generated from its data model, and its
application sources. The linker map is then attributed to mEEM core code, generated code, configuration
constants, core RAM, parameter caches and user code. See README.md for the method and its limits.

Usage:
    uv run test/footprint/footprint.py [--target NAME ...] [--crc table|bitwise] [--xc8 PATH] [--packs DIR] [--out DIR]
    uv run test/footprint/footprint.py --map FILE.map    # attribute an existing map, no build
"""

import argparse
import json
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Tuple

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
EXAMPLE = ROOT / "example" / "Microchip"

DEFAULT_XC8 = "C:/Program Files/Microchip/xc8/v4.00/bin/xc8-cc.exe"
DEFAULT_PACKS = "C:/Program Files/Microchip/MPLABX/v6.35/packs/Microchip"


@dataclass
class Target:
    name: str
    mcpu: str
    dfp: str
    """Device pack directory, relative to --packs"""
    optimization: str
    datamodel: Path
    settings: Path
    sources: List[Path]
    includes: List[Path]
    defines: List[str] = field(default_factory=lambda: ["NDEBUG"])
    flags: List[str] = field(default_factory=list)
    legacy: List[Tuple[Path, Path]] = field(default_factory=list)
    """Legacy models, each with the platform settings it was released with, for the generator's '--legacy'"""


# XC8 copies some const objects to RAM when RAM is free (-fcacheconst, on by default), so the RAM figures
# would depend on how much RAM the rest of the build leaves. All targets switch it off to stay comparable.
COMMON_FLAGS = ["-std=c99", "-mstack=compiled", "-fcacheconst=off"]


def example_target(name: str, optimization: str) -> Target:
    """The v1 example project (mEEM-demo.X) on its PIC16F1825"""
    return Target(
        name=name,
        mcpu="16F1825",
        dfp="PIC12-16F1xxx_DFP/1.9.258",
        optimization=optimization,
        datamodel=EXAMPLE / "MEEM_Config" / "eeprom_datamodel_example.json",
        settings=EXAMPLE / "MEEM_Config" / "platform_settings_xc8.json",
        sources=[
            *sorted((EXAMPLE / "Applications").glob("*.c")),
            EXAMPLE / "EEPROM_Driver_PIC16F" / "EEPROM_Driver_PIC16F.c",
            EXAMPLE / "MEEM_Config" / "MEEM_EEAIF.c",
            EXAMPLE / "MEEM_Config" / "MEEM_UserCallbacks.c",
            EXAMPLE / "main.c",
        ],
        includes=[EXAMPLE, EXAMPLE / "MEEM_Config", EXAMPLE / "EEPROM_Driver_PIC16F", EXAMPLE / "Applications"],
    )


def production_like_target(name: str, datamodel: str, legacy: List[str]) -> Target:
    """The storage shape of a production application on its MCU, with the optimization level of its release build. The data models are those of
    production_like/, each laid out with its platform settings."""
    settings = HERE / "production_like" / "platform_settings_xc8.json"
    return Target(
        name=name,
        mcpu="18F46K22",
        dfp="PIC18F-K_DFP/1.16.308",
        optimization="-Os",
        datamodel=HERE / "production_like" / datamodel,
        settings=settings,
        sources=[HERE / "production_like" / "main.c", HERE / "production_like" / "footprint_user.c"],
        includes=[HERE / "production_like"],
        flags=["-maddrqual=ignore"],
        legacy=[(HERE / "production_like" / model, settings) for model in legacy],
    )


TARGETS = [
    example_target("pic16f1825-example", "-O1"),  # The optimization level of the example project
    example_target("pic16f1825-example-os", "-Os"),  # What a footprint-sensitive PIC16 build would use
    production_like_target("pic18f46k22-production-like", "eeprom_datamodel.json", []),
    # Block09 changed, migrating from the unchanged model
    production_like_target("pic18f46k22-production-like-migration", "eeprom_datamodel_migration.json", ["eeprom_datamodel.json"]),
]

CORE_INCLUDES = [ROOT / "src" / "core", ROOT / "src" / "provided_interface", ROOT / "src" / "required_interface"]

USER_FUNCTIONS = {
    "MEEM_SelectInitiallyActiveProfile",
    "MEEM_OnBlockInitComplete",
    "MEEM_OnBlockWriteStarted",
    "MEEM_OnBlockWriteComplete",
    "MEEM_OnMultiProfileBlockFetchStarted",
    "MEEM_OnMultiProfileBlockFetchComplete",
    "MEEM_OnCompactionComplete",
}
LIBRARY_FUNCTIONS = {"memcpy", "memset"}
RAM_CLASSES = re.compile(r"^(COMMON|COMRAM|BANK\d+|BIGRAM|RAM|ABS1)$")

SYMBOL_LINE = re.compile(r"^_([\w$@?.]+)\s+(\S+)\s+([0-9A-Fa-f]+)\s*$")
PSECT_LINE = re.compile(r"^\s+(\w+)\s+([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)\s+(\d+)\s*$")
CLASS_LINE = re.compile(r"^\s+CLASS\s+(\w+)\s*$")
SUMMARY_LINE = re.compile(r"^\s*(Program|Data) space\s+used\s+[0-9A-Fa-f]+h \(\s*(\d+)\) of\s+([0-9A-Fa-f]+)h (words|bytes)", re.MULTILINE)


@dataclass
class Psect:
    name: str
    cls: str
    link: int
    length: int

    @property
    def is_ram(self) -> bool:
        return RAM_CLASSES.match(self.cls) is not None


def parse_map(path: Path):
    """Returns the psects of the TOTAL section, the global symbols (name -> (psect, address)),
    the end addresses that XC8 records for functions and const objects (name -> address)
    and the const objects that XC8 cached in RAM (name -> size)."""
    psects: Dict[str, Psect] = {}
    symbols: Dict[str, tuple] = {}
    ends: Dict[str, int] = {}
    cached: Dict[str, int] = {}
    section = ""
    cls = ""

    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("TOTAL"):
            section = "total"
            continue
        if line.startswith("Symbols cached in RAM"):
            section = "cached"
            continue
        if section == "total":
            if line and not line[0].isspace():
                section = ""
            elif m := CLASS_LINE.match(line):
                cls = m.group(1)
                continue
            elif m := PSECT_LINE.match(line):
                psects[m.group(1)] = Psect(m.group(1), cls, int(m.group(2), 16), int(m.group(4), 16))
                continue
        if section == "cached":
            if m := re.match(r"^_(\w+)\s+(\d+)\s*$", line):
                cached[m.group(1)] = int(m.group(2))
                continue
            if line and not line.startswith("_"):
                section = ""
        # Other names starting with a double underscore are psect bounds (__p, __L, __H), not objects
        if m := SYMBOL_LINE.match(line):
            if m.group(1).startswith("_end_of_"):
                ends[m.group(1)[len("_end_of_") :]] = int(m.group(3), 16)
            elif not m.group(1).startswith("_"):
                symbols[m.group(1)] = (m.group(2), int(m.group(3), 16))
    return psects, symbols, ends, cached


def classify_function(name: str) -> str:
    if name.startswith("EEAIF_") or name in USER_FUNCTIONS:
        return "user"
    if name in ("MEEM_ValidateConfiguration", "MEEM_MigrateBlock") or name.startswith(("MEEM_Get_", "MEEM_Set_", "MEEM_Migrate_", "MEEM_Convert", "MEEM_Clamp")):
        return "generated"
    if name.startswith("MEEM_"):
        return "core"
    if name in LIBRARY_FUNCTIONS:
        return "library"
    return "other"


def classify_object(name: str, in_ram: bool) -> Optional[str]:
    if not name.startswith("MEEM_"):
        return None
    if name.startswith("MEEM_cache"):
        return "caches"
    if name == "MEEM_block_config" or name.startswith(("MEEM_defaults_", "MEEM_legacy_")):
        return "config_ram" if in_ram else "config_flash"
    return "core_ram" if in_ram else "core_flash"


def attribute(psects: Dict[str, Psect], symbols: Dict[str, tuple], ends: Dict[str, int], cached: Dict[str, int]) -> dict:
    """Attributes function psects and data objects to the mEEM categories."""
    code = {"core": 0, "generated": 0, "user": 0, "library": 0}
    functions = {}
    objects = {}
    data = {"core_ram": 0, "caches": 0, "config_ram": 0, "config_flash": 0, "core_flash": 0}

    # Functions: XC8 normally gives every function its own psect. At -Os, code factored out by procedural abstraction
    # stays in that psect behind the function's end label, and on PIC16 several functions may share one psect.
    # So every psect is counted once and split among its functions in proportion to their sizes up to the end label.
    by_code_psect: Dict[str, List[tuple]] = {}
    for name, (psect_name, address) in symbols.items():
        psect = psects.get(psect_name)
        if psect is None or psect.cls != "CODE" or not (psect_name.startswith("text") or psect_name == "maintext"):
            continue
        by_code_psect.setdefault(psect_name, []).append((name, ends[name] - address if name in ends else psect.length))

    for psect_name, members in by_code_psect.items():
        own_total = sum(own for _, own in members) or 1
        for name, own in members:
            category = classify_function(name)
            if category in code:
                size = round(psects[psect_name].length * own / own_total)
                code[category] += size
                functions[name] = {"category": category, "size": size}

    # Objects: const objects have an end label. RAM objects share a data psect without one,
    # so their size is the distance to the next symbol or to the psect end.
    by_psect: Dict[str, List[tuple]] = {}
    for name, (psect_name, address) in symbols.items():
        psect = psects.get(psect_name)
        if psect is None or psect.cls == "CODE" or psect_name.startswith("cstack") or any(c in name for c in "$@?"):
            continue
        by_psect.setdefault(psect_name, []).append((address, name))

    for psect_name, members in by_psect.items():
        psect = psects[psect_name]
        members.sort()
        for i, (address, name) in enumerate(members):
            end = ends.get(name) or (members[i + 1][0] if i + 1 < len(members) else psect.link + psect.length)
            category = classify_object(name, psect.is_ram)
            if category:
                data[category] += end - address
                objects[name] = {"category": category, "psect": psect_name, "size": end - address}

    cstack = sum(p.length for p in psects.values() if p.name.startswith("cstack") and p.is_ram)
    cached_meem = {name: size for name, size in cached.items() if name.startswith("MEEM_")}
    return {"code": code, "data": data, "compiled_stack": cstack, "cached_in_ram": cached_meem, "functions": functions, "objects": objects}


def build(target: Target, xc8: str, packs: Path, out_root: Path, crc: Optional[str] = None) -> dict:
    """Builds the target. crc overrides the 'crc_implementation' of its platform settings, and suffixes its output directory."""
    out = (out_root / (f"{target.name}-{crc}" if crc else target.name)).resolve()  # xc8-cc runs inside it, so relative paths would break
    shutil.rmtree(out, ignore_errors=True)
    generated = out / "generated"
    generated.mkdir(parents=True)

    settings_json = json.loads(target.settings.read_text(encoding="utf-8"))
    settings = target.settings
    if crc:
        settings_json["crc_implementation"] = crc
        settings = out / "platform_settings.json"
        settings.write_text(json.dumps(settings_json, indent=4), encoding="utf-8")

    legacy_args = [arg for model, model_settings in target.legacy for arg in ("--legacy", str(model), str(model_settings))]
    subprocess.run(
        [sys.executable, str(ROOT / "tools" / "meem_config_gen" / "meem_config_gen.py"), str(target.datamodel), str(settings), str(generated), *legacy_args],
        check=True,
        capture_output=True,
    )

    sources = [
        *sorted((ROOT / "src" / "core").glob("*.c")),
        generated / "MEEM_GenConfig.c",
        generated / "MEEM_GenInterface.c",
        generated / "MEEM_Migration.c",
        *target.sources,
    ]
    includes = [generated, *CORE_INCLUDES, *target.includes]
    map_file = out / "footprint.map"
    args = [
        xc8,
        f"-mcpu={target.mcpu}",
        f"-mdfp={packs / target.dfp / 'xc8'}",
        target.optimization,
        *COMMON_FLAGS,
        *target.flags,
        *(f"-D{d}" for d in target.defines),
        *(f"-I{p}" for p in includes),
        f"-Wl,-Map={map_file}",
        "-o",
        str(out / "footprint.elf"),
        *(str(s) for s in sources),
    ]
    result = subprocess.run(args, cwd=out, capture_output=True, text=True)
    output = result.stdout + result.stderr
    (out / "xc8.log").write_text(output, encoding="utf-8")
    if result.returncode != 0:
        raise RuntimeError(f"xc8-cc failed for {target.name}, see {out / 'xc8.log'}")

    summary = {m.group(1).lower(): {"used": int(m.group(2)), "capacity": int(m.group(3), 16), "unit": m.group(4)} for m in SUMMARY_LINE.finditer(output)}
    notes = [line.strip() for line in output.splitlines() if re.search(r"licen|free mode|optimi[sz]ation", line, re.IGNORECASE)]

    report = {
        "target": target.name,
        "mcpu": target.mcpu,
        "optimization": " ".join([target.optimization, *COMMON_FLAGS, *target.flags]),
        "crc_implementation": settings_json.get("crc_implementation", "table"),
        "legacy_models": [model.name for model, _ in target.legacy],
        "summary": summary,
        "notes": notes,
    }
    report.update(attribute(*parse_map(map_file)))
    (out / "footprint.json").write_text(json.dumps(report, indent=4), encoding="utf-8")
    return report


def print_report(report: dict):
    unit = report.get("summary", {}).get("program", {}).get("unit", "units")
    code, data = report["code"], report["data"]
    print(f"\n== {report['target']} ({report.get('mcpu', '?')}, {report.get('optimization', '?')}, crc_implementation {report.get('crc_implementation', '?')})")
    if report.get("legacy_models"):
        print(f"   legacy models: {', '.join(report['legacy_models'])}")
    for space in ("program", "data"):
        if space in report.get("summary", {}):
            s = report["summary"][space]
            print(f"   {space:<8} total: {s['used']} of {s['capacity']} {s['unit']}")
    print(f"   code ({unit}):  core {code['core']}, generated {code['generated']}, user {code['user']}, memcpy/memset {code['library']}")
    print(f"   flash const ({unit}): config {data['config_flash']}, core {data['core_flash']}")
    print(f"   RAM (bytes): core {data['core_ram']}, caches {data['caches']}, config in RAM {data['config_ram']}, compiled stack (whole program) {report['compiled_stack']}")
    if report["cached_in_ram"]:
        print(f"   const cached in RAM by XC8: {sum(report['cached_in_ram'].values())} bytes ({', '.join(report['cached_in_ram'])})")
    for note in report.get("notes", []):
        print(f"   note: {note}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--target", action="append", choices=[t.name for t in TARGETS], help="Target to build; repeatable. Default: all")
    parser.add_argument("--xc8", default=shutil.which("xc8-cc") or DEFAULT_XC8, help="Path to xc8-cc")
    parser.add_argument("--packs", default=DEFAULT_PACKS, help="Directory of the Microchip device packs")
    parser.add_argument("--out", default=str(ROOT / "build" / "footprint"), help="Output directory")
    parser.add_argument("--crc", choices=["table", "bitwise"], help="Overrides 'crc_implementation' of the platform settings. Default: the settings' own")
    parser.add_argument("--map", help="Only attribute this existing map file")
    args = parser.parse_args()

    if args.map:
        report = {"target": Path(args.map).name}
        report.update(attribute(*parse_map(Path(args.map))))
        print_report(report)
        return

    for target in [t for t in TARGETS if not args.target or t.name in args.target]:
        print_report(build(target, args.xc8, Path(args.packs), Path(args.out), args.crc))


if __name__ == "__main__":
    main()
