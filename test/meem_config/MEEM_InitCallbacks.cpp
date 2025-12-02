/******************************************************************************/
/*    Dependencies                                                            */
/******************************************************************************/
#include "MEEM_InitCallbacks.h"

/******************************************************************************/
/*    Required operations by the mEEM core                                    */
/******************************************************************************/
uint8_t MEEM_SelectInitialActiveProfile(uint8_t block_id)
{
    (void)block_id;
    return 0;
}

void MEEM_OnBlockInitComplete(uint8_t block_id)
{
    (void)block_id;
}
