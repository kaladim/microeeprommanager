/*!
 * \file    main.c
 * \brief   Footprint harness application: calls the mEEM API like a real application, so that XC8 links all of it.
 *          It is only built and measured, never run.
 */
#include <stdint.h>
#include "MEEM.h"

/** Opaque input, so that the compiler can't fold any request path away */
volatile uint8_t footprint_request;

void main(void)
{
    MEEM_Init();
    MEEM_Resume();

    for (;;)
    {
        uint8_t block_id = footprint_request;

        MEEM_PeriodicTask();

        if (block_id < MEEM_BLOCK_COUNT)
        {
            if (!MEEM_InitiateBlockWrite(block_id) && MEEM_GetBlockStatus(block_id).write_failed)
            {
                MEEM_RestoreDefaults(block_id);
            }
        }
        else if (block_id == UINT8_MAX)
        {
            MEEM_Suspend();
            while (MEEM_IsBusy())
            {
                MEEM_PeriodicTask();
            }
            MEEM_DeInit();
        }
    }
}
