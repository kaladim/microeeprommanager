/*!
 * \file    MEEM_InitCallbacks.h
 * \brief   User-implemented initialization callbacks, expected by the mEEM core.
 * \author  Kaloyan Dimitrov
 * \copyright Copyright (c) 2025 Kaloyan Dimitrov
 *            https://github.com/kaladim
 *            SPDX-License-Identifier: MIT
 */
#ifndef MEEM_INITCALLBACKS_H
#define MEEM_INITCALLBACKS_H

/******************************************************************************/
/*    Dependencies                                                            */
/******************************************************************************/
#include <stdint.h>
#include "MEEM_Linkage.h"

/******************************************************************************/
/*    Required operations                                                     */
/******************************************************************************/
/*!
 * \brief     During the execution of #MEEM_Init(), the mEEM core calls this function to notify about completed block initialization.
 *            Then, if necessary, the user may call #MEEM_GetBlockStatus() to obtain additional info about the init process.
 * \param[in] block_id in the range [0..(MEEM_BLOCK_COUNT-1)]
 */
EXTERN_C void MEEM_OnBlockInitComplete(uint8_t block_id);

/*!
 * \brief     During the execution of #MEEM_Init(), the mEEM core calls this function to obtain the initially selected active instance of a multi-profile block.
 * \note      Called for multi-profile blocks only.
 * \note      Hint: since the blocks are initialized sequentially in the order they appear in the configuration array, the user can obtain the active index from
 * other, already initialized block.
 * \param[in] block_id in the range [0..(MEEM_BLOCK_COUNT-1)]
 * \return    Active instance ID
 */
EXTERN_C uint8_t MEEM_SelectInitialActiveProfile(uint8_t block_id);

#endif /* MEEM_INITCALLBACKS_H */
