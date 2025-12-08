/*!
 * \file    MEEM_EEAIF.h
 * \brief   Interface to the EEPROM access driver, expected by the mEEM core.
 * \author  Kaloyan Dimitrov
 * \copyright Copyright (c) 2025 Kaloyan Dimitrov
 *            https://github.com/kaladim
 *            SPDX-License-Identifier: MIT
 */
#ifndef MEEM_EEAIF_H
#define MEEM_EEAIF_H

/******************************************************************************/
/*    Dependencies                                                            */
/******************************************************************************/
#include <stdbool.h>
#include <stdint.h>
#include "MEEM_Linkage.h"

/******************************************************************************/
/*    Types                                                                   */
/******************************************************************************/
typedef enum {
    EEAIF_OK = 0,
    EEAIF_NOK,
    EEAIF_BUSY,
    EEAIF_IDLE
} EEAIF_status_t;

/******************************************************************************/
/*    Required operations                                                     */
/******************************************************************************/
/*!
 * \brief   Initializes your EEPROM access driver
 * \details Called once by the mEEM core. Executed in the context of #MEEM_Init().
 * \pre     This has to be a synchronous/blocking operation.
 */
EXTERN_C void EEAIF_Init(void);

/*!
 * \brief   De-initializes your EEPROM access driver
 * \details Called by the mEEM core from #MEEM_DeInit().
 */
EXTERN_C void EEAIF_DeInit(void);

/*!
 * \brief  Execute the cyclic jobs of the EEPROM driver.
 * \details Executed in the context of #MEEM_PeriodicTask().
 * \pre    This has to be an asynchronous/non-blocking operation.
 * \note   If the implementation of your EEPROM access driver is entirely interrupt-driven, leave this function empty.
 * \note   Called by the mEEM core with same frequency as the #MEEM_PeriodicTask()
 */
EXTERN_C void EEAIF_Task(void);

/*!
 * \brief   Tries to push a read request to the EEPROM access driver.
 * \details Executed in the context of #MEEM_PeriodicTask().
 * \pre     The read operation is asynchronous and this function is expected to return immediately!
 * \param[in]  offset_in_eeprom not an absolute address. The EEPROM access driver is responsible to map to an absolute address!
 * \param[out] dest destination buffer
 * \param[in]  size byte count
 * \retval  true if the request is accepted
 * \retval  false if the request is rejected: either the driver is busy with another operation or some of the parameters is invalid
 */
EXTERN_C bool EEAIF_BeginRead(uint16_t offset_in_eeprom, uint8_t* dest, uint16_t size);

/*!
 * \brief   Tries to push a write request to the EEPROM access driver.
 * \details Executed in the context of #MEEM_PeriodicTask().
 * \pre     The write operation is asynchronous and this function is expected to return immediately!
 * \pre     The driver is expected to verify the written data and, if there's a discrepancy, retry several times!
 * \param[in] offset_in_eeprom not an absolute address. The EEPROM access driver is responsible to map to an absolute address!
 * \param[in] source source buffer
 * \param[in] size byte count
 * \retval  true if the request is accepted
 * \retval  false if the request is rejected: either the driver is busy with another operation or some of the parameters is invalid
 */
EXTERN_C bool EEAIF_BeginWrite(uint16_t offset_in_eeprom, const uint8_t* source, uint16_t size);

/*!
 * \brief   Gets the status of last accepted request.
 * \details Executed in the context of #MEEM_PeriodicTask().
 * \pre     A synchronous/blocking operation is expected
 * \retval  EEAIF_IDLE - immediately after #EEAIF_Init() until the first accepted request
 * \retval  EEAIF_BUSY - if the driver executes a request at the moment.
 * \retval  EEAIF_OK - if the last request completed successfully
 * \retval  EEAIF_NOK - if the last request failed to complete
 *          For read requests this would mean:
 *          - If an external EEPROM is used -> serial bus failure
 *          - If an internal onchip EEPROM is used -> MCU failure or misconfiguration (reads should always be possible within the MCU)
 *          For write requests this would mean either:
 *          - Serial bus failure
 *          - Memory cell/page wearout (assuming the driver verifies the written data).
 */
EXTERN_C EEAIF_status_t EEAIF_GetStatus(void);

#endif /* MEEM_EEAIF_H */
