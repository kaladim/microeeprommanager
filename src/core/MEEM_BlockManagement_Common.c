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
MEEM_globalStatus_t       MEEM_global_status;
MEEM_blockStatusPrivate_t MEEM_block_status[MEEM_BLOCK_COUNT];
uint8_t                   MEEM_work_buffer[MEEM_WORKBUFFER_SIZE];

/******************************************************************************/
/*    Internal operations                                                     */
/******************************************************************************/
/*!
 * \brief     Initiates a read of a block.
 * \note      Default target of the init operation is MEEM_workBuffer.
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
 * \brief   Tries to read a region of EEPROM to the work buffer.
 *          If it's not possible to read from the EEPROM, defaults will be loaded.
 * \pre     The request structure should be already prepared!
 */
MEEM_status_t MEEM_ReadOperationTask(void)
{
    switch (MEEM_global_status.io_request.stage)
    {
        case MEEM_IO_INITIATE:
            if (EEAIF_BeginRead(MEEM_global_status.io_request.offset_in_eeprom, MEEM_global_status.io_request.data, MEEM_global_status.io_request.size))
            {
                MEEM_global_status.io_request.stage = MEEM_IO_WAITING;
            }
            else
            {
                MEEM_global_status.io_request.status = MEEM_NOK;
                MEEM_global_status.io_request.stage  = MEEM_IO_COMPLETE;
                assert(false); /* Wrong time to put a request (development error)! */
            }
            break;

        case MEEM_IO_WAITING:
            EEAIF_Task();

            switch (EEAIF_GetStatus())
            {
                case EEAIF_OK:
                    MEEM_global_status.io_request.status = MEEM_OK;
                    MEEM_global_status.io_request.stage  = MEEM_IO_COMPLETE;
                    break;

                case EEAIF_NOK:
                    MEEM_global_status.io_request.status = MEEM_NOK;
                    MEEM_global_status.io_request.stage  = MEEM_IO_COMPLETE;
                    break;

                default:
                    break; /* Still busy */
            }
            break;

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

    if ((block_cfg->management_type == MEEM_MGMT_BASIC) || (block_cfg->management_type == MEEM_MGMT_BACKUP_COPY))
    {
        MEEM_global_status.io_request.offset_in_eeprom = 0;
        block_status->index_of_active_instance         = 0;
    }
#if ((MEEM_USING_BACKUP_COPY_BLOCKS == true) || (MEEM_USING_WEAR_LEVELING_BLOCKS == true))
    else
    {
        MEEM_global_status.io_request.offset_in_eeprom = (sizeof(MEEM_checksum_t) + block_cfg->data_size) * (uint16_t) block_status->index_of_active_instance;
    }
#endif

    MEEM_global_status.io_request.offset_in_eeprom += block_cfg->offset_in_eeprom;
    MEEM_global_status.io_request.size   = (block_cfg->data_size + sizeof(MEEM_checksum_t));
    MEEM_global_status.io_request.data   = MEEM_work_buffer;
    MEEM_global_status.io_request.status = MEEM_BUSY;

    MEEM_global_status.block_id    = block_id;
    MEEM_global_status.write_stage = MEEM_IO_INITIATE; /* Next stage to execute */

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
        case MEEM_IO_INITIATE:
            MEEM_CalculateAndSetChecksum();
            MEEM_WriteInitiate();
            MEEM_global_status.write_stage = MEEM_IO_WAITING;
            break;

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
 * \brief   Calculates a checksum over the data and sets in place.
 */
void MEEM_CalculateAndSetChecksum(void)
{
    /* Prepare the write image - step 2: Calculate and set the checksum */
    *((MEEM_checksum_t*) &MEEM_work_buffer[0]) =
        MEEM_CalculateChecksum(&MEEM_work_buffer[sizeof(MEEM_checksum_t)], (MEEM_global_status.io_request.size - sizeof(MEEM_checksum_t)));
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
        case MEEM_OK:
            next_stage = MEEM_IO_FINALIZE;
            break;

        case MEEM_NOK:
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
 * \brief  Execute post-write actions, specific to 'backup copy' and 'wear-leveling' blocks.
 */
MEEM_ioStage_t MEEM_WriteFinalize(void)
{
    const MEEM_blockConfig_t*  block_config = &MEEM_block_config[MEEM_global_status.block_id];
    MEEM_blockStatusPrivate_t* block_status = &MEEM_block_status[MEEM_global_status.block_id];
    MEEM_ioStage_t             next_stage   = MEEM_IO_COMPLETE;

    /* Most expected result */
    block_status->write_complete = true;

    switch (block_config->management_type)
    {
#if (MEEM_USING_BACKUP_COPY_BLOCKS == true)
        case MEEM_MGMT_BACKUP_COPY:
            block_status->index_of_active_instance++;
            if (block_status->index_of_active_instance < 2)
            {
                /* Initiate write of the backup copy */
                block_status->write_complete = false;

                MEEM_global_status.io_request.offset_in_eeprom += (block_config->data_size + sizeof(MEEM_checksum_t));
                MEEM_WriteInitiate();
                next_stage = MEEM_IO_WAITING;
            }
            break;
#endif
#if (MEEM_USING_WEAR_LEVELING_BLOCKS == true)
        case MEEM_MGMT_WEAR_LEVELING:
            /* Update the sequence counter and the active instance index */
            block_config->cache[0]                 = MEEM_IncrementAndWrapAround(block_config->cache[0], 255);
            block_status->index_of_active_instance = MEEM_IncrementAndWrapAround(block_status->index_of_active_instance, block_config->instance_count);
            break;
#endif
        default:
            break;
    }

    return next_stage;
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

    if (default_pattern_length == 0)
    {
        (void) memcpy(block_cfg->cache, block_cfg->defaults, block_cfg->data_size);
    }
    else
    {
        uint16_t offset    = (block_cfg->management_type == MEEM_MGMT_WEAR_LEVELING) ? 1u : 0u;
        uint16_t data_size = (block_cfg->data_size - offset);

        if (default_pattern_length == 1)
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
}

/*!
 * \brief     Checks the integrity of a block's data (fetched in the work buffer), by performing checksum verification.
 * \param[in] block_id - ID of the block
 * \retval    true - if data is valid
 * \retval    false - if data is not valid
 */
bool MEEM_IsDataValid(uint8_t block_id)
{
    return *((const MEEM_checksum_t*) &MEEM_work_buffer[0]) ==
           MEEM_CalculateChecksum(&MEEM_work_buffer[sizeof(MEEM_checksum_t)], MEEM_block_config[block_id].data_size);
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
