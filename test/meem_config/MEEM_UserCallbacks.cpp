/******************************************************************************/
/*    Dependencies                                                            */
/******************************************************************************/
#include "MEEM_UserCallbacks.h"
#include "MEEM_UserCallbacks_Mock.hpp"

// Define the static instance pointer (initialized to nullptr by default)
MockUserCallbacks* MockUserCallbacks::instance = nullptr;

/******************************************************************************/
/*    Required operations by the mEEM core                                    */
/******************************************************************************/
uint8_t MEEM_SelectInitiallyActiveProfile(uint8_t block_id)
{
    if (MockUserCallbacks::instance)
    {
        return MockUserCallbacks::instance->SelectInitiallyActiveProfile(block_id);
    }
    return 0;
}

void MEEM_OnBlockInitComplete(uint8_t block_id)
{
    if (MockUserCallbacks::instance)
    {
        MockUserCallbacks::instance->OnBlockInitComplete(block_id);
    }
}

void MEEM_OnBlockWriteStarted(uint8_t block_id)
{
    if (MockUserCallbacks::instance)
    {
        MockUserCallbacks::instance->OnBlockWriteStarted(block_id);
    }
}

void MEEM_OnBlockWriteComplete(uint8_t block_id)
{
    if (MockUserCallbacks::instance)
    {
        MockUserCallbacks::instance->OnBlockWriteComplete(block_id);
    }
}

void MEEM_OnMultiProfileBlockFetchStarted(uint8_t block_id)
{
    if (MockUserCallbacks::instance)
    {
        MockUserCallbacks::instance->OnMultiProfileBlockFetchStarted(block_id);
    }
}

void MEEM_OnMultiProfileBlockFetchComplete(uint8_t block_id)
{
    if (MockUserCallbacks::instance)
    {
        MockUserCallbacks::instance->OnMultiProfileBlockFetchComplete(block_id);
    }
}