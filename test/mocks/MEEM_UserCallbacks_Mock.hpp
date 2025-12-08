#ifndef MEEM_USER_CALLBACKS_MOCK_HPP
#define MEEM_USER_CALLBACKS_MOCK_HPP

#include <gmock/gmock.h>
#include "MEEM_UserCallbacks.h"

/*!
 * \brief Mock class for MEEM user callbacks
 * \note  The static instance pointer is managed by the test fixture (SetUp/TearDown)
 */
class MockUserCallbacks
{
  public:
    // Static instance pointer - set by test fixture
    static MockUserCallbacks* instance;

    // Mock methods matching the callback signatures
    MOCK_METHOD(uint8_t, SelectInitiallyActiveProfile, (uint8_t block_id));
    MOCK_METHOD(void, OnBlockInitComplete, (uint8_t block_id));
    MOCK_METHOD(void, OnBlockWriteStarted, (uint8_t block_id));
    MOCK_METHOD(void, OnBlockWriteComplete, (uint8_t block_id));
    MOCK_METHOD(void, OnMultiProfileBlockFetchStarted, (uint8_t block_id));
    MOCK_METHOD(void, OnMultiProfileBlockFetchComplete, (uint8_t block_id));
};

#endif // MEEM_USER_CALLBACKS_MOCK_HPP
