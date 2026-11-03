/*!
 * \file    MEEM_CRC.c
 * \brief   Calculation of the instance checksum: CRC-16/IBM-3740.
 *          Poly 0x1021, init 0xFFFF, no reflection, no final XOR. Check value 0x29B1 for "123456789".
 *          The generated MEEM_CRC_IMPLEMENTATION selects the implementation: the built-in routine with a lookup table (fastest) or bitwise (smallest),
 *          or the MCU's CRC unit through MEEM_CRCIF.h.
 * \author  Kaloyan Dimitrov
 * \copyright Copyright (c) 2026 Kaloyan Dimitrov
 *            https://github.com/kaladim
 *            SPDX-License-Identifier: MIT
 */
/******************************************************************************/
/*    Dependencies                                                            */
/******************************************************************************/
#include "MEEM_GenConfig.h"
#include "MEEM_Internal.h"
#include <assert.h>

/******************************************************************************/
/*    Macros                                                                  */
/******************************************************************************/
#define CRC16_POLYNOMIAL    0x1021u
#define CRC16_INITIAL_VALUE 0xFFFFu

#if (MEEM_CRC_IMPLEMENTATION != MEEM_CRC_TABLE) && (MEEM_CRC_IMPLEMENTATION != MEEM_CRC_BITWISE) && (MEEM_CRC_IMPLEMENTATION != MEEM_CRC_HARDWARE)
#error "Unknown MEEM_CRC_IMPLEMENTATION! Regenerate the mEEM configuration."
#endif

/******************************************************************************/
/*    Internal operations                                                     */
/******************************************************************************/
#if (MEEM_CRC_IMPLEMENTATION == MEEM_CRC_HARDWARE)
/*!
 * \brief         Calculates the checksum of the instance in the work buffer on the CRC unit, over the size of the read/write request.
 *                Begins the calculation in MEEM_IO_BEGIN_CHECKSUM, and polls for its result in MEEM_IO_WAITING_CHECKSUM.
 * \param[in,out] stage - stage of the read or the write operation. Moves on to MEEM_IO_WAITING_CHECKSUM when the unit accepts the calculation.
 * \param[out]    checksum - the result, valid with MEEM_OK
 * \retval        MEEM_OK - the checksum is in
 * \retval        MEEM_BUSY - still calculating, or the unit is busy and refused to begin. Call again later.
 * \retval        MEEM_NOK - the unit failed
 */
MEEM_status_t MEEM_ChecksumTask(MEEM_ioStage_t* stage, MEEM_checksum_t* checksum)
{
    CRCIF_status_t crc_status;
    MEEM_status_t  status;

    if (MEEM_IO_BEGIN_CHECKSUM == *stage)
    {
        crc_status = CRCIF_BeginCalculation(&MEEM_work_buffer[sizeof(MEEM_checksum_t)], (MEEM_global_status.io_request.size - sizeof(MEEM_checksum_t)));

        if (CRCIF_OK == crc_status)
        {
            *stage     = MEEM_IO_WAITING_CHECKSUM;
            crc_status = CRCIF_BUSY; /* The result is polled from the next call on */
        }
    }
    else
    {
        crc_status = CRCIF_GetResult(checksum);
    }

    /* Development error: the application initializes the CRC unit before MEEM_Init() */
    assert(CRCIF_NOT_INITIALIZED != crc_status);

    switch (crc_status)
    {
        case CRCIF_OK:
            status = MEEM_OK;
            break;

        case CRCIF_BUSY:
            status = MEEM_BUSY;
            break;

        default:
            status = MEEM_NOK; /* CRCIF_NOK, or CRCIF_NOT_INITIALIZED with NDEBUG */
            break;
    }

    return status;
}
#else
/*!
 * \brief     Calculates the instance checksum with the built-in routine.
 * \param[in] data - pointer to source data
 * \param[in] data_size - in bytes
 * \return    CRC-16/IBM-3740 of the data
 */
MEEM_checksum_t MEEM_CalculateChecksum(const uint8_t* data, uint16_t data_size)
{
    uint16_t crc = CRC16_INITIAL_VALUE;

    for (uint16_t i = 0; i < data_size; i++)
    {
#if (MEEM_CRC_IMPLEMENTATION == MEEM_CRC_TABLE)
        crc = (uint16_t) ((uint16_t) (crc << 8) ^ MEEM_crc_table[(uint8_t) ((crc >> 8) ^ data[i])]);
#else
        crc ^= (uint16_t) ((uint16_t) data[i] << 8);

        for (uint8_t bit = 0; bit < 8u; bit++)
        {
            crc = (crc & 0x8000u) ? (uint16_t) ((uint16_t) (crc << 1) ^ CRC16_POLYNOMIAL) : (uint16_t) (crc << 1);
        }
#endif
    }

    return (MEEM_checksum_t) crc;
}
#endif
