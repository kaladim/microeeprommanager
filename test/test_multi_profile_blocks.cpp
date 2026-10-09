#include "test_base.hpp"
#include <cstring>

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

    /// @brief Persists random data in a profile and leaves it active.
    /// @return the persisted data
    std::vector<uint8_t> StoreRandomDataToProfile(uint8_t block_id, uint8_t profile_id)
    {
        const auto block_config = &MEEM_block_config[block_id];

        if (MEEM_GetActiveProfile(block_id) != profile_id)
        {
            EXPECT_TRUE(MEEM_InitiateSwitchToProfile(block_id, profile_id));
            ProcessMeemUntilIdle();
        }

        const auto data = GenerateRandomBytes(block_config->data_size);
        std::copy(data.cbegin(), data.cend(), block_config->cache);
        EXPECT_TRUE(MEEM_InitiateBlockWrite(block_id));
        ProcessMeemUntilIdle();
        return data;
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

TEST_F(MultiProfileBlocksTest, WriteChangesOnlyTheInstanceOfTheActiveProfile)
{
    auto mp_blocks_ids = FilterBlocksByManagementType(MEEM_MGMT_MULTI_PROFILE);

    MEEM_DeInit();
    MEEM_Init();
    MEEM_Resume();
    ProcessMeemUntilIdle();

    for (auto block_id : mp_blocks_ids)
    {
        const auto block_config  = &MEEM_block_config[block_id];
        const auto instance_size = sizeof(MEEM_checksum_t) + block_config->data_size;

        for (uint8_t profile_id = 0; profile_id < block_config->instance_count; profile_id++)
        {
            SCOPED_TRACE("Block ID: " + std::to_string(block_id) + ", Profile ID: " + std::to_string(profile_id));

            if (MEEM_GetActiveProfile(block_id) != profile_id)
            {
                ASSERT_TRUE(MEEM_InitiateSwitchToProfile(block_id, profile_id));
                ProcessMeemUntilIdle();
            }

            ChangeAllDataInBlock(block_id);
            auto expected_eeprom = CreateEepromSnapshot();

            ASSERT_TRUE(MEEM_InitiateBlockWrite(block_id));
            ProcessMeemUntilIdle();

            // Only the profile's instance is expected to change, to [checksum][data]
            const auto instance_offset = block_config->offset_in_eeprom + (profile_id * instance_size);
            const auto checksum        = MEEM_CalculateChecksum(block_config->cache, block_config->data_size);
            std::memcpy(&expected_eeprom[instance_offset], &checksum, sizeof(checksum));
            std::copy(block_config->cache, &block_config->cache[block_config->data_size], expected_eeprom.begin() + instance_offset + sizeof(MEEM_checksum_t));

            const auto first_difference = std::mismatch(expected_eeprom.cbegin(), expected_eeprom.cend(), eep_sim->eeprom.cbegin()).first;
            EXPECT_TRUE(expected_eeprom == eep_sim->eeprom) << "First difference at EEPROM offset " << (first_difference - expected_eeprom.cbegin());
        }
    }
}

TEST_F(MultiProfileBlocksTest, WriteRequestedBeforeSwitchGoesToThePreviouslyActiveProfile)
{
    auto mp_blocks_ids = FilterBlocksByManagementType(MEEM_MGMT_MULTI_PROFILE);

    MEEM_DeInit();
    MEEM_Init();
    MEEM_Resume();
    ProcessMeemUntilIdle();

    for (auto block_id : mp_blocks_ids)
    {
        SCOPED_TRACE("Block ID: " + std::to_string(block_id));

        const auto    block_config = &MEEM_block_config[block_id];
        const uint8_t from_profile = MEEM_GetActiveProfile(block_id);
        const uint8_t to_profile   = (from_profile + 1) % block_config->instance_count;

        const auto to_profile_data = StoreRandomDataToProfile(block_id, to_profile);
        ASSERT_TRUE(MEEM_InitiateSwitchToProfile(block_id, from_profile));
        ProcessMeemUntilIdle();

        // Request a write, immediately followed by a switch
        const auto from_profile_data = GenerateRandomBytes(block_config->data_size);
        std::copy(from_profile_data.cbegin(), from_profile_data.cend(), block_config->cache);
        ASSERT_TRUE(MEEM_InitiateBlockWrite(block_id));
        ASSERT_TRUE(MEEM_InitiateSwitchToProfile(block_id, to_profile));
        EXPECT_EQ(from_profile, MEEM_GetActiveProfile(block_id)) << "The switch must not take effect before the pending write";
        ProcessMeemUntilIdle();

        // The target profile is intact...
        EXPECT_EQ(to_profile, MEEM_GetActiveProfile(block_id));
        EXPECT_TRUE(std::equal(to_profile_data.cbegin(), to_profile_data.cend(), block_config->cache));

        // ...and the write went to the profile it was requested for
        ASSERT_TRUE(MEEM_InitiateSwitchToProfile(block_id, from_profile));
        ProcessMeemUntilIdle();
        EXPECT_TRUE(std::equal(from_profile_data.cbegin(), from_profile_data.cend(), block_config->cache));
    }
}

TEST_F(MultiProfileBlocksTest, BlockIsReadyOnlyWhenTheProfileIsFetched)
{
    auto mp_blocks_ids = FilterBlocksByManagementType(MEEM_MGMT_MULTI_PROFILE);

    MEEM_DeInit();
    MEEM_Init();
    MEEM_Resume();
    ProcessMeemUntilIdle();

    for (auto block_id : mp_blocks_ids)
    {
        SCOPED_TRACE("Block ID: " + std::to_string(block_id));

        const auto    block_config    = &MEEM_block_config[block_id];
        const uint8_t from_profile    = MEEM_GetActiveProfile(block_id);
        const uint8_t to_profile      = (from_profile + 1) % block_config->instance_count;
        const auto    to_profile_data = StoreRandomDataToProfile(block_id, to_profile);

        ASSERT_TRUE(MEEM_InitiateSwitchToProfile(block_id, from_profile));
        ProcessMeemUntilIdle();

        // Poll the readiness, as an application would do
        ASSERT_TRUE(MEEM_InitiateSwitchToProfile(block_id, to_profile));
        for (int i = 0; (i < 100) && !MEEM_IsMultiProfileBlockReady(block_id); i++)
        {
            MEEM_PeriodicTask();
        }

        ASSERT_TRUE(MEEM_IsMultiProfileBlockReady(block_id));
        EXPECT_TRUE(std::equal(to_profile_data.cbegin(), to_profile_data.cend(), block_config->cache)) << "Reported ready before the profile was fetched";
        ProcessMeemUntilIdle();
    }
}
