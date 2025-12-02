/*!
 * \file    MEEM_BlockManagement_WearLeveling.c
 * \brief   Management routines, specific to wear-leveling blocks.
 *          Wear-leveling blocks have 1 parameter cache instance and N checksum-protected instances in the EEPROM.
 *          At startup, the mEEM finds the most recently written instance and uses it to initialize its parameter cache.
 *          On each write, only one EEPROM instance is written, but always a different one.
 * \author  Kaloyan Dimitrov
 * \copyright Copyright (c) 2025 Kaloyan Dimitrov
 *            https://github.com/kaladim
 *            SPDX-License-Identifier: MIT
 */
/******************************************************************************/
/*    Dependencies                                                            */
/******************************************************************************/
#include <assert.h>
#include <string.h>
#include "MEEM_EEAIF.h"
#include "MEEM_GenConfig.h"
#include "MEEM_Internal.h"
#include "MEEM.h"

/******************************************************************************/
/*    Macros                                                                  */
/******************************************************************************/
#define INVALID_INSTANCE 0xFFu
#define INVALID_INDEX    INVALID_INSTANCE

/******************************************************************************/
/*    Private operations                                                      */
/******************************************************************************/
static inline uint8_t MEEM_FindIndexOfMaxElement(const uint8_t array[], uint8_t array_length, uint8_t start_index, uint8_t loop_count)
{
    uint8_t max       = 0;
    uint8_t max_index = INVALID_INDEX;
    uint8_t i         = start_index;

    for (uint8_t c = 0; c < loop_count; c++)
    {
        uint8_t element = array[i];
        if ((element != INVALID_INSTANCE) && (element >= max))
        {
            max       = element;
            max_index = i;
        }

        i = MEEM_IncrementAndWrapAround(i, array_length);
    }
    return max_index;
}

/******************************************************************************/
/*    Internal operations                                                     */
/******************************************************************************/
/*!
 * \brief     Initializes a wear-leveling block.
 * \note      This is a synchronous (blocking) operation!
 * \param[in] block_id - ID of the block to initialize
 */
void MEEM_InitializeWearLevelingBlock(uint8_t block_id)
{
    const MEEM_blockConfig_t*  block_config = &MEEM_block_config[block_id];
    MEEM_blockStatusPrivate_t* block_status = &MEEM_block_status[block_id];
    uint8_t                    sequence_counters[MEEM_MAX_WL_INSTANCE_COUNT];
    uint8_t                    index_of_current_instance;
    MEEM_initStage_t           init_stage = MEEM_INIT_PREPARE;

    do
    {
        switch (init_stage)
        {
            case MEEM_INIT_PREPARE:
                index_of_current_instance = 0;
                init_stage                = MEEM_INIT_FETCH_INSTANCE;
                break;

            case MEEM_INIT_FETCH_INSTANCE:
            {
                MEEM_status_t readStatus;

                MEEM_StartReadOperation(block_id);

                MEEM_global_status.io_request.offset_in_eeprom =
                    block_config->offset_in_eeprom + (MEEM_global_status.io_request.size * index_of_current_instance);

                do
                {
                    readStatus = MEEM_ReadOperationTask();
                } while (MEEM_BUSY == readStatus);

                if (MEEM_OK == readStatus)
                {
                    init_stage = MEEM_INIT_EVALUATE_INSTANCE;
                }
                else
                {
                    init_stage = MEEM_INIT_RECOVER_DATA;
                }
            }
            break;

            case MEEM_INIT_EVALUATE_INSTANCE:
                if (MEEM_IsDataValid(block_id))
                {
                    sequence_counters[index_of_current_instance] = MEEM_work_buffer[sizeof(MEEM_checksum_t)];
                }
                else
                {
                    sequence_counters[index_of_current_instance] = INVALID_INSTANCE; /* Mark as invalid */
                }

                index_of_current_instance++;
                if (index_of_current_instance < block_config->instance_count)
                {
                    init_stage = MEEM_INIT_FETCH_INSTANCE; /* More instances to scan */
                }
                else
                {
                    /* All instances were scanned, move to analysis stage */
                    init_stage = MEEM_INIT_ANALYZE;
                }
                break;

            case MEEM_INIT_ANALYZE:
            {
                uint8_t index_of_most_recent_instance = MEEM_FindIndexOfMostRecentInstance(sequence_counters, block_config->instance_count);

                if (index_of_most_recent_instance == INVALID_INDEX)
                {
                    block_status->index_of_active_instance = 0;
                    init_stage                             = MEEM_INIT_RECOVER_DATA;
                }
                else
                {
                    block_status->index_of_active_instance = index_of_most_recent_instance;
                    init_stage                             = MEEM_INIT_CACHE;
                }
            }
            break;

            case MEEM_INIT_CACHE:
            {
                MEEM_status_t read_status;

                /* Read the last valid instance directly to the block cache */
                MEEM_StartReadOperation(block_id);

                MEEM_global_status.io_request.offset_in_eeprom =
                    sizeof(MEEM_checksum_t) + /* Since we read directly to the cache, skip the checksum */
                    block_config->offset_in_eeprom + ((sizeof(MEEM_checksum_t) + block_config->data_size) * (uint16_t) block_status->index_of_active_instance);
                MEEM_global_status.io_request.data = block_config->cache;
                MEEM_global_status.io_request.size = block_config->data_size;

                do
                {
                    read_status = MEEM_ReadOperationTask();
                } while (MEEM_BUSY == read_status);

                if (MEEM_OK == read_status)
                {
                    /* Set next instance ID and instance index for next write */
                    block_config->cache[0]                 = MEEM_IncrementAndWrapAround(sequence_counters[block_status->index_of_active_instance], 255);
                    block_status->index_of_active_instance = MEEM_IncrementAndWrapAround(block_status->index_of_active_instance, block_config->instance_count);

                    init_stage = MEEM_INIT_READY;
                }
                else
                {
                    init_stage = MEEM_INIT_RECOVER_DATA;
                }
            }
            break;

            case MEEM_INIT_RECOVER_DATA:
                block_config->cache[0]                 = 0;
                block_status->index_of_active_instance = 0;

                MEEM_RecoverBlockData(block_id);
                init_stage = MEEM_INIT_READY;
                break;

            default:
                break;
        }
    } while (MEEM_INIT_READY != init_stage);
}

uint8_t MEEM_FindIndexOfMostRecentInstance(const uint8_t sequence_counters[], uint8_t instance_count)
{
    uint8_t sequence_counter_last_valid = INVALID_INSTANCE;
    uint8_t min                         = 0xFF; // Initial min-max thresholds are intentionally inverted!
    uint8_t max                         = 0;    // Initial min-max thresholds are intentionally inverted!
    uint8_t min_index                   = INVALID_INDEX;
    uint8_t max_index                   = INVALID_INDEX;
    uint8_t rollover_start_index        = INVALID_INDEX;
    uint8_t rollover_end_index          = INVALID_INDEX;
    uint8_t i                           = 0;

    // Find min, max, rollover region.
    // Note: the loop must be executed +1 time than the instance_count, this is critically important!
    for (uint8_t c = 0; c <= instance_count; c++)
    {
        uint8_t sequence_counter_current = sequence_counters[i];
        if (sequence_counter_current != INVALID_INSTANCE)
        {
            if (sequence_counter_current < min)
            {
                min       = sequence_counter_current;
                min_index = i;
            }
            if (sequence_counter_current >= max)
            {
                max       = sequence_counter_current;
                max_index = i;
            }

            // Capture the rollover region boundaries
            if ((rollover_start_index == INVALID_INDEX) && (sequence_counter_current < sequence_counter_last_valid) &&
                ((sequence_counter_last_valid - sequence_counter_current) >= instance_count))
            {
                rollover_start_index = i;
            }
            else if ((rollover_end_index == INVALID_INDEX) && (sequence_counter_current > sequence_counter_last_valid) &&
                     ((sequence_counter_current - sequence_counter_last_valid) >= instance_count))
            {
                rollover_end_index = i;
            }

            sequence_counter_last_valid = sequence_counter_current;
        }

        i = MEEM_IncrementAndWrapAround(i, instance_count);
    }

    // Check for at least 1 valid instance:
    if ((min_index == INVALID_INDEX) || (max_index == INVALID_INDEX))
    {
        return INVALID_INDEX;
    }

    assert(sequence_counter_last_valid != INVALID_INSTANCE);

    // Check for sequence counter rollover:
    if ((max - min) >= instance_count)
    {
        // Yep, rollover:
        assert((rollover_start_index != INVALID_INDEX) && (rollover_end_index != INVALID_INDEX));

        uint8_t length = (rollover_end_index > rollover_start_index) ? (rollover_end_index - rollover_start_index)
                                                                     : (instance_count - (rollover_start_index - rollover_end_index));
        return MEEM_FindIndexOfMaxElement(sequence_counters, instance_count, rollover_start_index, length);
    }

    // No rollover, return the max
    return max_index;
}
