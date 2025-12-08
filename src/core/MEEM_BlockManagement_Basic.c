/*!
 * \file    MEEM_BlockManagement_Basic.c
 * \brief   Management routines, specific to basic blocks.
 *          Basic blocks have 1 parameter cache instance and 1 checksum-protected instance in the EEPROM.
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
 * \note      This is a synchronous (blocking) operation!
 * \param[in] block_id - ID of the block to initialize
 */
void MEEM_InitializeBasicBlock(uint8_t block_id)
{
    const MEEM_blockConfig_t* block_cfg  = &MEEM_block_config[block_id];
    MEEM_initStage_t          init_stage = MEEM_INIT_FETCH_INSTANCE;

    MEEM_StartReadOperation(block_id);

    do
    {
        switch (init_stage)
        {
            case MEEM_INIT_FETCH_INSTANCE:
                switch (MEEM_ReadOperationTask())
                {
                    case MEEM_OK:
                        init_stage = MEEM_INIT_EVALUATE_INSTANCE;
                        break;
                    case MEEM_NOK:
                        /* Can't read the EEPROM, continue with default values */
                        init_stage = MEEM_INIT_RECOVER_DATA;
                        break;
                    default:
                        break; /* Still busy */
                }
                break;

            case MEEM_INIT_EVALUATE_INSTANCE:
                if (MEEM_IsDataValid(block_id))
                {
                    init_stage = MEEM_INIT_CACHE;
                }
                else
                {
                    init_stage = MEEM_INIT_RECOVER_DATA;
                }
                break;

            case MEEM_INIT_CACHE:
                /* Just copy the content of the work buffer to data cache */
                (void) memcpy(block_cfg->cache, &MEEM_work_buffer[sizeof(MEEM_checksum_t)], block_cfg->data_size);
                init_stage = MEEM_INIT_READY;
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
