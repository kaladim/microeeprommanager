/*!
 * \file    MEEM_UserCallbacks.h
 * \brief   User-implemented callbacks, expected by the mEEM core.
 * \author  Kaloyan Dimitrov
 * \copyright Copyright (c) 2025 Kaloyan Dimitrov
 *            https://github.com/kaladim
 *            SPDX-License-Identifier: MIT
 */
#ifndef MEEM_USERCALLBACKS_H
#define MEEM_USERCALLBACKS_H

/******************************************************************************/
/*    Dependencies                                                            */
/******************************************************************************/
#include <stdint.h>
#include "MEEM_Linkage.h"

/******************************************************************************/
/*    Required operations                                                     */
/******************************************************************************/
/*!
 * \brief     The mEEM core obtains the initially selected active instance of a multi-profile block via this callback.
 * \note      Called in the context of #MEEM_Init().
 * \note      Called for multi-profile blocks only.
 * \note      Hint: since the blocks are initialized sequentially in the order they appear in the configuration array, the user can obtain the active index from
 * an other, already initialized block.
 * \param[in] block_id in the range [0..(MEEM_BLOCK_COUNT-1)]
 * \return    Active instance ID
 */
EXTERN_C uint8_t MEEM_SelectInitiallyActiveProfile(uint8_t block_id);

/*!
 * \brief     The mEEM core notifies the user about completed block initialization via this callback.
 * \note      Called in the context of #MEEM_Init().
 *            Then, if necessary, the user may call #MEEM_GetBlockStatus() to obtain additional info about the init process.
 * \param[in] block_id in the range [0..(MEEM_BLOCK_COUNT-1)]
 */
EXTERN_C void MEEM_OnBlockInitComplete(uint8_t block_id);

/*!
 * \brief     The mEEM core notifies the user about the start of a write operation via this callback.
 * \note      Called in the context of #MEEM_PeriodicTask().
 * \param[in] block_id in the range [0..(MEEM_BLOCK_COUNT-1)]
 */
EXTERN_C void MEEM_OnBlockWriteStarted(uint8_t block_id);

/*!
 * \brief     The mEEM core notifies the user about the completion of a write operation via this callback. The user is expected to check the status of the
 * operation, by calling #MEEM_GetBlockStatus().
 * \note      Called in the context of #MEEM_PeriodicTask().
 * \param[in] block_id in the range [0..(MEEM_BLOCK_COUNT-1)]
 */
EXTERN_C void MEEM_OnBlockWriteComplete(uint8_t block_id);

/*!
 * \brief     The mEEM core notifies the user about the start of a fetch operation via this callback.
 * \note      Called in the context of #MEEM_PeriodicTask().
 * \note      Called for multi-profile blocks only.
 * \param[in] block_id in the range [0..(MEEM_BLOCK_COUNT-1)]
 */
EXTERN_C void MEEM_OnMultiProfileBlockFetchStarted(uint8_t block_id);

/*!
 * \brief     The mEEM core notifies the user about the completion of a fetch operation via this callback.
 * \note      Called in the context of #MEEM_PeriodicTask().
 * \note      Called for multi-profile blocks only.
 * \param[in] block_id in the range [0..(MEEM_BLOCK_COUNT-1)]
 */
EXTERN_C void MEEM_OnMultiProfileBlockFetchComplete(uint8_t block_id);

#endif /* MEEM_USERCALLBACKS_H */
