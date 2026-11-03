/*!
 * \file    MEEM_BlockManagement_Common.c
 * \brief   Routines, common to all block management types.
 * \author  Kaloyan Dimitrov
 * \copyright Copyright (c) 2025 Kaloyan Dimitrov
 *            https://github.com/kaladim
 *            SPDX-License-Identifier: MIT
 */
/******************************************************************************/
/*    Dependencies                                                            */
/******************************************************************************/
#include "MEEM_EEAIF.h"
#include "MEEM_GenConfig.h"
#include "MEEM_Internal.h"
#include "MEEM.h"
#include <string.h>
#include <assert.h>

/******************************************************************************/
/*    Internal variables                                                      */
/******************************************************************************/
/* Explicitly initialized to get strong definitions instead of 'common' symbols, which AddressSanitizer can't guard reliably */
MEEM_globalStatus_t       MEEM_global_status                     = {0};
MEEM_blockStatusPrivate_t MEEM_block_status[MEEM_BLOCK_COUNT]    = {0};
uint8_t                   MEEM_work_buffer[MEEM_WORKBUFFER_SIZE] = {0};

/******************************************************************************/
/*    Internal operations                                                     */
/******************************************************************************/
/*!
 * \brief     Initiates a read of a block's instance, which the read task checks after the read.
 * \note      Default target of the init operation is MEEM_workBuffer.
 *            To read a region without the check, set MEEM_IO_INITIATE_UNCHECKED after this call.
 * \param[in] block_id - ID of the block to read
 */
void MEEM_StartReadOperation(uint8_t block_id)
{
    const MEEM_blockConfig_t* block_cfg = &MEEM_block_config[block_id];

    MEEM_global_status.block_id          = block_id;
    MEEM_global_status.init_stage        = MEEM_INIT_FETCH_INSTANCE;
    MEEM_global_status.io_request.stage  = MEEM_IO_INITIATE;
    MEEM_global_status.io_request.status = MEEM_BUSY;
    MEEM_global_status.io_request.data   = MEEM_work_buffer;
    MEEM_global_status.io_request.size   = block_cfg->data_size + sizeof(MEEM_checksum_t);

    switch (block_cfg->management_type)
    {
#if (MEEM_USING_BASIC_BLOCKS == true)
        case MEEM_MGMT_BASIC:
            MEEM_global_status.io_request.offset_in_eeprom = block_cfg->offset_in_eeprom;
            break;
#endif
#if (MEEM_USING_MULTI_PROFILE_BLOCKS == true)
        case MEEM_MGMT_MULTI_PROFILE:
        {
            MEEM_blockStatusPrivate_t* block_status = &MEEM_block_status[block_id];

            /* A read can be started only if the profile is already initialized! */
            assert(block_status->index_of_active_instance != MEEM_INVALID_PROFILE_INSTANCE);

            MEEM_global_status.io_request.offset_in_eeprom =
                block_cfg->offset_in_eeprom + ((uint16_t) block_status->index_of_active_instance * MEEM_global_status.io_request.size);
        }
        break;
#endif
        default:
            break;
    }
}

/*!
 * \brief   Tries to read a region of EEPROM, and checks the instance read into the work buffer.
 *          A read started at MEEM_IO_INITIATE_UNCHECKED skips the check.
 * \pre     The request structure should be already prepared!
 * \retval  MEEM_OK - read, and the instance is valid (or not checked)
 * \retval  MEEM_INVALID - read, but the instance failed its check
 * \retval  MEEM_NOK - the EEPROM couldn't be read
 * \retval  MEEM_BUSY - still in progress
 */
MEEM_status_t MEEM_ReadOperationTask(void)
{
    switch (MEEM_global_status.io_request.stage)
    {
        case MEEM_IO_INITIATE:
        case MEEM_IO_INITIATE_UNCHECKED:
            if (EEAIF_BeginRead(MEEM_global_status.io_request.offset_in_eeprom, MEEM_global_status.io_request.data, MEEM_global_status.io_request.size))
            {
                /* Each start stage has a waiting stage of its own, which remembers whether to check the instance */
                MEEM_global_status.io_request.stage = (MEEM_IO_INITIATE == MEEM_global_status.io_request.stage) ? MEEM_IO_WAITING : MEEM_IO_WAITING_UNCHECKED;
            }
            else
            {
                MEEM_global_status.io_request.status = MEEM_NOK;
                MEEM_global_status.io_request.stage  = MEEM_IO_COMPLETE;
                assert(false); /* Wrong time to put a request (development error)! */
            }
            break;

        case MEEM_IO_WAITING:
        case MEEM_IO_WAITING_UNCHECKED:
            EEAIF_Task();

            switch (EEAIF_GetStatus())
            {
                case EEAIF_OK:
                    if (MEEM_IO_WAITING == MEEM_global_status.io_request.stage)
                    {
                        MEEM_global_status.io_request.stage = MEEM_IO_BEGIN_CHECKSUM;
                    }
                    else
                    {
                        MEEM_global_status.io_request.status = MEEM_OK;
                        MEEM_global_status.io_request.stage  = MEEM_IO_COMPLETE;
                    }
                    break;

                case EEAIF_NOK:
                    MEEM_global_status.io_request.status = MEEM_NOK;
                    MEEM_global_status.io_request.stage  = MEEM_IO_COMPLETE;
                    break;

                default:
                    break; /* Still busy */
            }
            break;

#if (MEEM_CRC_IMPLEMENTATION == MEEM_CRC_HARDWARE)
        case MEEM_IO_BEGIN_CHECKSUM:
        case MEEM_IO_WAITING_CHECKSUM:
        {
            MEEM_checksum_t checksum;

            switch (MEEM_ChecksumTask(&MEEM_global_status.io_request.stage, &checksum))
            {
                case MEEM_OK:
                    MEEM_global_status.io_request.status = (*((const MEEM_checksum_t*) &MEEM_work_buffer[0]) == checksum) ? MEEM_OK : MEEM_INVALID;
                    MEEM_global_status.io_request.stage  = MEEM_IO_COMPLETE;
                    break;

                case MEEM_BUSY:
                    break; /* Still calculating, or the CRC unit is busy */

                default:
                    /* The CRC unit failed: the instance counts as unreadable */
                    MEEM_global_status.io_request.status = MEEM_NOK;
                    MEEM_global_status.io_request.stage  = MEEM_IO_COMPLETE;
                    break;
            }
        }
        break;
#else
        case MEEM_IO_BEGIN_CHECKSUM:
        {
            /* The built-in CRC is calculated within this stage, so there's no stage to wait for it */
            MEEM_checksum_t checksum =
                MEEM_CalculateChecksum(&MEEM_work_buffer[sizeof(MEEM_checksum_t)], (MEEM_global_status.io_request.size - sizeof(MEEM_checksum_t)));

            MEEM_global_status.io_request.status = (*((const MEEM_checksum_t*) &MEEM_work_buffer[0]) == checksum) ? MEEM_OK : MEEM_INVALID;
            MEEM_global_status.io_request.stage  = MEEM_IO_COMPLETE;
        }
        break;
#endif

        default:
            break; /* MEEM_IO_COMPLETE */
    }

    return MEEM_global_status.io_request.status;
}

/*!
 * \brief    Initiates async write of a cached block.
 * \param[in] block_id - ID of the block to write
 */
void MEEM_StartWriteOperationCachedBlock(uint8_t block_id)
{
    const MEEM_blockConfig_t*  block_cfg    = &MEEM_block_config[block_id];
    MEEM_blockStatusPrivate_t* block_status = &MEEM_block_status[block_id];

    /* Basic and 'backup copy' blocks, and a rewrite, start at the first instance */
    MEEM_global_status.io_request.offset_in_eeprom = 0;

#if ((MEEM_USING_MULTI_PROFILE_BLOCKS == true) || (MEEM_USING_WEAR_LEVELING_BLOCKS == true))
    if (block_status->rewrite_all)
    {
#if (MEEM_USING_WEAR_LEVELING_BLOCKS == true)
        /* A 'wear-leveling' block starts over with sequence counter 0, which the next instances increment. 'Multi-profile' keeps its active profile. */
        if (block_cfg->management_type == MEEM_MGMT_WEAR_LEVELING)
        {
            block_cfg->cache[0]                    = 0;
            block_status->index_of_active_instance = 0;
        }
#endif
    }
    else if ((block_cfg->management_type == MEEM_MGMT_MULTI_PROFILE) || (block_cfg->management_type == MEEM_MGMT_WEAR_LEVELING))
    {
        MEEM_global_status.io_request.offset_in_eeprom = (sizeof(MEEM_checksum_t) + block_cfg->data_size) * (uint16_t) block_status->index_of_active_instance;
    }
#endif

    /* A write of a 'backup copy' block covers both copies, and a rewrite covers every instance of any block */
    MEEM_global_status.remaining_instances =
        ((block_cfg->management_type == MEEM_MGMT_BACKUP_COPY) || block_status->rewrite_all) ? (block_cfg->instance_count - 1u) : 0u;

    MEEM_global_status.io_request.offset_in_eeprom += block_cfg->offset_in_eeprom;
    MEEM_global_status.io_request.size   = (block_cfg->data_size + sizeof(MEEM_checksum_t));
    MEEM_global_status.io_request.data   = MEEM_work_buffer;
    MEEM_global_status.io_request.status = MEEM_BUSY;

    MEEM_global_status.block_id    = block_id;
    MEEM_global_status.write_stage = MEEM_IO_BEGIN_CHECKSUM; /* Next stage to execute */

    /* First stage of write image preparation - copy block's data cache to the work buffer */
    MEEM_EnterCriticalSection();

    (void) memcpy(&MEEM_work_buffer[sizeof(MEEM_checksum_t)], block_cfg->cache, block_cfg->data_size);

    MEEM_ExitCriticalSection();
}

/*!
 * \brief    Block write main state machine.
 * \retval   true if write completed
 * \retval   false if write is in progress
 */
bool MEEM_WriteTask(void)
{
    switch (MEEM_global_status.write_stage)
    {
#if (MEEM_CRC_IMPLEMENTATION == MEEM_CRC_HARDWARE)
        case MEEM_IO_BEGIN_CHECKSUM:
        case MEEM_IO_WAITING_CHECKSUM:
        {
            MEEM_checksum_t checksum;

            switch (MEEM_ChecksumTask(&MEEM_global_status.write_stage, &checksum))
            {
                case MEEM_OK:
                    /* Prepare the write image - step 2: Set the checksum */
                    *((MEEM_checksum_t*) &MEEM_work_buffer[0]) = checksum;
                    MEEM_WriteInitiate();
                    MEEM_global_status.write_stage = MEEM_IO_WAITING;
                    break;

                case MEEM_BUSY:
                    break; /* Still calculating, or the CRC unit is busy */

                default:
                {
                    /* The CRC unit failed: skip the write, and the rest of a rewrite, which ends like a failed one */
                    MEEM_blockStatusPrivate_t* block_status = &MEEM_block_status[MEEM_global_status.block_id];

                    block_status->write_failed     = true;
                    block_status->write_complete   = true;
                    block_status->rewrite_all      = false;
                    MEEM_global_status.write_stage = MEEM_IO_COMPLETE;
                }
                break;
            }
        }
        break;
#else
        case MEEM_IO_BEGIN_CHECKSUM:
            /* Prepare the write image - step 2: Calculate and set the checksum.
             * The built-in CRC is calculated within this stage, so there's no stage to wait for it. */
            *((MEEM_checksum_t*) &MEEM_work_buffer[0]) =
                MEEM_CalculateChecksum(&MEEM_work_buffer[sizeof(MEEM_checksum_t)], (MEEM_global_status.io_request.size - sizeof(MEEM_checksum_t)));
            MEEM_WriteInitiate();
            MEEM_global_status.write_stage = MEEM_IO_WAITING;
            break;
#endif

        case MEEM_IO_WAITING:
            MEEM_global_status.write_stage = MEEM_WriteWaitToComplete();
            break;

        case MEEM_IO_FINALIZE:
            MEEM_global_status.write_stage = MEEM_WriteFinalize();
            break;

        default:
            break; /* MEEM_IO_COMPLETE */
    }

    return (MEEM_IO_COMPLETE == MEEM_global_status.write_stage);
}

/*!
 * \brief   Pushes a write request to the driver.
 */
void MEEM_WriteInitiate(void)
{
    /* Try to push a request to the driver */
    if (!EEAIF_BeginWrite(MEEM_global_status.io_request.offset_in_eeprom, MEEM_global_status.io_request.data, MEEM_global_status.io_request.size))
    {
        assert(false); /* Wrong time to put a request (development error)! */
    }
}

/*!
 * \brief   Wait the previously started write request to finish and tries to push 'compare' request.
 * \retval  MEEM_IO_WAITING - if write request failed or driver is busy
 * \retval  MEEM_IO_FINALIZE - if write request is successful
 * \retval  MEEM_IO_VERIFYING - if write request is successful and write verification is enabled
 * \retval  MEEM_IO_COMPLETE - if all write failed.
 */
MEEM_ioStage_t MEEM_WriteWaitToComplete(void)
{
    MEEM_ioStage_t next_stage;

    switch (EEAIF_GetStatus())
    {
        case EEAIF_OK:
            next_stage = MEEM_IO_FINALIZE;
            break;

        case EEAIF_NOK:
            MEEM_block_status[MEEM_global_status.block_id].write_failed = true;
            next_stage                                                  = MEEM_IO_FINALIZE;
            break;

        default:
            next_stage = MEEM_IO_WAITING;
            break;
    }
    return next_stage;
}

/*!
 * \brief  Execute post-write actions: move on to the next instance of a 'backup copy' block or of a rewrite,
 *         and update the sequence counter of a 'wear-leveling' block.
 * \retval MEEM_IO_WAITING - the same image is written to the next instance
 * \retval MEEM_IO_BEGIN_CHECKSUM - the next instance of a rewritten 'wear-leveling' block needs a checksum of its own
 * \retval MEEM_IO_COMPLETE - every instance of the write is written
 */
MEEM_ioStage_t MEEM_WriteFinalize(void)
{
    const MEEM_blockConfig_t*  block_config = &MEEM_block_config[MEEM_global_status.block_id];
    MEEM_blockStatusPrivate_t* block_status = &MEEM_block_status[MEEM_global_status.block_id];
    MEEM_ioStage_t             next_stage   = MEEM_IO_COMPLETE;

#if (MEEM_USING_WEAR_LEVELING_BLOCKS == true)
    if (block_config->management_type == MEEM_MGMT_WEAR_LEVELING)
    {
        /* Update the sequence counter and the active instance index */
        block_config->cache[0]                 = MEEM_IncrementAndWrapAround(block_config->cache[0], 255);
        block_status->index_of_active_instance = MEEM_IncrementAndWrapAround(block_status->index_of_active_instance, block_config->instance_count);
    }
#endif

    if (MEEM_global_status.remaining_instances > 0u)
    {
        MEEM_global_status.remaining_instances--;
        MEEM_global_status.io_request.offset_in_eeprom += MEEM_global_status.io_request.size;

#if (MEEM_USING_WEAR_LEVELING_BLOCKS == true)
        if (block_config->management_type == MEEM_MGMT_WEAR_LEVELING)
        {
            /* The next instance holds the next sequence counter, so it needs a checksum of its own */
            MEEM_work_buffer[sizeof(MEEM_checksum_t)] = block_config->cache[0];
            next_stage                                = MEEM_IO_BEGIN_CHECKSUM;
        }
        else
#endif
        {
            /* The same image with the same checksum */
            MEEM_WriteInitiate();
            next_stage = MEEM_IO_WAITING;
        }
    }
    else
    {
        block_status->write_complete = true;
        block_status->rewrite_all    = false;
    }

    return next_stage;
}

/*!
 * \brief     Initializes a block from the EEPROM according to its management type, as in v1.
 * \note      This is a synchronous (blocking) operation!
 * \param[in] block_id - ID of the block to initialize. The active profile of a multi-profile block is already selected.
 */
void MEEM_InitializeBlock(uint8_t block_id)
{
    switch (MEEM_block_config[block_id].management_type)
    {
#if (MEEM_USING_BASIC_BLOCKS == true)
        case MEEM_MGMT_BASIC:
            MEEM_InitializeBasicBlock(block_id);
            break;
#endif
#if (MEEM_USING_BACKUP_COPY_BLOCKS == true)
        case MEEM_MGMT_BACKUP_COPY:
            MEEM_InitializeBackupCopyBlock(block_id);
            break;
#endif
#if (MEEM_USING_WEAR_LEVELING_BLOCKS == true)
        case MEEM_MGMT_WEAR_LEVELING:
            MEEM_InitializeWearLevelingBlock(block_id);
            break;
#endif
#if (MEEM_USING_MULTI_PROFILE_BLOCKS == true)
        case MEEM_MGMT_MULTI_PROFILE:
            MEEM_InitializeMultiProfileBlock(block_id);
            break;
#endif
        default:
            break;
    }
}

/*!
 * \brief     Requests the rewrite of a block after a reset or a migration: a write that covers every instance.
 * \param[in] block_id - ID of the block to rewrite
 */
void MEEM_RequestRewrite(uint8_t block_id)
{
    MEEM_block_status[block_id].write_pending = true;
    MEEM_block_status[block_id].rewrite_all   = true;
}

/*!
 * \brief     Recover the block's data cache and/or EEPROM according to configured strategy.
 * \param[in] block_id - ID of the block to recover
 */
void MEEM_RecoverBlockData(uint8_t block_id)
{
    MEEM_dataRecoveryStrategy_t recovery_strategy = (MEEM_dataRecoveryStrategy_t) MEEM_block_config[block_id].data_recovery_strategy;
    MEEM_blockStatusPrivate_t*  block_status      = &MEEM_block_status[block_id];

    block_status->recovered = true;

    if (MEEM_RECOVER_DEFAULTS_AND_REPAIR == recovery_strategy)
    {
        block_status->write_pending = true;
    }
    MEEM_RestoreDefaults(block_id);
}

void MEEM_RestoreDefaults(uint8_t block_id)
{
    assert(block_id < MEEM_BLOCK_COUNT);

    const MEEM_blockConfig_t* block_cfg              = &MEEM_block_config[block_id];
    uint8_t                   default_pattern_length = block_cfg->default_pattern_length;

    /* Keep the sequence counter of wear-leveling blocks intact, otherwise the next written instance won't be recognized as the most recent one */
    uint16_t offset    = (block_cfg->management_type == MEEM_MGMT_WEAR_LEVELING) ? 1u : 0u;
    uint16_t data_size = (block_cfg->data_size - offset);

    if (default_pattern_length == 0)
    {
        (void) memcpy(&block_cfg->cache[offset], &block_cfg->defaults[offset], data_size);
    }
    else if (default_pattern_length == 1)
    {
        (void) memset(&block_cfg->cache[offset], block_cfg->defaults[0], data_size);
    }
    else
    {
        for (; offset < data_size; offset += (uint16_t) default_pattern_length)
        {
            (void) memcpy(&block_cfg->cache[offset], block_cfg->defaults, (size_t) default_pattern_length);
        }
    }
}

/*!
 * \brief     Increments a number by 1 and wrap around if it reaches the upper limit.
 * \param[in] number number to increment
 * \param[in] exclusive_upper_limit boundary limit, exclusive
 * \return    incremented number
 */
uint8_t MEEM_IncrementAndWrapAround(uint8_t number, uint8_t exclusive_upper_limit)
{
    number++;
    if (number >= exclusive_upper_limit)
    {
        number = 0u;
    }

    return number;
}
