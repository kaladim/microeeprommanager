#ifndef REFERENCE_CRC16_HPP
#define REFERENCE_CRC16_HPP

#include <cstddef>
#include <cstdint>

/*!
 * \brief Reference CRC-16/IBM-3740, shifted bit by bit: poly 0x1021, init 0xFFFF, no reflection, xorout 0
 * \param initial_value 0xFFFF for CRC-16/IBM-3740. Another value gives the CRC of a misconfigured unit.
 */
inline uint16_t ReferenceCrc16(const uint8_t* data, size_t size, uint16_t initial_value = 0xFFFF)
{
    uint16_t crc = initial_value;

    for (size_t i = 0; i < size; i++)
    {
        crc ^= static_cast<uint16_t>(data[i] << 8);

        for (int bit = 0; bit < 8; bit++)
        {
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

#endif // REFERENCE_CRC16_HPP
