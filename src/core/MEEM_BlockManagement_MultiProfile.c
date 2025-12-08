/*!
 * \file    MEEM_BlockManagement_MultiProfile.c
 * \brief   Management routines, specific to multi-profile blocks.
 * Multi-profile blocks have 1 parameter cache instance and N checksum-protected instances in the EEPROM.
 * Only one EEPROM instance (profile) can be active at a time, and it's selected by MEEM_InitiateSwitchToProfile() API.
 * At startup, no active instance is selected automatically - this is left to the user.
 * On write, only the selected instance will be written in the EEPROMN.
 * \author  Kaloyan Dimitrov
 * \copyright Copyright (c) 2025 Kaloyan Dimitrov
 *            https://github.com/kaladim
 *            SPDX-License-Identifier: MIT
 */
/******************************************************************************/
/*    Dependencies                                                            */
/******************************************************************************/
#include "MEEM_Internal.h"
#include "MEEM_GenConfig.h"
#include "MEEM.h"
#include "MEEM_EEAIF.h"
#include <string.h>
#include <assert.h>

/******************************************************************************/
/*    Internal operations                                                     */
/******************************************************************************/
/*!
 * \brief    State machine of multi-profile block initialization.
 * \retval   true if init completed
 * \retval   false if init is still in progress
 */
bool MEEM_InitMultiProfileBlockTask(void)
{
    switch (MEEM_global_status.init_stage)
    {
        case MEEM_INIT_FETCH_INSTANCE:
            switch (MEEM_ReadOperationTask())
            {
                case MEEM_OK:
                    MEEM_global_status.init_stage = MEEM_INIT_EVALUATE_INSTANCE;
                    break;

                case MEEM_NOK:
                    /* Can't read the EEPROM, continue with default values */
                    MEEM_global_status.init_stage = MEEM_INIT_RECOVER_DATA;
                    break;

                default:
                    break; /* Still busy */
            }
            break;

        case MEEM_INIT_EVALUATE_INSTANCE:
            if (MEEM_IsDataValid(MEEM_global_status.block_id))
            {
                MEEM_global_status.init_stage = MEEM_INIT_CACHE;
            }
            else
            {
                MEEM_global_status.init_stage = MEEM_INIT_RECOVER_DATA;
            }
            break;

        case MEEM_INIT_CACHE:
        {
            const MEEM_blockConfig_t* block_config = &MEEM_block_config[MEEM_global_status.block_id];

            /* Just copy the content of the work buffer to data cache */
            (void) memcpy(block_config->cache, &MEEM_work_buffer[sizeof(MEEM_checksum_t)], block_config->data_size);

            MEEM_global_status.init_stage = MEEM_INIT_READY;
        }
        break;

        case MEEM_INIT_RECOVER_DATA:
            MEEM_RecoverBlockData(MEEM_global_status.block_id);
            MEEM_global_status.init_stage = MEEM_INIT_READY;
            break;

        default:
            break; /* MEEM_READY */
    }

    return (MEEM_INIT_READY == MEEM_global_status.init_stage);
}

uint8_t MEEM_GetActiveProfile(uint8_t block_id)
{
    assert(MEEM_MGMT_MULTI_PROFILE == MEEM_block_config[block_id].management_type);

    MEEM_EnterCriticalSection();
    uint8_t active_profile = MEEM_block_status[block_id].index_of_active_instance;
    MEEM_ExitCriticalSection();
    return active_profile;
}

bool MEEM_InitiateSwitchToProfile(uint8_t block_id, uint8_t target_profile_id)
{
    assert(MEEM_MGMT_MULTI_PROFILE == MEEM_block_config[block_id].management_type);
    assert(target_profile_id < MEEM_block_config[block_id].instance_count);

    MEEM_blockStatusPrivate_t* block_status = &MEEM_block_status[block_id];
    bool                       accepted     = false;

    MEEM_EnterCriticalSection();
    if (MEEM_global_status.accept_new_requests && !block_status->fetch_pending && (target_profile_id != block_status->index_of_active_instance))
    {
        block_status->index_of_active_instance = target_profile_id;
        block_status->recovered                = false;
        block_status->fetch_pending            = true;
        accepted                               = true;
    }
    MEEM_ExitCriticalSection();
    return accepted;
}

bool MEEM_IsMultiProfileBlockReady(uint8_t block_id)
{
    assert(MEEM_MGMT_MULTI_PROFILE == MEEM_block_config[block_id].management_type);
    return !MEEM_block_status[block_id].fetch_pending;
}
