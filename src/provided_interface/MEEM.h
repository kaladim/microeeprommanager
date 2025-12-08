/*!
 * \file    MEEM.h
 * \brief   Interface of the mEEM to applications.
 * \author  Kaloyan Dimitrov
 * \copyright Copyright (c) 2025 Kaloyan Dimitrov
 *            https://github.com/kaladim
 *            SPDX-License-Identifier: MIT
 */
#ifndef MEEM_H
#define MEEM_H

/******************************************************************************/
/*    Dependencies                                                            */
/******************************************************************************/
#include "MEEM_Linkage.h"
#include "MEEM_GenInterface.h"

/******************************************************************************/
/*    Types                                                                   */
/******************************************************************************/
/** Runtime status of a block */
typedef struct {
    uint8_t recovered      : 1; /**< Set once when initialization fails and the cache is populated with defaults */
    uint8_t write_complete : 1; /**< Set by the core when a write operation completes. Cleared at the actual start of the operation. */
    uint8_t write_failed   : 1; /**< Set once when a write operation fails.  */
    uint8_t write_pending  : 1; /**< Set after call to #MEEM_InitiateBlockWrite() */
    uint8_t fetch_pending  : 1; /**< Set after call to #MEEM_InitiateSwitchToProfile(). Apply to 'multi-profile' blocks only! */
    uint8_t reserved       : 3; /**< Do not use these */
} MEEM_blockStatus_t;

/******************************************************************************/
/*    Exported operations                                                     */
/******************************************************************************/
/*!
 * \brief Fetches and validates all defined blocks from the EEPROM, and populates caches.
 * \pre   Call once.
 * \note  It's a synchronous (blocking) operation! Its execution time depends entirely on the configuration!
 */
EXTERN_C void MEEM_Init(void);

/*!
 * \brief Clears all RAM areas, used by the mEEM.
 * \note  The mEEM is not operational after this call. If it needs to be resumed, MEEM_Init() must be called again.
 */
EXTERN_C void MEEM_DeInit(void);

/*!
 * \brief Processes write and profile fetch requests.
 * \pre  Must be called periodically. Although there's no restriction on the call period, the optimal one is 5~6ms.
 * \pre  When used in a multithreaded environment, ensure this function is called by one thread only!
 */
EXTERN_C void MEEM_PeriodicTask(void);

/*!
 * \brief  Starts/resumes acceptance of new write/profile fetch requests.
 */
EXTERN_C void MEEM_Resume(void);

/*!
 * \brief  Stops acceptance of new write/profile fetch requests
 * \note   Currently pending write/profile fetch requests will still be processed.
 */
EXTERN_C void MEEM_Suspend(void);

/*!
 * \brief     Triggers an asynchronous write of the block's data cache to the EEPROM.
 * \note      Write is not guaranteed to start immediately - it depends on count of waiting blocks.
 * \param[in] block_id of the block to write
 * \retval    true If the request is accepted and the write operation is scheduled
 * \retval    false If one of the following conditions is fulfilled:
 *            - #MEEM_Suspend() has already been called
 *            - This block already has a pending write request
 *            - This block already has a pending switchover request
 */
EXTERN_C bool MEEM_InitiateBlockWrite(uint8_t block_id);

/*!
 * \brief     Populates the block's data cache with default values.
 * \param[in] block_id ID of the block to restore
 */
EXTERN_C void MEEM_RestoreDefaults(uint8_t block_id);

/*!
 * \brief  Checks for an ongoing or pending write/fetch operation in any block.
 * \retval true If there are waiting or currently processed blocks
 * \retval false otherwise
 */
EXTERN_C bool MEEM_IsBusy(void);

/*!
 * \brief     Returns the current status of a block.
 * \param[in] block_id of the block to write
 * \return    Current block status
 */
EXTERN_C MEEM_blockStatus_t MEEM_GetBlockStatus(uint8_t block_id);

/*------------------------ Control API for 'multi-profile' blocks ------------------*/
/*!
 * \brief     Retrieves the index of currently active profile of a 'multi-profile' block.
 * \param[in] block_id ID of the multi-profile block.
 * \retval    [0..14] index/ID of currently active profile. The maximal value depends on configured instance count.
 */
EXTERN_C uint8_t MEEM_GetActiveProfile(uint8_t block_id);

/*!
 * \brief     Initiates switch to a profile in a 'multi-profile' block.
 * \note      Switchover is not guaranteed to start immediately - it depends on count of waiting blocks.
 * \param[in] block_id ID of the multi-profile block
 * \param[in] target_profile_id ID of the profile. The maximum value depends on configured instance count.
 * \retval    true If the request is accepted and the switchover is initiated
 * \retval    false If one of the following conditions is fulfilled:
 *            - #MEEM_Suspend() has already been called
 *            - The requested profile is same as the active one
 *            - There is already a switchover in progress
 * \post      Since the switchover is an asynchronous operation, data will not be available immediately upon exiting this function.
 *            The caller needs poll the data availability by calling #MEEM_IsMultiProfileBlockReady() until it returns true.
 *            During this time, the block's data is unusable!
 */
EXTERN_C bool MEEM_InitiateSwitchToProfile(uint8_t block_id, uint8_t target_profile_id);

/*!
 * \brief     Retrieves the progress of the switch operation, triggered by #MEEM_InitiateSwitchToProfile().
 * \param[in] block_id of the multi-profile block
 * \retval    true If the profile is already available in the block's data cache.
 * \retval    false If the profile is currently being fetched to or validated in the data cache.
 */
EXTERN_C bool MEEM_IsMultiProfileBlockReady(uint8_t block_id);

#endif /* MEEM_H */
