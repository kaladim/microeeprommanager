#include "test_base.hpp"

using ::testing::Return;

class MultiProfileBlocksTest : public TestBase
{
  public:
    MultiProfileBlocksTest()
    {
        // You can do set-up work for each test here.
    }

    ~MultiProfileBlocksTest() override
    {
        // You can do clean-up work that doesn't throw exceptions here.
    }

    // If the constructor and destructor are not enough for setting up
    // and cleaning up each test, you can define the following methods:

    void SetUp() override
    {
        TestBase::SetUp();
    }

    void TearDown() override
    {
        TestBase::TearDown();
    }
};

TEST_F(MultiProfileBlocksTest, VerifySelectInitiallyActiveProfileReturnValue)
{
    // Find a multi-profile block to test with
    const auto mp_blocks_ids = FilterBlocksByManagementType(MEEM_MGMT_MULTI_PROFILE);

    for (auto block_id : mp_blocks_ids)
    {
        ON_CALL(user_callbacks_mock, SelectInitiallyActiveProfile(block_id)).WillByDefault(Return(MEEM_block_config[block_id].instance_count - 1));
    }

    MEEM_Init(); // This will call SelectInitiallyActiveProfile

    for (auto block_id : mp_blocks_ids)
    {
        EXPECT_EQ(MEEM_GetActiveProfile(block_id), (MEEM_block_config[block_id].instance_count - 1));
    }
}

TEST_F(MultiProfileBlocksTest, SwitchProfileRequestAccepted)
{
    auto mp_blocks_ids = FilterBlocksByManagementType(MEEM_MGMT_MULTI_PROFILE);

    for (auto block_id : mp_blocks_ids)
    {
        MEEM_Init();
        MEEM_Resume();
        ProcessMeemUntilIdle();

        auto another_profile = (MEEM_GetActiveProfile(block_id) + 1) % MEEM_block_config[block_id].instance_count;
        EXPECT_TRUE(MEEM_InitiateSwitchToProfile(block_id, another_profile));
    }
}

TEST_F(MultiProfileBlocksTest, SwitchProfileRequestRejectionOnSwitchToSameProfileOrOngoingSwitch)
{
    auto mp_blocks_ids = FilterBlocksByManagementType(MEEM_MGMT_MULTI_PROFILE);

    MEEM_DeInit();
    MEEM_Init();
    ProcessMeemUntilIdle();
    MEEM_Resume();

    for (auto block_id : mp_blocks_ids)
    {
        const auto block_config = &MEEM_block_config[block_id];

        // Reject a swicth to same profile
        EXPECT_FALSE(MEEM_InitiateSwitchToProfile(block_id, MEEM_GetActiveProfile(block_id)));

        // Initiate a switch successfully
        auto another_profile = (MEEM_GetActiveProfile(block_id) + 1) % block_config->instance_count;
        EXPECT_TRUE(MEEM_InitiateSwitchToProfile(block_id, another_profile));

        // Once a swicth is in progress, reject further requests
        for (auto i = 0; i < block_config->instance_count; i++)
        {
            another_profile = (another_profile + 1) % block_config->instance_count;
            EXPECT_FALSE(MEEM_InitiateSwitchToProfile(block_id, another_profile));
        }
    }
}

TEST_F(MultiProfileBlocksTest, SwitchProfileRequestAllowedOnOngoingWrite)
{
    auto mp_blocks_ids = FilterBlocksByManagementType(MEEM_MGMT_MULTI_PROFILE);

    MEEM_DeInit();
    MEEM_Init();
    ProcessMeemUntilIdle();
    MEEM_Resume();

    for (auto block_id : mp_blocks_ids)
    {
        // Initiate a write successfully
        EXPECT_TRUE(MEEM_InitiateBlockWrite(block_id));

        // Allow a swicth if there is already an ongoing write
        auto another_profile = (MEEM_GetActiveProfile(block_id) + 1) % MEEM_block_config[block_id].instance_count;
        EXPECT_TRUE(MEEM_InitiateSwitchToProfile(block_id, another_profile));

        // Verify the write completes first:
        do
        {
            MEEM_PeriodicTask();
        } while (MEEM_GetBlockStatus(block_id).write_pending);

        EXPECT_TRUE(MEEM_GetBlockStatus(block_id).fetch_pending);
    }
}

TEST_F(MultiProfileBlocksTest, EachProfilePersistsItsData)
{
    auto mp_blocks_ids = FilterBlocksByManagementType(MEEM_MGMT_MULTI_PROFILE);

    for (auto block_id : mp_blocks_ids)
    {
        MEEM_DeInit();
        MEEM_Init();
        MEEM_Resume();
        ProcessMeemUntilIdle();

        const auto                        block_config = &MEEM_block_config[block_id];
        std::vector<std::vector<uint8_t>> profile_data_collection(block_config->instance_count);

        // Write each instance with unique data
        for (auto profile_id = 0; profile_id < block_config->instance_count; profile_id++)
        {
            MEEM_InitiateSwitchToProfile(block_id, profile_id);
            ProcessMeemUntilIdle();

            std::vector<uint8_t> profile_data(block_config->data_size);
            FillWithRandomBytes(profile_data);
            std::copy(profile_data.cbegin(), profile_data.cend(), block_config->cache);

            MEEM_InitiateBlockWrite(block_id);
            ProcessMeemUntilIdle();

            profile_data_collection[profile_id] = profile_data;
        }

        // Verify persisted data
        MEEM_DeInit();
        MEEM_Init();
        MEEM_Resume();
        ProcessMeemUntilIdle();

        for (auto profile_id = 0; profile_id < block_config->instance_count; profile_id++)
        {
            MEEM_InitiateSwitchToProfile(block_id, profile_id);
            ProcessMeemUntilIdle();
            EXPECT_TRUE(std::equal(block_config->cache, &block_config->cache[block_config->data_size], profile_data_collection[profile_id].begin()));
        }
        // std::cout << ToHexString(eep_sim->eeprom.data(), eep_sim->eeprom.size()) << std::endl;
    }
}
