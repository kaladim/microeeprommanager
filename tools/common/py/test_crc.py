import os
import sys
import random
import unittest

sys.path.append(os.path.dirname(os.path.dirname(os.path.dirname(__file__))))
from common.py.crc import CRC16_TABLE, crc16, crc32


def crc16_bitwise(data: bytes) -> int:
    """Reference: CRC-16/IBM-3740 shifted bit by bit, as the core's 'bitwise' routine does"""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if (crc & 0x8000) else (crc << 1) & 0xFFFF
    return crc


class TestCrc16(unittest.TestCase):
    def test_check_value(self):
        self.assertEqual(crc16(b"123456789"), 0x29B1)

    def test_table(self):
        self.assertEqual(len(CRC16_TABLE), 256)
        self.assertEqual(CRC16_TABLE[:4], [0x0000, 0x1021, 0x2042, 0x3063])
        self.assertEqual(CRC16_TABLE[255], 0x1EF0)

        for byte in range(256):
            # Each entry is the byte shifted through a zero register
            expected = byte << 8
            for _ in range(8):
                expected = ((expected << 1) ^ 0x1021) & 0xFFFF if (expected & 0x8000) else (expected << 1) & 0xFFFF
            self.assertEqual(CRC16_TABLE[byte], expected, f"entry {byte}")

    def test_table_routine_matches_bitwise_routine(self):
        rng = random.Random(0)

        for length in (0, 1, 2, 7, 64, 255, 1000):
            data = bytes(rng.randrange(256) for _ in range(length))
            self.assertEqual(crc16(data), crc16_bitwise(data), f"length {length}")

    def test_calculation_can_be_continued(self):
        data = b"123456789"
        self.assertEqual(crc16(data[4:], crc=crc16(data[:4])), crc16(data))

    def test_blank_and_zeroed_instances_never_pass(self):
        """An instance of all 0x00 or all 0xFF bytes, checksum included, never passes its check"""
        crc_of_zeros = crc_of_ones = 0xFFFF

        for data_size in range(1, 4097):
            crc_of_zeros = crc16(b"\x00", crc_of_zeros)
            crc_of_ones = crc16(b"\xff", crc_of_ones)

            self.assertNotEqual(crc_of_zeros, 0x0000, f"An all-0x00 instance with {data_size} B of data passes")
            self.assertNotEqual(crc_of_ones, 0xFFFF, f"An all-0xFF instance with {data_size} B of data passes")


class TestCrc32(unittest.TestCase):
    def test_check_value(self):
        self.assertEqual(crc32(b"123456789"), 0xCBF43926)


if __name__ == "__main__":
    unittest.main()
