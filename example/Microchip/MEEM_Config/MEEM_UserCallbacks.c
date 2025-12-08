/******************************************************************************/
/*    Dependencies                                                            */
/******************************************************************************/
#include <assert.h>
#include "MEEM_UserCallbacks.h"
#include "MEEM.h"

/******************************************************************************/
/*    Required operations by the mEEM core                                    */
/******************************************************************************/
uint8_t MEEM_SelectInitiallyActiveProfile(uint8_t block_id)
{
    (void) block_id; // There is just one 'multi-profile' block in this configuration, so we don't need the block_id

    // We use 'Block_BackupCopy' to store the active index of 'Block_MultiProfile', so on the next power cycle we use the same instance.
    // And, we're sure the 'backup copy' block is initialized before the block which uses its data:
    assert(MEEM_BLOCK_Block_BackupCopy_ID < MEEM_BLOCK_Block_MultiProfile_ID);
    return MEEM_cache_Block_BackupCopy.active_profile_index;
}

void MEEM_OnBlockInitComplete(uint8_t block_id)
{
    // Here you may check if some other block already completed its initialization and take some actions.
    (void) block_id;
}

void MEEM_OnBlockWriteStarted(uint8_t block_id)
{
    (void) block_id;
}

void MEEM_OnBlockWriteComplete(uint8_t block_id)
{
    (void) block_id;
}

void MEEM_OnMultiProfileBlockFetchStarted(uint8_t block_id)
{
    (void) block_id;
}

void MEEM_OnMultiProfileBlockFetchComplete(uint8_t block_id)
{
    (void) block_id;
}