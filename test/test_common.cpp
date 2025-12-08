#include "test_base.hpp"
#include <algorithm>
#include <numeric>

using testing::InSequence;

class TestCommon : public TestBase
{
  public:
    TestCommon() {}

    ~TestCommon() override {}

    // If the constructor and destructor are not enough for setting up
    // and cleaning up each test, you can define the following methods:

    void SetUp() override
    {
        TestBase::SetUp(); // Important: Call base class SetUp() first
    }

    void TearDown() override
    {
        TestBase::TearDown(); // Important: Call base class TearDown()
    }
};

TEST_F(TestCommon, InitFromBlankEeprom)
{
    eep_sim->erase();
    MEEM_DeInit();

    for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
    {
        EXPECT_CALL(user_callbacks_mock, OnBlockInitComplete(block_id)).Times(1);
    }

    MEEM_Init();

    for (int block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
    {
        EXPECT_TRUE(MEEM_GetBlockStatus(block_id).recovered);
    }

    ProcessMeemUntilIdle();
}

TEST_F(TestCommon, InitFromPreviouslyValidEeprom)
{
    MEEM_DeInit();
    MEEM_Init();
    MEEM_Resume();

    // Ensure all blocks are reset to default values and persisted in EEPROM
    for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
    {
        MEEM_RestoreDefaults(block_id);
        MEEM_InitiateBlockWrite(block_id);
    }
    ProcessMeemUntilIdle();

    MEEM_DeInit();
    MEEM_Init();

    for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
    {
        EXPECT_FALSE(MEEM_GetBlockStatus(block_id).recovered);
    }
}

TEST_F(TestCommon, EnsureProcessingStartsAlwaysFromBlock0)
{
    MEEM_DeInit();
    MEEM_Init();
    ProcessMeemUntilIdle();
    MEEM_Resume();

    // Initiate writes for all blocks in reverse order
    for (uint8_t block_id = (MEEM_BLOCK_COUNT - 1); block_id != std::numeric_limits<uint8_t>::max(); block_id--)
    {
        EXPECT_TRUE(MEEM_InitiateBlockWrite(block_id));
        EXPECT_TRUE(MEEM_GetBlockStatus(block_id).write_pending);
    }

    {
        InSequence seq;
        EXPECT_CALL(user_callbacks_mock, OnBlockWriteStarted(0)).Times(1);
        EXPECT_CALL(user_callbacks_mock, OnBlockWriteComplete(0)).Times(1);
    }
    MEEM_PeriodicTask(); // First call of the task should trigger processing of block #0

    {
        InSequence seq;
        for (uint8_t block_id = 1; block_id < MEEM_BLOCK_COUNT; block_id++)
        {
            EXPECT_CALL(user_callbacks_mock, OnBlockWriteStarted(block_id)).Times(1);
            EXPECT_CALL(user_callbacks_mock, OnBlockWriteComplete(block_id)).Times(1);
        }
    }
    ProcessMeemUntilIdle();
}

TEST_F(TestCommon, AcceptWriteAndProfileFetchRequestsOnlyIfResumed)
{
    MEEM_DeInit();
    MEEM_Init();

    for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
    {
        EXPECT_FALSE(MEEM_InitiateBlockWrite(block_id));

        if (MEEM_block_config[block_id].management_type == MEEM_MGMT_MULTI_PROFILE)
        {
            EXPECT_FALSE(MEEM_InitiateSwitchToProfile(block_id, MEEM_GetActiveProfile(block_id) + 1));
        }
    }
}

TEST_F(TestCommon, WriteOnUserRequestAndVerifyOnlyOwnBlockAreaIsChanged)
{
    MEEM_DeInit();
    MEEM_Init();
    ProcessMeemUntilIdle();
    MEEM_Resume();

    for (int block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
    {
        auto eeprom_before_change = CreateEepromSnapshot();

        ChangeAllDataInBlock(block_id);
        MEEM_InitiateBlockWrite(block_id);
        ProcessMeemUntilIdle();
        EXPECT_TRUE(IsOwnAreaWrittenOnly(block_id, eeprom_before_change));
    }
}

TEST_F(TestCommon, EepromDriverFailureWhenWriting)
{
    MEEM_DeInit();
    MEEM_Init();
    ProcessMeemUntilIdle();
    MEEM_Resume();

    for (int block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
    {
        MEEM_InitiateBlockWrite(block_id);
    }

    eep_sim->return_nok_for_next_jobs(); // Simulate driver failure
    ProcessMeemUntilIdle();
    eep_sim->return_ok_for_next_jobs();

    for (int block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
    {
        EXPECT_TRUE(MEEM_GetBlockStatus(block_id).write_failed);
    }
}

TEST_F(TestCommon, EnsureEachBlockWillBeProcessedEvenOnHighLoad)
{
    constexpr uint16_t REQUESTS_PER_BLOCK{MEEM_BLOCK_COUNT * 3};

    std::array<uint16_t, MEEM_BLOCK_COUNT> write_requests_made{};          // Successful write requests made per block
    std::array<uint16_t, MEEM_BLOCK_COUNT> profile_switch_requests_made{}; // Successful profile switch requests made per block
    std::array<bool, MEEM_BLOCK_COUNT>     is_multi_profile{};

    for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
    {
        is_multi_profile[block_id] = (MEEM_block_config[block_id].management_type == MEEM_MGMT_MULTI_PROFILE);
    }

    MEEM_DeInit();
    MEEM_Init();
    ProcessMeemUntilIdle();
    MEEM_Resume();

    // Phase 1: Generate all requests - flood the MEEM with exactly REQUESTS_PER_BLOCK
    bool more_requests_needed = true;
    while (more_requests_needed)
    {
        more_requests_needed = false;

        for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
        {
            // Make write requests until we reach REQUESTS_PER_BLOCK for this block
            if (write_requests_made[block_id] < REQUESTS_PER_BLOCK)
            {
                ChangeAllDataInBlock(block_id);
                if (MEEM_InitiateBlockWrite(block_id))
                {
                    write_requests_made[block_id]++;
                }
                // If rejected, we keep trying until the request is accepted
            }

            // While there are write requests, try to push profile switch requests for multi-profile blocks
            // Profile switches must wait for completion before initiating next one
            if (is_multi_profile[block_id])
            {
                // Check if previous profile switch is complete (or if this is the first request)
                if (MEEM_IsMultiProfileBlockReady(block_id))
                {
                    uint8_t current_profile = MEEM_GetActiveProfile(block_id);
                    uint8_t next_profile    = (current_profile + 1) % MEEM_block_config[block_id].instance_count;

                    if (MEEM_InitiateSwitchToProfile(block_id, next_profile))
                    {
                        profile_switch_requests_made[block_id]++;
                    }
                }
            }

            // Check if any block still needs write requests
            more_requests_needed = (write_requests_made[block_id] < REQUESTS_PER_BLOCK);
        }

        // Process MEEM tasks to handle accepted requests
        MEEM_PeriodicTask();
    }

    // Phase 2: Process all remaining operations until system is idle
    ProcessMeemUntilIdle();

    // Phase 3: Verify completion by checking status flags
    for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
    {
        // Check that write operations completed successfully
        MEEM_blockStatus_t status = MEEM_GetBlockStatus(block_id);
        EXPECT_FALSE(status.write_failed) << "Block #" << static_cast<int>(block_id) << " has write_failed flag set";
        EXPECT_FALSE(status.write_pending) << "Block #" << static_cast<int>(block_id) << " still has write_pending flag set";

        // For multi-profile blocks, check that profile operations completed
        if (is_multi_profile[block_id])
        {
            EXPECT_FALSE(status.fetch_pending) << "Multi-profile block #" << static_cast<int>(block_id) << " still has fetch_pending flag set";
            EXPECT_TRUE(MEEM_IsMultiProfileBlockReady(block_id)) << "Multi-profile block #" << static_cast<int>(block_id) << " is not ready";
        }
    }

    // Verification: at least 1 request per block should have been accepted and processed
    for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
    {
        SCOPED_TRACE("Block ID: " + std::to_string(block_id) + ", Management Type: " + std::to_string(MEEM_block_config[block_id].management_type));

        EXPECT_GE(write_requests_made[block_id], 1) << "Block #" << static_cast<int>(block_id)
                                                    << " should have made at least 1 write requests, but actually made " << write_requests_made[block_id];

        if (is_multi_profile[block_id])
        {
            // Verify profile switch operations for multi-profile blocks.
            EXPECT_GE(profile_switch_requests_made[block_id], 1)
                << "Multi-profile block " << static_cast<int>(block_id) << " should have made at least 1 profile switch requests, but made "
                << profile_switch_requests_made[block_id];
        }
    }

    // Verify the MEEM is completely idle after processing all requests
    EXPECT_FALSE(MEEM_IsBusy()) << "MEEM should not be busy after processing all " << REQUESTS_PER_BLOCK << " requests per block";

    // Verify total system throughput
    uint16_t total_write_operations     = std::accumulate(write_requests_made.begin(), write_requests_made.end(), 0);
    uint16_t total_fetch_operations     = std::accumulate(profile_switch_requests_made.begin(), profile_switch_requests_made.end(), 0);
    uint16_t total_multi_profile_blocks = std::accumulate(is_multi_profile.begin(), is_multi_profile.end(), 0);

    EXPECT_GE(total_write_operations, MEEM_BLOCK_COUNT)
        << "Total write operations should be at least " << MEEM_BLOCK_COUNT << ", but was " << total_write_operations;
    EXPECT_GE(total_fetch_operations, total_multi_profile_blocks)
        << "Total profile switch operations should be at least " << total_multi_profile_blocks << " but was " << total_fetch_operations;
}

TEST_F(TestCommon, DISABLED_CreateExampleEepromImage)
{
    MEEM_Init();
    MEEM_Resume();

    for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
    {
        const auto block_cfg = &MEEM_block_config[block_id];

        switch (block_cfg->management_type)
        {
            case MEEM_MGMT_MULTI_PROFILE:
                for (uint8_t instance = 0; instance < block_cfg->instance_count; instance++)
                {
                    MEEM_InitiateSwitchToProfile(block_id, (MEEM_GetActiveProfile(block_id) + 1) % block_cfg->instance_count);
                    ProcessMeemUntilIdle();

                    ChangeAllDataInBlock(block_id);
                    MEEM_InitiateBlockWrite(block_id);
                    ProcessMeemUntilIdle();
                }
                break;

            case MEEM_MGMT_WEAR_LEVELING:
                for (uint8_t instance = 0; instance < block_cfg->instance_count; instance++)
                {
                    MEEM_InitiateBlockWrite(block_id);
                    ProcessMeemUntilIdle();
                    ChangeAllDataInBlock(block_id);
                }
                break;

            default:
                MEEM_InitiateBlockWrite(block_id);
                ProcessMeemUntilIdle();
                ChangeAllDataInBlock(block_id);
                break;
        }
    }

    // By default, it will be stored in the same directory as the test executable
    eep_sim->store();
}
