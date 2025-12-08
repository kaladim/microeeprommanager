/*!
 * \file    MEEM_BlockManagement_BackupCopy.c
 * \brief   Management routines, specific to 'backup copy' blocks.
 * These blocks have 1 parameter cache instance and 2 identical checksum-protected instances in the EEPROM.
 * At startup, the mEEM finds the first valid instance and uses it to initialize its parameter cache.
 * On each write, both EEPROM instances are written.
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

/******************************************************************************/
/*    Internal operations                                                     */
/******************************************************************************/
/*!
 * \brief    Initializes a 'backup copy' block.
 * \note     This is a synchronous (blocking) operation!
 * \param[in] block_id of the block to initialize
 */
void MEEM_InitializeBackupCopyBlock(uint8_t block_id)
{
    const MEEM_blockConfig_t*  block_config = &MEEM_block_config[block_id];
    MEEM_blockStatusPrivate_t* block_status = &MEEM_block_status[block_id];
    uint8_t                    index_of_current_instance;
    uint8_t                    instance_validity_mask;
    bool                       cache_initialized;
    MEEM_initStage_t           init_stage = MEEM_INIT_PREPARE;

    do
    {
        switch (init_stage)
        {
            case MEEM_INIT_PREPARE:
                index_of_current_instance = 0;
                instance_validity_mask    = 0;
                cache_initialized         = false;
                init_stage                = MEEM_INIT_FETCH_INSTANCE;
                break;

            case MEEM_INIT_FETCH_INSTANCE:
            {
                MEEM_status_t read_status;

                MEEM_StartReadOperation(block_id);
                MEEM_global_status.io_request.offset_in_eeprom =
                    block_config->offset_in_eeprom + ((block_config->data_size + sizeof(MEEM_checksum_t)) * (uint16_t) index_of_current_instance);
                do
                {
                    read_status = MEEM_ReadOperationTask();
                } while (MEEM_BUSY == read_status);

                if (MEEM_OK == read_status)
                {
                    init_stage = MEEM_INIT_EVALUATE_INSTANCE;
                }
                else
                {
                    /* Can't read the EEPROM, continue with default values */
                    init_stage = MEEM_INIT_RECOVER_DATA;
                }
            }
            break;

            case MEEM_INIT_EVALUATE_INSTANCE:
                if (MEEM_IsDataValid(block_id))
                {
                    /* This instance is valid, it's enough to populate the cache */
                    instance_validity_mask |= (1 << index_of_current_instance);

                    if (!cache_initialized)
                    {
                        cache_initialized = true;

                        (void) memcpy(block_config->cache, &MEEM_work_buffer[sizeof(MEEM_checksum_t)], block_config->data_size);
                    }
                }

                index_of_current_instance++;
                if (index_of_current_instance < 2)
                {
                    init_stage = MEEM_INIT_FETCH_INSTANCE; /* Fetch and other instance */
                }
                else
                {
                    /* Both instances have been scanned, move to analysis stage */
                    init_stage = MEEM_INIT_ANALYZE;
                }
                break;

            case MEEM_INIT_ANALYZE:
                switch (instance_validity_mask)
                {
                    case 3:
                        /* Both valid */
                        init_stage = MEEM_INIT_READY;
                        break;
                    case 0:
                        /* Both invalid, repair. */
                        block_status->write_pending = true; /* The write procedure will update both copies. */
                        init_stage                  = MEEM_INIT_RECOVER_DATA;
                        break;
                    default:
                        /* Only one is valid, repair. */
                        block_status->write_pending = true; /* The write procedure will update both copies. */
                        init_stage                  = MEEM_INIT_READY;
                        break;
                }
                break;

            case MEEM_INIT_RECOVER_DATA:
                MEEM_RecoverBlockData(block_id);
                init_stage = MEEM_INIT_READY;
                break;

            default:
                break; /* MEEM_READY */
        }
    } while (MEEM_INIT_READY != init_stage);
}
