/*!
 * \file    MEEM_Checksum.h
 * \brief   Checksum calculation routine, expected by the mEEM core.
 * \author  Kaloyan Dimitrov
 * \copyright Copyright (c) 2025 Kaloyan Dimitrov
 *            https://github.com/kaladim
 *            SPDX-License-Identifier: MIT
 */
#ifndef MEEM_CHECKSUM_H
#define MEEM_CHECKSUM_H

/******************************************************************************/
/*    Dependencies                                                            */
/******************************************************************************/
#include "MEEM_GenConfig.h"

/******************************************************************************/
/*    Required operations                                                     */
/******************************************************************************/
/*!
 * \brief     Checksum calculation routine.
 * \details   Executed in the context of the #MEEM_PeriodicTask() function.
 * \pre       The reliability of the MEEM depends mostly on the chosen checksum algorithm. Choose it carefully!
 * \pre       A synchronous/blocking operation is expected.
 * \param[in] data - pointer to source data
 * \param[in] data_size - in bytes
 * \return    checksum
 */
EXTERN_C MEEM_checksum_t MEEM_CalculateChecksum(const void* data, uint16_t data_size);

#endif /* MEEM_CHECKSUM_H */
