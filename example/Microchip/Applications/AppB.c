#include <stdint.h>
#include <MEEM.h>

/*!
 * \brief AppB works with highly reliable data and expects sudden power loss at any moment, therefore it needs a 'backup copy' block to store its parameters.
 * The typical use case for a 'backup copy' blocks is:
 * - A sudden power loss is expected at any moment
 * - Revert to default values should be minimized and ideally avoided
 * - The expected write frequency is low
 */
void AppB_Init(void)
{
    if (MEEM_GetBlockStatus(MEEM_BLOCK_Block_BackupCopy_ID).recovered)
    {
        // This should be a rare and exceptional case, handle it here
    }
}

void AppB_Task_10ms(void)
{
    static uint32_t timer = (3ul * 3600ul * 1000ul / 10ul) + 1ul; // Period= 3 hours

    // Perform 1 write when the timer elapses.
    switch (timer)
    {
        case 0ul:
            break; // Stopped

        case 1ul:
            // Expired
            timer = 0ul; // Stop it
            MEEM_Set_Block_BackupCopy_timestamp(MEEM_Get_Block_BackupCopy_timestamp() + 0x11111111ul);
            MEEM_InitiateBlockWrite(MEEM_BLOCK_Block_BackupCopy_ID); // Trigger write immediately here
            break;

        default:
            timer--; // Running
            break;
    }
}

void AppB_OnShutdown(void)
{
    // Something else, but not a EEPROM write.
}
