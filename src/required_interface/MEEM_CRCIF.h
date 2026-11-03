/*!
 * \file    MEEM_CRCIF.h
 * \brief   Interface to the driver of the MCU's CRC unit, expected by the mEEM core with 'crc_implementation' 'hardware' only.
 *          The unit must calculate CRC-16/IBM-3740: poly 0x1021, init 0xFFFF, no reflection, xorout 0. Check value 0x29B1 for "123456789".
 * \note    The application initializes the unit before #MEEM_Init(). The core doesn't, because the application may use the unit too.
 * \author  Kaloyan Dimitrov
 * \copyright Copyright (c) 2026 Kaloyan Dimitrov
 *            https://github.com/kaladim
 *            SPDX-License-Identifier: MIT
 */
#ifndef MEEM_CRCIF_H
#define MEEM_CRCIF_H

/******************************************************************************/
/*    Dependencies                                                            */
/******************************************************************************/
#include <stdint.h>
#include "MEEM_Linkage.h"

/******************************************************************************/
/*    Types                                                                   */
/******************************************************************************/
typedef enum {
    CRCIF_OK = 0,
    CRCIF_NOK,
    CRCIF_BUSY,
    CRCIF_NOT_INITIALIZED
} CRCIF_status_t;

/******************************************************************************/
/*    Required operations                                                     */
/******************************************************************************/
/*!
 * \brief   Tries to begin a CRC-16/IBM-3740 calculation: poly 0x1021, init 0xFFFF, no reflection, xorout 0.
 * \details Executed in the context of #MEEM_Init() and #MEEM_PeriodicTask().
 * \pre     The calculation is asynchronous and this function is expected to return immediately!
 * \param[in] data source buffer. The core leaves it unchanged until the result is in.
 * \param[in] size byte count
 * \retval  CRCIF_OK - the request is accepted
 * \retval  CRCIF_BUSY - the unit is busy, e.g. with a calculation of the application. The core retries later.
 *          The application must not hold the unit while #MEEM_Init() runs, which waits for it.
 * \retval  CRCIF_NOK - the unit failed. The driver retries itself where that makes sense; the core doesn't.
 * \retval  CRCIF_NOT_INITIALIZED - the unit isn't initialized (development error)
 */
EXTERN_C CRCIF_status_t CRCIF_BeginCalculation(const uint8_t* data, uint16_t size);

/*!
 * \brief   Gets the result of the accepted calculation.
 * \details Executed in the context of #MEEM_Init() and #MEEM_PeriodicTask().
 * \param[out] crc the CRC, valid with CRCIF_OK
 * \retval  CRCIF_OK - the calculation is complete
 * \retval  CRCIF_BUSY - it's still running
 * \retval  CRCIF_NOK - it failed
 * \retval  CRCIF_NOT_INITIALIZED - the unit isn't initialized (development error)
 */
EXTERN_C CRCIF_status_t CRCIF_GetResult(uint16_t* crc);

#endif /* MEEM_CRCIF_H */
