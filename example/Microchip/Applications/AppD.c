#include <MEEM.h>

/*!
 * \brief AppD needs to update its EEPROM parameters frequently and immediately after change, therefore uses a 'wear-leveling' block.
 * In this scenario, the application author has to estimate the total expected amount of EEPROM data during the device lifecycle and
 * define the appropriate value for the 'instance_count' attribute in the configuration.
 * Note: while increased 'instance_count' means less EEPROM wear, it also means increased startup time!
 * The typical use case for a 'wear-leveling' blocks is:
 * - The expected write frequency is high, and data is written immediately after change.
 * - Usage of not necessarily latest data is acceptable (due to a sudden power loss)
 */
void AppD_Init(void)
{
    // AppD uses "MEEM_cache_Block_WearLeveling.states_of_mcu_output_pins" directly.
    // At this point, the data is already available in the block's cache and ready to use.
    // For example, load the output ports of the MCU with last stored states:
    // LATA = (uint8_t)MEEM_cache_Block_WearLeveling.states_of_mcu_output_pins;
    // LATB = (uint8_t)(MEEM_cache_Block_WearLeveling.states_of_mcu_output_pins >> 8);
}

void AppD_Task_10ms(void)
{
    static uint16_t timer = 0u;

    timer++;
    if (timer == (60ul * 1000ul / 10ul)) // Estimated period: 60 seconds
    {
        timer = 0u;

        // Simulate periodic change of the MCU output pins:
        MEEM_Set_Block_WearLeveling_states_of_mcu_output_pins(MEEM_Get_Block_WearLeveling_states_of_mcu_output_pins() + 0x1111);
        MEEM_InitiateBlockWrite(MEEM_BLOCK_Block_WearLeveling_ID); // Trigger a write immediately

        // Forgot something? While #MEEM_Task() is not called yet, you can still change the data:
        MEEM_Set_Block_WearLeveling_states_of_mcu_output_pins(MEEM_Get_Block_WearLeveling_states_of_mcu_output_pins() + 0x1111);
    }
}

void AppD_OnShutdown(void)
{
    // Do nothing
}