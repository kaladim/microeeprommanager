import os
from abc import ABC
from datetime import datetime
from enum import IntEnum
from tzlocal import get_localzone


class CodeGenerator(ABC):
    class TextAlignment(IntEnum):
        Left = 0
        Center = 1
        Right = 2

    class Enclosure(IntEnum):
        Opening = 0
        Closing = 1

    def __init__(self):
        self.max_line_length: int = 80
        self.author: str = "Kaloyan Dimitrov"
        """Author's name"""

        self.application_name: str = "Micro EEPROM manager"
        """Name of the application for which we generate source files"""

        self.generator_name: str = "mEEM generator"
        """Name of the tool, producing the code"""

        self.generator_version: dict = {"major": 0, "minor": 1}
        """Version of the tool, producing the code."""

    def compose_file(
        self,
        config_name: str,
        file_name: str,
        file_content: str,
    ) -> str:
        """Produces final source file (.c or .h) by incorporating timestamp,multiple inclusion protection macro and file content."""

        is_header = file_name.lower().endswith(".h")
        txt = self.generate_file_header(file_name, config_name) + "\n"

        if is_header:
            txt += self.to_multilple_inclusion_protection_macro(file_name, CodeGenerator.Enclosure.Opening) + "\n"

        txt += file_content + "\n"

        if is_header:
            txt += self.to_multilple_inclusion_protection_macro(file_name, CodeGenerator.Enclosure.Closing)
        return txt

    def generate_file_header(self, file_name: str, config_name: str) -> str:
        """Generates information file header as comment box.
        Parameters:
        file_name (str): Name of the generated file, including extention
        config_name (str): Name of the data source configuration
        """
        txt = ""
        txt += " \\file   " + file_name + "\n"
        txt += " \\author " + self.author + "\n"
        txt += " \\date   " + datetime.now(tz=get_localzone()).strftime("%Y-%m-%d %H:%M:%S %Z (UTC%z)") + "\n"
        txt += " \\note   !!! Do not change manually !!!" + "\n"
        txt += f" \\brief  Generated file for '{self.application_name}'" + "\n"
        txt += "\n"
        txt += "         Generator name    : " + self.generator_name + "\n"
        txt += "         Generator version : " + str(self.generator_version["major"]) + "." + str(self.generator_version["minor"]) + "\n"
        txt += "         Datamodel name    : " + config_name

        return self.to_doxygen_comment_box(txt)

    def to_comment_box(self, text: str, alignment: TextAlignment) -> str:
        """Encloses a text with multiline asterisk box."""

        lines = text.splitlines()
        line_len = len(max(lines, key=len))

        if line_len > (self.max_line_length - 6):
            line_len += 6
        else:
            line_len = self.max_line_length

        txt = "/*" + "".ljust(line_len - 4, "*") + "*/\n"

        for line in lines:
            left_pad: int
            right_pad: int
            total_pad: int = line_len - 4 - len(line)  # 4 is for two comment symbols at the beggining and at the end of the row

            if alignment == CodeGenerator.TextAlignment.Left:
                left_pad = 1
                right_pad = total_pad - left_pad
            elif alignment == CodeGenerator.TextAlignment.Right:
                right_pad = 1
                left_pad = total_pad - right_pad
            else:
                # Aligh: Center
                left_pad = int(total_pad / 2)
                right_pad = total_pad - left_pad

            txt += "/*" + "".ljust(left_pad) + line + "".rjust(right_pad) + "*/\n"

        txt += "/" + "".ljust(line_len - 2, "*") + "/"
        return txt

    def to_doxygen_comment_box(self, multiline_text: str) -> str:
        """Encloses a text with Doxygen-style asterisk box.
        Parameters:
        multiline_text(str): Text to enclose. Can be multiline.
        """
        lines = multiline_text.splitlines()
        txt = "/*!\n"

        for line in lines:
            txt += " *" + line + "\n"

        txt += " */"
        return txt

    def to_comment_line(self, text: str, alignment: TextAlignment) -> str:
        line_length: int = len(text)
        left_pad: int
        right_pad: int
        total_pad: int

        if len(text) > (self.max_line_length - 6):
            line_length += 6
        else:
            line_length = self.max_line_length

        total_pad = line_length - 4 - len(text)
        if alignment == CodeGenerator.TextAlignment.Left:
            left_pad = 1
            right_pad = 1
        elif alignment == CodeGenerator.TextAlignment.Right:
            right_pad = 1
            left_pad = total_pad - right_pad
        else:
            left_pad = int(total_pad / 2)
            right_pad = total_pad - left_pad

        return "/*" + "".rjust(left_pad) + text + "".ljust(right_pad) + "*/"

    def to_multilple_inclusion_protection_macro(self, file_name: str, enclosure: Enclosure) -> str:
        """Produces multiple inclusion protection macro. Example: fileName.ext -> FILENAME_EXT__"""

        macro = file_name.replace(".", "_").replace(" ", "_").replace("-", "_").upper()
        txt = ""

        if enclosure == CodeGenerator.Enclosure.Opening:
            txt += f"#ifndef {macro}\n"
            txt += f"#define {macro}\n"
        else:
            txt += f"#endif  /* {macro} */"

        return txt
