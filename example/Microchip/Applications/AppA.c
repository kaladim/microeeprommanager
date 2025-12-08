#include <stdint.h>
#include "MEEM.h"

static volatile bool appA_trigger_system_shutdown = false; // Modify this one with debugger to cause system shutdown

/*!
 * \brief AppA just wants to have some or all of its parameters persisted in the EEPROM, so it uses a 'basic' block.
 * The typical use case for a 'basic' blocks is:
 * - A sudden power loss and subsequent revert to default values is acceptable
 * - The expected write frequency is low to moderate, but the EEPROM wearout should be minimized by postponing writes to the latest possible moment: see
 * #AppA_OnShutdown().
 */
void AppA_Init(void)
{
    // Now you can work with the mEEM's parameters...
}

void AppA_Task_10ms(void)
{
    static uint8_t _10ms_tick_counter = 0u;

    _10ms_tick_counter++;
    if (_10ms_tick_counter == 100u)
    {
        _10ms_tick_counter = 0ul; // 1000 ms elapsed

        // Generated API: updates the parameter cache, but not the EEPROM yet
        MEEM_Set_Block_Basic_Param_uint16(MEEM_Get_Block_Basic_Param_uint16(0) + 1u, 0);
    }
}

bool AppA_NeedsToShutdownTheSystem(void)
{
    // Modify "appA_trigger_shutdown" with a debugger or exit by another condition
    return appA_trigger_system_shutdown;
}

void AppA_OnShutdown(void)
{
    // Core API: now we trigger the actual write to EEPROM:
    MEEM_InitiateBlockWrite(MEEM_BLOCK_Block_Basic_ID);
}