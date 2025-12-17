#include <assert.h>
#include <stdint.h>
#include <MEEM.h>

/*!
 * \brief AppC uses "user profiles", which it switches runtime, therefore it needs a 'multi-profile' block.
 * The typical use case for a 'multi-profile' blocks is:
 * - A sudden power loss and subsequent revert to default values is acceptable
 * - The expected write frequency is low to moderate, but the EEPROM wearout should be minimized by postponing writes to the latest possible moment: see
 * #AppC_OnShutdown().
 * - Temporary data unavailability during switchover is acceptable
 */
static enum {
    IDLE                     = 0,
    SAVING_CURRENT_PROFILE   = 1,
    SWITCHING_TO_NEW_PROFILE = 2,
    FETCHING_NEW_PROFILE     = 3
} switchover_stage                                          = IDLE;
static volatile bool initiate_switch_of_active_user_profile = false; // Modify this with a debugger to initiate a switchover

void AppC_Init(void)
{
    // See #MEEM_SelectInitialActiveProfile() - it initializes the MEEM_cache_Block_BackupCopy.active_profile_index which we use here.
    assert(MEEM_Get_Block_BackupCopy_active_profile_index() == MEEM_GetActiveProfile(MEEM_BLOCK_Block_MultiProfile_ID));
}

static void AppC_BusinessLogic(void)
{
    static uint16_t timer = 0u;

    timer++;
    if (timer == (10u * 60000u / 10u)) // Estimated period: 10 minutes
    {
        // Update the parameters every 10 minutes:
        timer = 0u;

        // Generated API: update the parameters cache only
        MEEM_Set_Block_MultiProfile_param_0(MEEM_Get_Block_MultiProfile_param_0() + 1.1f);
        MEEM_Set_Block_MultiProfile_param_1(MEEM_Get_Block_MultiProfile_param_1() + 2.2f);
    }
}

void AppC_Task_10ms(void)
{
    switch (switchover_stage)
    {
        case IDLE:
            // Your business logic should execute only while there's no switchover in progress:
            AppC_BusinessLogic();

            // Check switch-over trigger condition:
            if (initiate_switch_of_active_user_profile)
            {
                initiate_switch_of_active_user_profile = false;

                // Saving the current instance to the EEPROM is optional.
                // Alternatively, you may jump directly to SWITCHING_TO_NEW_PROFILE and skip MEEM_InitiateBlockWrite().
                MEEM_InitiateBlockWrite(MEEM_BLOCK_Block_MultiProfile_ID);
                switchover_stage = SAVING_CURRENT_PROFILE;
            }
            break;

        case SAVING_CURRENT_PROFILE:
            // Wait for write to complete...
            if (MEEM_GetBlockStatus(MEEM_BLOCK_Block_MultiProfile_ID).write_complete)
            {
                switchover_stage = SWITCHING_TO_NEW_PROFILE;
            }
            break;

        case SWITCHING_TO_NEW_PROFILE:
        {
            uint8_t active_profile_index = MEEM_Get_Block_BackupCopy_active_profile_index();
            active_profile_index++;
            active_profile_index &= 3; // Block_MultiProfile is configured with 4 instances, so the valid indices are [0..3].
            MEEM_Set_Block_BackupCopy_active_profile_index(active_profile_index);

            if (!MEEM_InitiateSwitchToProfile(MEEM_BLOCK_Block_MultiProfile_ID, active_profile_index))
            {
                // Not possible now, check why.
            }
            switchover_stage = FETCHING_NEW_PROFILE;
        }
        break;

        case FETCHING_NEW_PROFILE:
            if (MEEM_IsMultiProfileBlockReady(MEEM_BLOCK_Block_MultiProfile_ID))
            {
                switchover_stage = IDLE;
            }
            break;

        default:
            switchover_stage = FETCHING_NEW_PROFILE;
            break;
    }
}

void AppC_OnShutdown(void)
{
    MEEM_InitiateBlockWrite(MEEM_BLOCK_Block_MultiProfile_ID);
}