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
#include "MEEM_Migration.h"
#include <assert.h>
#include <string.h>

/******************************************************************************/
/*    Private operations prototypes                                           */
/******************************************************************************/
static bool    MEEM_ProcessCurrentRequest(void);
static void    MEEM_TryProcessNextRequest(void);
static uint8_t MEEM_GetNextBlockToProcess(void);
static bool    MEEM_IsRewritePending(void);
static void    MEEM_StartCommit(void);
static uint8_t MEEM_IdentifyStoredModel(void);

/******************************************************************************/
/*    Public operations                                                       */
/******************************************************************************/
void MEEM_Init(void)
{
    MEEM_ValidateConfiguration();
    EEAIF_Init();

    uint8_t model = MEEM_IdentifyStoredModel();

    /* A reset or a migration ends with the header write after the rewrites */
    if (MEEM_MODEL_CURRENT != model)
    {
        MEEM_global_status.identity_pending = true;
    }

#if (MEEM_LEGACY_MODEL_COUNT > 0u)
    if (model < MEEM_LEGACY_MODEL_COUNT)
    {
        MEEM_MigrateFromLegacyModel(model);
    }
    else
#endif
    {
        /* Every block in definition order */
        for (uint8_t i = 0; i < MEEM_BLOCK_COUNT; i++)
        {
            MEEM_SelectInitialProfile(i);

            if (MEEM_global_status.identity_pending)
            {
                /* A reset: the defaults, and nothing is read. A rewrite stores them in every instance, whatever the recovery strategy. */
                MEEM_RecoverBlockData(i);
                MEEM_RequestRewrite(i);
            }
            else
            {
                MEEM_InitializeBlock(i);
            }
            MEEM_OnBlockInitComplete(i);
        }
    }

    MEEM_global_status.current_operation     = MEEM_OPR_NONE;
    MEEM_global_status.next_block_to_process = (MEEM_BLOCK_COUNT - 1u);
}

void MEEM_DeInit(void)
{
    EEAIF_DeInit();

    memset(&MEEM_global_status, 0, sizeof(MEEM_global_status));
    memset(MEEM_block_status, 0, sizeof(MEEM_block_status));
    memset(MEEM_work_buffer, 0, sizeof(MEEM_work_buffer));

    for (uint8_t i = 0; i < MEEM_BLOCK_COUNT; i++)
    {
        memset(MEEM_block_config[i].cache, 0, MEEM_block_config[i].data_size);
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
    /* A pending header write keeps it busy, as the rewrites before it do with write_pending */
    if ((MEEM_OPR_NONE != MEEM_global_status.current_operation) || MEEM_global_status.identity_pending)
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
    else if (MEEM_OPR_COMMIT == MEEM_global_status.current_operation)
    {
        /* Done once the driver has written the header or failed to. A failure isn't retried, since the driver retries before it reports one. */
        if (EEAIF_BUSY != EEAIF_GetStatus())
        {
            MEEM_global_status.current_operation = MEEM_OPR_NONE;
        }
    }
    return (MEEM_global_status.current_operation != MEEM_OPR_NONE);
}

static void MEEM_TryProcessNextRequest(void)
{
    if (EEAIF_BUSY != EEAIF_GetStatus())
    {
        if (MEEM_global_status.identity_pending && !MEEM_IsRewritePending())
        {
            MEEM_StartCommit();
        }
        else
        {
            uint8_t i = MEEM_GetNextBlockToProcess();

            if (i < UINT8_MAX)
            {
                if (MEEM_block_status[i].write_pending)
                {
                    MEEM_block_status[i].write_pending   = false; /* Clear as early as possible to allow further write requests to be registered */
                    MEEM_global_status.current_operation = MEEM_OPR_WRITE;
                    MEEM_StartWriteOperationCachedBlock(i);
                    MEEM_OnBlockWriteStarted(i);
                }
#if (MEEM_USING_MULTI_PROFILE_BLOCKS == true)
                else if (MEEM_block_status[i].fetch_pending)
                {
                    MEEM_EnterCriticalSection();
                    MEEM_block_status[i].index_of_active_instance = MEEM_block_status[i].index_of_requested_profile;
                    MEEM_block_status[i].fetch_pending            = false;
                    MEEM_ExitCriticalSection();

                    MEEM_global_status.current_operation = MEEM_OPR_INIT;

                    MEEM_StartReadOperation(i);
                    (void) MEEM_InitMultiProfileBlockTask();
                    MEEM_OnMultiProfileBlockFetchStarted(i);
                }
#endif
            }
        }
    }
}

/*!
 * \retval true if a block's rewrite is pending
 * \retval false otherwise
 */
static bool MEEM_IsRewritePending(void)
{
    for (uint8_t i = 0; i < MEEM_BLOCK_COUNT; i++)
    {
        if (MEEM_block_status[i].rewrite_all)
        {
            return true;
        }
    }
    return false;
}

/*!
 * \brief   Reads the stored identity header into the work buffer, without the check of an instance, and finds the data model it identifies.
 * \retval  MEEM_MODEL_CURRENT - the current header, or it couldn't be read: the blocks are initialized as in v1, and the header is left
 *          alone. The driver has already retried a failed read.
 * \retval  [0..MEEM_LEGACY_MODEL_COUNT) - the header of that legacy model: the blocks are migrated from it
 * \retval  MEEM_MODEL_UNKNOWN - any other header: blank, zeroed, torn, a v1 image or another data model. Every block is reset.
 */
static uint8_t MEEM_IdentifyStoredModel(void)
{
    MEEM_status_t read_status;

    MEEM_global_status.io_request.offset_in_eeprom = 0u;
    MEEM_global_status.io_request.data             = MEEM_work_buffer;
    MEEM_global_status.io_request.size             = sizeof(MEEM_identity_header);
    MEEM_global_status.io_request.stage            = MEEM_IO_INITIATE_UNCHECKED;
    MEEM_global_status.io_request.status           = MEEM_BUSY;

    do
    {
        read_status = MEEM_ReadOperationTask();
    } while (MEEM_BUSY == read_status);

    if (MEEM_OK == read_status)
    {
        /* A loop, since memcmp() would add library code */
        for (uint8_t i = 0; i < sizeof(MEEM_identity_header); i++)
        {
            if (MEEM_work_buffer[i] != MEEM_identity_header[i])
            {
#if (MEEM_LEGACY_MODEL_COUNT > 0u)
                return MEEM_FindLegacyModel();
#else
                return MEEM_MODEL_UNKNOWN;
#endif
            }
        }
    }
    return MEEM_MODEL_CURRENT;
}

/*!
 * \brief Starts the write of the identity header, which commits a reset or a migration. It comes after every rewrite, even a failed one.
 */
static void MEEM_StartCommit(void)
{
    MEEM_global_status.identity_pending  = false; /* Cleared now, so that a failed write isn't repeated */
    MEEM_global_status.current_operation = MEEM_OPR_COMMIT;

    (void) memcpy(MEEM_work_buffer, MEEM_identity_header, sizeof(MEEM_identity_header));

    if (!EEAIF_BeginWrite(0u, MEEM_work_buffer, sizeof(MEEM_identity_header)))
    {
        assert(false); /* Wrong time to put a request (development error)! */
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
        /* Always increment the index to ensure every block has a chance to be processed: */
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
