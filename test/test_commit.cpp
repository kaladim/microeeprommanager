#include "test_base.hpp"

/*!
 * \brief Tests of the commit: once no rewrite is pending, the periodic task writes the identity header and clears identity_pending.
 *        Nothing requests a commit yet, so the tests set its bit directly (RequestCommit()), as the reset and the migration will.
 */
class CommitTest : public CommittedEepromTest
{
  public:
    void SetUp() override
    {
        CommittedEepromTest::SetUp(); // Important: Call base class SetUp() first

        MEEM_DeInit();
        MEEM_Init();
        ProcessMeemUntilIdle();
        MEEM_Resume();

        // A header other than the current one, so that its write can be seen
        std::fill_n(eep_sim->eeprom.begin(), sizeof(MEEM_identity_header), uint8_t{0x00});
        eep_sim->write_log.clear();
    }

    size_t CountHeaderWrites()
    {
        return static_cast<size_t>(std::count(eep_sim->write_log.cbegin(), eep_sim->write_log.cend(), HeaderWriteJob()));
    }
};

TEST_F(CommitTest, HeaderIsTheLastWrite)
{
    size_t instance_count = 0;

    for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
    {
        RequestRewrite(block_id);
        instance_count += MEEM_block_config[block_id].instance_count;
    }
    RequestCommit();
    ProcessMeemUntilIdle();

    ASSERT_EQ(eep_sim->write_log.size(), instance_count + 1);
    EXPECT_EQ(eep_sim->write_log.back(), HeaderWriteJob());
    EXPECT_EQ(CountHeaderWrites(), 1u);
    EXPECT_EQ(GetHeaderFromEeprom(), CurrentHeader());
    EXPECT_FALSE(MEEM_global_status.identity_pending);

    for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
    {
        EXPECT_TRUE(MEEM_GetBlockStatus(block_id).write_complete) << "Block #" << static_cast<int>(block_id);
    }
}

TEST_F(CommitTest, BusyUntilTheHeaderIsWritten)
{
    RequestCommit();
    EXPECT_TRUE(MEEM_IsBusy()) << "The header write is pending";

    MEEM_PeriodicTask(); // Starts the header write
    EXPECT_FALSE(MEEM_global_status.identity_pending);
    EXPECT_TRUE(MEEM_IsBusy()) << "The header is being written";

    ProcessMeemUntilIdle();

    const std::vector<EepromSimulator::WriteJob> expected_jobs{HeaderWriteJob()};
    EXPECT_EQ(eep_sim->write_log, expected_jobs);
    EXPECT_EQ(GetHeaderFromEeprom(), CurrentHeader());
}

TEST_F(CommitTest, OnlyRewritesHoldTheHeaderBack)
{
    // A write that isn't a rewrite, such as a v1 repair of an unchanged block after a migration
    ASSERT_TRUE(MEEM_InitiateBlockWrite(0));
    RequestCommit();
    ProcessMeemUntilIdle();

    ASSERT_FALSE(eep_sim->write_log.empty());
    EXPECT_EQ(eep_sim->write_log.front(), HeaderWriteJob());
    EXPECT_TRUE(MEEM_GetBlockStatus(0).write_complete);
}

TEST_F(CommitTest, FailedRewriteDoesntHoldTheHeaderBack)
{
    for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
    {
        SCOPED_TRACE("Block ID: " + std::to_string(block_id));
        std::fill_n(eep_sim->eeprom.begin(), sizeof(MEEM_identity_header), uint8_t{0x00});
        eep_sim->write_log.clear();

        RequestRewrite(block_id);
        RequestCommit();

        // Every instance of the rewrite fails, the header write doesn't
        eep_sim->return_nok_for_next_jobs();
        while (MEEM_block_status[block_id].rewrite_all)
        {
            MEEM_PeriodicTask();
        }
        eep_sim->return_ok_for_next_jobs();
        ProcessMeemUntilIdle();

        EXPECT_TRUE(MEEM_GetBlockStatus(block_id).write_failed);
        ASSERT_FALSE(eep_sim->write_log.empty());
        EXPECT_EQ(eep_sim->write_log.back(), HeaderWriteJob());
        EXPECT_EQ(CountHeaderWrites(), 1u);
        EXPECT_EQ(GetHeaderFromEeprom(), CurrentHeader());
    }
}

TEST_F(CommitTest, FailedHeaderWriteIsNotRetried)
{
    RequestCommit();

    eep_sim->return_nok_for_next_jobs();
    ProcessMeemUntilIdle();
    eep_sim->return_ok_for_next_jobs();

    EXPECT_FALSE(MEEM_global_status.identity_pending);
    for (int tick = 0; tick < 10; tick++)
    {
        MEEM_PeriodicTask();
        EXPECT_FALSE(MEEM_IsBusy());
    }

    const std::vector<EepromSimulator::WriteJob> expected_jobs{HeaderWriteJob()};
    EXPECT_EQ(eep_sim->write_log, expected_jobs);

    for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
    {
        EXPECT_FALSE(MEEM_GetBlockStatus(block_id).write_failed) << "Block #" << static_cast<int>(block_id);
    }
}

TEST_F(CommitTest, SuspendDoesntStopTheCommit)
{
    for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
    {
        RequestRewrite(block_id);
    }
    RequestCommit();

    MEEM_PeriodicTask(); // Starts the first rewrite
    MEEM_Suspend();
    ProcessMeemUntilIdle();

    for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
    {
        EXPECT_TRUE(MEEM_GetBlockStatus(block_id).write_complete) << "Block #" << static_cast<int>(block_id);
    }
    ASSERT_FALSE(eep_sim->write_log.empty());
    EXPECT_EQ(eep_sim->write_log.back(), HeaderWriteJob());
    EXPECT_EQ(GetHeaderFromEeprom(), CurrentHeader());
}

TEST_F(CommitTest, DeInitDropsAPendingCommit)
{
    const auto header_before = GetHeaderFromEeprom();

    for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
    {
        RequestRewrite(block_id);
    }
    RequestCommit();
    MEEM_PeriodicTask(); // Starts the first rewrite

    MEEM_DeInit();

    EXPECT_FALSE(MEEM_IsBusy());
    EXPECT_FALSE(MEEM_global_status.identity_pending);
    EXPECT_EQ(CountHeaderWrites(), 0u);
    EXPECT_EQ(GetHeaderFromEeprom(), header_before);
}
