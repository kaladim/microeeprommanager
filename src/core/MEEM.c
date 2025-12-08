/*!
 * \file    MEEM.c
 * \brief   Storage and management of parameters to/from the EEPROM.
 * \author  Kaloyan Dimitrov
 * \copyright Copyright (c) 2025 Kaloyan Dimitrov
 *            https://github.com/kaladim
 *            SPDX-License-Identifier: MIT
 */
/******************************************************************************/
/*    Dependencies                                                            */
/******************************************************************************/
#include "MEEM.h"
#include "MEEM_Internal.h"
#include <assert.h>
#include <string.h>

/******************************************************************************/
/*    Private operations prototypes                                           */
/******************************************************************************/
static bool    MEEM_ProcessCurrentRequest(void);
static void    MEEM_TryProcessNextRequest(void);
static uint8_t MEEM_GetNextBlockToProcess(void);

/******************************************************************************/
/*    Public operations                                                       */
/******************************************************************************/
void MEEM_Init(void)
{
    MEEM_ValidateConfiguration();
    EEAIF_Init();

    for (uint8_t i = 0; i < MEEM_BLOCK_COUNT; i++)
    {
        switch (MEEM_block_config[i].management_type)
        {
#if (MEEM_USING_BASIC_BLOCKS == true)
            case MEEM_MGMT_BASIC:
                MEEM_InitializeBasicBlock(i);
                break;
#endif
#if (MEEM_USING_BACKUP_COPY_BLOCKS == true)
            case MEEM_MGMT_BACKUP_COPY:
                MEEM_InitializeBackupCopyBlock(i);
                break;
#endif
#if (MEEM_USING_WEAR_LEVELING_BLOCKS == true)
            case MEEM_MGMT_WEAR_LEVELING:
                MEEM_InitializeWearLevelingBlock(i);
                break;
#endif
#if (MEEM_USING_MULTI_PROFILE_BLOCKS == true)
            case MEEM_MGMT_MULTI_PROFILE:
                MEEM_block_status[i].index_of_active_instance = MEEM_SelectInitiallyActiveProfile(i);
                MEEM_StartReadOperation(i);

                while (false == MEEM_InitMultiProfileBlockTask())
                {
                }
                break;
#endif
            default:
                break;
        }
        MEEM_OnBlockInitComplete(i);
    }

    MEEM_global_status.current_operation     = MEEM_OPR_NONE;
    MEEM_global_status.next_block_to_process = (MEEM_BLOCK_COUNT - 1u);
}

void MEEM_DeInit(void)
{
    EEAIF_DeInit();

    memset(&MEEM_global_status, 0, sizeof(MEEM_global_status));
    memset(MEEM_work_buffer, 0, sizeof(MEEM_work_buffer));

    for (uint8_t i = 0; i < MEEM_BLOCK_COUNT; i++)
    {
        memset(MEEM_block_config[i].cache, 0, MEEM_block_config[i].data_size);
        memset(&MEEM_block_status[i], 0, sizeof(MEEM_block_status));
    }
}

void MEEM_PeriodicTask(void)
{
    if (false == MEEM_ProcessCurrentRequest())
    {
        MEEM_TryProcessNextRequest();
    }
    EEAIF_Task();
}

bool MEEM_IsBusy(void)
{
    if (MEEM_OPR_NONE != MEEM_global_status.current_operation)
    {
        return true;
    }

    for (uint8_t i = 0; i < MEEM_BLOCK_COUNT; i++)
    {
        if (
#if (MEEM_USING_MULTI_PROFILE_BLOCKS == true)
            MEEM_block_status[i].fetch_pending ||
#endif
            MEEM_block_status[i].write_pending)
        {
            return true;
        }
    }
    return false;
}

void MEEM_Resume(void)
{
    MEEM_global_status.accept_new_requests = true;
}

void MEEM_Suspend(void)
{
    MEEM_global_status.accept_new_requests = false;
}

bool MEEM_InitiateBlockWrite(uint8_t block_id)
{
    assert(block_id < MEEM_BLOCK_COUNT);
    bool accepted = false;

    if (MEEM_global_status.accept_new_requests && !MEEM_block_status[block_id].write_pending && !MEEM_block_status[block_id].fetch_pending)
    {
        MEEM_block_status[block_id].write_pending  = true;
        MEEM_block_status[block_id].write_complete = false;
        accepted                                   = true;
    }
    return accepted;
}

MEEM_blockStatus_t MEEM_GetBlockStatus(uint8_t block_id)
{
    assert(block_id < MEEM_BLOCK_COUNT);
    return *((MEEM_blockStatus_t*) &MEEM_block_status[block_id]);
}

/******************************************************************************/
/*    Private operations                                                      */
/******************************************************************************/
/*!
 * \retval true if a request is currently being processed
 * \retval false no request is being processed now
 */
static bool MEEM_ProcessCurrentRequest(void)
{
    if (MEEM_OPR_WRITE == MEEM_global_status.current_operation)
    {
        if (MEEM_WriteTask())
        {
            MEEM_global_status.current_operation = MEEM_OPR_NONE;
            MEEM_OnBlockWriteComplete(MEEM_global_status.block_id);
        }
    }
#if (MEEM_USING_MULTI_PROFILE_BLOCKS == true)
    else if (MEEM_OPR_INIT == MEEM_global_status.current_operation)
    {
        if (MEEM_InitMultiProfileBlockTask())
        {
            MEEM_global_status.current_operation = MEEM_OPR_NONE;
            MEEM_OnMultiProfileBlockFetchComplete(MEEM_global_status.block_id);
        }
    }
#endif
    return (MEEM_global_status.current_operation != MEEM_OPR_NONE);
}

static void MEEM_TryProcessNextRequest(void)
{
    if (EEAIF_BUSY != EEAIF_GetStatus())
    {
        uint8_t i = MEEM_GetNextBlockToProcess();

        if (i < UINT8_MAX)
        {
            if (MEEM_block_status[i].write_pending)
            {
                MEEM_block_status[i].write_pending   = false; // Clear as early as possible to allow further write requests to be registered
                MEEM_global_status.current_operation = MEEM_OPR_WRITE;
                MEEM_StartWriteOperationCachedBlock(i);
                MEEM_OnBlockWriteStarted(i);
            }
#if (MEEM_USING_MULTI_PROFILE_BLOCKS == true)
            else if (MEEM_block_status[i].fetch_pending)
            {
                MEEM_block_status[i].fetch_pending   = false;
                MEEM_global_status.current_operation = MEEM_OPR_INIT;

                MEEM_StartReadOperation(i);
                (void) MEEM_InitMultiProfileBlockTask();
                MEEM_OnMultiProfileBlockFetchStarted(i);
            }
#endif
        }
    }
}

/*!
 * \retval UINT8_MAX - if there's no pending block to process
 * \retval [0..MEEM_BLOCK_COUNT) - index of block to process
 */
static uint8_t MEEM_GetNextBlockToProcess(void)
{
    for (uint8_t c = 0; c < MEEM_BLOCK_COUNT; c++)
    {
        // Always increment the index to ensure every block has a chance to be processed:
        MEEM_global_status.next_block_to_process = MEEM_IncrementAndWrapAround(MEEM_global_status.next_block_to_process, MEEM_BLOCK_COUNT);
        uint8_t i                                = MEEM_global_status.next_block_to_process;

        if (
#if (MEEM_USING_MULTI_PROFILE_BLOCKS == true)
            MEEM_block_status[i].fetch_pending ||
#endif
            MEEM_block_status[i].write_pending)
        {
            return i;
        }
    }

    return UINT8_MAX;
}
