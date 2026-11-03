#include "test_base.hpp"

using testing::InSequence;

/*!
 * \brief Tests of rewrites: one write operation that writes the cache to every instance of a block.
 *        Nothing requests a rewrite yet, so the tests set its bits directly (RequestRewrite()), as the reset and the migration will.
 */
class RewriteTest : public CommittedEepromTest
{
  public:
    void InitAndResume()
    {
        MEEM_DeInit();
        MEEM_Init();
        ProcessMeemUntilIdle();
        MEEM_Resume();
    }
};

TEST_F(RewriteTest, RewriteWritesTheCacheToEveryInstance)
{
    InitAndResume();

    for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
    {
        SCOPED_TRACE("Block ID: " + std::to_string(block_id));
        const auto block_cfg     = &MEEM_block_config[block_id];
        const auto instance_size = sizeof(MEEM_checksum_t) + block_cfg->data_size;

        // Leftovers in every instance, whatever the data recovery strategy
        std::fill_n(eep_sim->eeprom.begin() + block_cfg->offset_in_eeprom, instance_size * block_cfg->instance_count, uint8_t{0x00});
        ChangeAllDataInBlock(block_id);
        auto eeprom_before_rewrite = CreateEepromSnapshot();
        eep_sim->write_log.clear();

        RequestRewrite(block_id);
        ProcessMeemUntilIdle();

        // One write job per instance, in order
        std::vector<EepromSimulator::WriteJob> expected_jobs;
        for (uint8_t instance_id = 0; instance_id < block_cfg->instance_count; instance_id++)
        {
            expected_jobs.push_back({block_cfg->offset_in_eeprom + (instance_id * instance_size), instance_size});
        }
        EXPECT_EQ(eep_sim->write_log, expected_jobs);
        EXPECT_TRUE(IsOwnAreaWrittenOnly(block_id, eeprom_before_rewrite));
        ExpectEveryInstanceHolds(block_id, SnapshotCache(block_id));

        const auto status = MEEM_GetBlockStatus(block_id);
        EXPECT_TRUE(status.write_complete);
        EXPECT_FALSE(status.write_failed);
        EXPECT_FALSE(MEEM_block_status[block_id].rewrite_all);
    }
}

TEST_F(RewriteTest, RewriteStoresTheCacheAsItIsWhenItStarts)
{
    InitAndResume();

    for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
    {
        SCOPED_TRACE("Block ID: " + std::to_string(block_id));

        ChangeAllDataInBlock(block_id);
        RequestRewrite(block_id);
        MEEM_PeriodicTask(); // Starts the rewrite: copies the cache to the work buffer
        const auto cache_at_start = SnapshotCache(block_id);

        ChangeAllDataInBlock(block_id); // The application changes the cache meanwhile
        ProcessMeemUntilIdle();

        ExpectEveryInstanceHolds(block_id, cache_at_start);
    }
}

TEST_F(RewriteTest, RewriteCallsTheWriteCallbacksOnce)
{
    InitAndResume();

    for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
    {
        SCOPED_TRACE("Block ID: " + std::to_string(block_id));
        {
            InSequence seq;
            EXPECT_CALL(user_callbacks_mock, OnBlockWriteStarted(block_id)).Times(1);
            EXPECT_CALL(user_callbacks_mock, OnBlockWriteComplete(block_id)).Times(1);
        }

        RequestRewrite(block_id);
        ProcessMeemUntilIdle();

        EXPECT_TRUE(MEEM_GetBlockStatus(block_id).write_complete);
        testing::Mock::VerifyAndClearExpectations(&user_callbacks_mock);
    }
}

TEST_F(RewriteTest, WriteRequestIsRejectedWhileTheRewriteIsPending)
{
    InitAndResume();

    for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
    {
        SCOPED_TRACE("Block ID: " + std::to_string(block_id));

        RequestRewrite(block_id);
        EXPECT_FALSE(MEEM_InitiateBlockWrite(block_id));

        ProcessMeemUntilIdle();
        EXPECT_TRUE(MEEM_InitiateBlockWrite(block_id));
        ProcessMeemUntilIdle();
    }
}

TEST_F(RewriteTest, FailedRewriteTriesEveryInstance)
{
    InitAndResume();

    for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
    {
        SCOPED_TRACE("Block ID: " + std::to_string(block_id));
        eep_sim->write_log.clear();

        RequestRewrite(block_id);
        eep_sim->return_nok_for_next_jobs(); // Simulate driver failure
        ProcessMeemUntilIdle();
        eep_sim->return_ok_for_next_jobs();

        EXPECT_EQ(eep_sim->write_log.size(), static_cast<size_t>(MEEM_block_config[block_id].instance_count));
        EXPECT_TRUE(MEEM_GetBlockStatus(block_id).write_failed);
        EXPECT_FALSE(MEEM_block_status[block_id].rewrite_all);
    }
}

TEST_F(RewriteTest, WearLevelingRewriteStartsTheSequenceOver)
{
    InitAndResume();

    for (auto block_id : FilterBlocksByManagementType(MEEM_MGMT_WEAR_LEVELING))
    {
        SCOPED_TRACE("Block ID: " + std::to_string(block_id));
        const auto block_cfg     = &MEEM_block_config[block_id];
        const auto instance_size = sizeof(MEEM_checksum_t) + block_cfg->data_size;

        // Let the sequence counter and the active instance move away from the start
        for (int i = 0; i < 3; i++)
        {
            ASSERT_TRUE(MEEM_InitiateBlockWrite(block_id));
            ProcessMeemUntilIdle();
        }

        RequestRewrite(block_id);
        ProcessMeemUntilIdle();

        // Instances 0 … n-1 hold the counters 0 … n-1, so the next write goes to instance 0 with counter n
        EXPECT_EQ(block_cfg->cache[0], block_cfg->instance_count);
        EXPECT_EQ(MEEM_block_status[block_id].index_of_active_instance, 0);

        // A restart finds the same state
        const auto cache_after_rewrite = SnapshotCache(block_id);
        MEEM_DeInit();
        MEEM_Init();
        MEEM_Resume();
        EXPECT_EQ(SnapshotCache(block_id), cache_after_rewrite);
        EXPECT_EQ(MEEM_block_status[block_id].index_of_active_instance, 0);
        EXPECT_FALSE(MEEM_GetBlockStatus(block_id).recovered);

        eep_sim->write_log.clear();
        ASSERT_TRUE(MEEM_InitiateBlockWrite(block_id));
        ProcessMeemUntilIdle();

        const std::vector<EepromSimulator::WriteJob> expected_jobs{{block_cfg->offset_in_eeprom, instance_size}};
        EXPECT_EQ(eep_sim->write_log, expected_jobs);
        EXPECT_EQ(GetInstanceFromEeprom(block_id, 0)[sizeof(MEEM_checksum_t)], block_cfg->instance_count);
    }
}

TEST_F(RewriteTest, MultiProfileRewriteKeepsTheActiveProfileAndPrecedesAPendingSwitch)
{
    InitAndResume();

    for (auto block_id : FilterBlocksByManagementType(MEEM_MGMT_MULTI_PROFILE))
    {
        SCOPED_TRACE("Block ID: " + std::to_string(block_id));
        const auto    block_cfg      = &MEEM_block_config[block_id];
        const uint8_t active_profile = block_cfg->instance_count - 1;
        const uint8_t target_profile = 0;

        if (MEEM_GetActiveProfile(block_id) != active_profile)
        {
            ASSERT_TRUE(MEEM_InitiateSwitchToProfile(block_id, active_profile));
            ProcessMeemUntilIdle();
        }

        ChangeAllDataInBlock(block_id);
        const auto content = SnapshotCache(block_id);
        RequestRewrite(block_id);
        ASSERT_TRUE(MEEM_InitiateSwitchToProfile(block_id, target_profile)) << "A profile switch is accepted while the rewrite is pending";
        {
            InSequence seq;
            EXPECT_CALL(user_callbacks_mock, OnBlockWriteStarted(block_id)).Times(1);
            EXPECT_CALL(user_callbacks_mock, OnBlockWriteComplete(block_id)).Times(1);
            EXPECT_CALL(user_callbacks_mock, OnMultiProfileBlockFetchStarted(block_id)).Times(1);
            EXPECT_CALL(user_callbacks_mock, OnMultiProfileBlockFetchComplete(block_id)).Times(1);
        }

        // The rewrite comes first and leaves the active profile alone
        do
        {
            MEEM_PeriodicTask();
            if (MEEM_GetBlockStatus(block_id).fetch_pending)
            {
                EXPECT_EQ(MEEM_GetActiveProfile(block_id), active_profile);
            }
        } while (MEEM_IsBusy());

        // Then the fetch loads the rewritten target profile
        EXPECT_EQ(MEEM_GetActiveProfile(block_id), target_profile);
        EXPECT_EQ(SnapshotCache(block_id), content);
        ExpectEveryInstanceHolds(block_id, content);
        testing::Mock::VerifyAndClearExpectations(&user_callbacks_mock);
    }
}
