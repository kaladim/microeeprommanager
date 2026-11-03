"""The CRCs of mEEM v2.

- CRC-16/IBM-3740: the instance checksum and the hash of the data and block identities.
  Poly 0x1021, init 0xFFFF, no reflection, no final XOR, check value 0x29B1 for "123456789".
- CRC-32/ISO-HDLC: the hash of the EEPROM layout identity, as Python's zlib.crc32().
  Poly 0x04C11DB7, init 0xFFFFFFFF, reflected input and output, final XOR 0xFFFFFFFF, check value 0xCBF43926.
"""

import zlib
from typing import List

CRC16_POLYNOMIAL = 0x1021
CRC16_INITIAL_VALUE = 0xFFFF


def make_crc16_table() -> List[int]:
    """Returns the 256-entry lookup table of CRC-16/IBM-3740: the CRC register after shifting each byte value through it."""
    table = []

    for byte in range(256):
        crc = byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ CRC16_POLYNOMIAL) if (crc & 0x8000) else (crc << 1)
        table.append(crc & 0xFFFF)

    return table


CRC16_TABLE = make_crc16_table()


def crc16(data: bytes | bytearray, crc: int = CRC16_INITIAL_VALUE) -> int:
    """CRC-16/IBM-3740 of the data. Pass a previous result as 'crc' to continue a calculation over more data."""
    for byte in data:
        crc = ((crc << 8) & 0xFFFF) ^ CRC16_TABLE[(crc >> 8) ^ byte]

    return crc


def crc32(data: bytes | bytearray) -> int:
    """CRC-32/ISO-HDLC of the data"""
    return zlib.crc32(data)
