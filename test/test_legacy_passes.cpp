#include "test_legacy_base.hpp"
#include <map>

using testing::_;
using testing::InSequence;
using testing::Return;

/*!
 * \brief Tests of the migration in MEEM_Init(), in the migration configuration: what pass 1 does by each source table
 *        entry, pass 2 in the generated order and directions, the status bits and the order of the callbacks. Each test loads the image
 *        of a legacy model (test/meem_config/eeprom_datamodel_migration_legacy_*.json) and stores data in its sources:
 *        - legacy model 0: one block unchanged, copies of every management type, a routine, and Block_MultiProfile in pass 2, ascending;
 *        - legacy model 1: every block moved, Block_MultiProfile filled from a basic block in pass 1, no pass 2;
 *        - legacy model 2: only the multi-profile blocks, both in pass 2: first Block_Profiles, whose old place lies in the new place of
 *          Block_MultiProfile, then Block_MultiProfile, which moves up into its own old place and so goes from the highest profile down.
 */
class MigrationPassesTest : public MigrationTestBase
{
  public:
    /// @brief Whether pass 1 reads a source for a block from legacy model k: to copy it, or to convert it with a routine
    static bool HasSourceInPass1(uint8_t k, uint8_t block_id)
    {
        const uint8_t source = MEEM_legacy_models[k].sources[block_id];
        return ((source <= 0xFCu) || (source == MEEM_SOURCE_ROUTINE)) && !IsInPass2(k, block_id);
    }

    /// @brief Fills a block's cache with a pattern that is neither its defaults nor legacy data, so that both show
    static void FillCache(uint8_t block_id)
    {
        std::memset(MEEM_block_config[block_id].cache, 0xEE, MEEM_block_config[block_id].data_size);
    }

    /// @brief Puts the image of an instance with the given parameters into an EEPROM image, at the place of a current block's instance
    static void PutInstance(std::vector<uint8_t>& image, uint8_t block_id, uint8_t instance_id, const std::vector<uint8_t>& parameters)
    {
        const auto block_cfg     = &MEEM_block_config[block_id];
        const auto instance_size = sizeof(MEEM_checksum_t) + block_cfg->data_size;
        const auto checksum      = ReferenceCrc16(parameters.data(), parameters.size());

        image[block_cfg->offset_in_eeprom + (instance_id * instance_size)]     = static_cast<uint8_t>(checksum);
        image[block_cfg->offset_in_eeprom + (instance_id * instance_size) + 1] = static_cast<uint8_t>(checksum >> 8);
        std::copy(parameters.cbegin(), parameters.cend(), image.begin() + block_cfg->offset_in_eeprom + (instance_id * instance_size) + 2);
    }

    /// @brief Expects a block to be filled from the legacy data in pass 1: 'migrated' and a rewrite
    static void ExpectMigratedWithRewrite(uint8_t block_id)
    {
        const auto status = MEEM_GetBlockStatus(block_id);

        EXPECT_TRUE(status.migrated);
        EXPECT_FALSE(status.recovered);
        EXPECT_TRUE(status.write_pending);
        EXPECT_FALSE(status.write_complete);
        EXPECT_TRUE(MEEM_block_status[block_id].rewrite_all);
    }

    /// @brief Expects a block to keep its defaults in pass 1, with no source or no valid one: 'recovered' and a rewrite
    void ExpectDefaultsWithRewrite(uint8_t block_id)
    {
        const auto status = MEEM_GetBlockStatus(block_id);

        EXPECT_EQ(CachedParameters(block_id), DefaultsOf(block_id));
        EXPECT_FALSE(status.migrated);
        EXPECT_TRUE(status.recovered);
        EXPECT_TRUE(status.write_pending);
        EXPECT_TRUE(MEEM_block_status[block_id].rewrite_all);
    }

    /// @brief Expects the state of a block after pass 2, which writes its profiles within MEEM_Init(): no rewrite, and no write status
    static void ExpectNoRewriteAfterPass2(uint8_t block_id)
    {
        const auto status = MEEM_GetBlockStatus(block_id);

        EXPECT_FALSE(status.write_pending);
        EXPECT_FALSE(status.write_complete) << "As v1's init, pass 2 leaves it clear";
        EXPECT_FALSE(status.write_failed);
        EXPECT_FALSE(MEEM_block_status[block_id].rewrite_all);
    }
};

/******************************************************************************/
/*    The header                                                              */
/******************************************************************************/
TEST_F(MigrationPassesTest, EachLegacyHeaderStartsAMigrationThatIsCommitted)
{
    for (uint8_t k = 0; k < MEEM_LEGACY_MODEL_COUNT; k++)
    {
        SCOPED_TRACE("Legacy model " + std::to_string(k));
        LoadLegacyImage(k);
        MEEM_DeInit();

        MEEM_Init();
        EXPECT_TRUE(MEEM_global_status.identity_pending);
        EXPECT_TRUE(MEEM_IsBusy());

        ProcessMeemUntilIdle();
        EXPECT_EQ(GetHeaderFromEeprom(), CurrentHeader());

        // The next init finds the current header, and every block valid at its new place
        MEEM_DeInit();
        MEEM_Init();

        for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
        {
            EXPECT_FALSE(MEEM_GetBlockStatus(block_id).recovered) << "Block ID " << int(block_id);
            EXPECT_FALSE(MEEM_GetBlockStatus(block_id).migrated) << "Block ID " << int(block_id);
        }
        EXPECT_FALSE(MEEM_IsBusy());
    }
}

/******************************************************************************/
/*    Pass 1, by source table entry                                           */
/******************************************************************************/
TEST_F(MigrationPassesTest, Pass1_UnchangedBlockIsInitializedAsInV1)
{
    LoadLegacyImage(0);
    ASSERT_EQ(MEEM_legacy_models[0].sources[MEEM_BLOCK_Block_Unchanged_ID], MEEM_SOURCE_UNCHANGED);
    const auto instance = StoreValidFirstInstance(MEEM_BLOCK_Block_Unchanged_ID);

    MEEM_Init();

    const auto status = MEEM_GetBlockStatus(MEEM_BLOCK_Block_Unchanged_ID);
    EXPECT_EQ(SnapshotCache(MEEM_BLOCK_Block_Unchanged_ID), std::vector<uint8_t>(instance.cbegin() + sizeof(MEEM_checksum_t), instance.cend()));
    EXPECT_FALSE(status.migrated);
    EXPECT_FALSE(status.recovered);
    EXPECT_FALSE(status.write_pending) << "No rewrite";
}

TEST_F(MigrationPassesTest, Pass1_InvalidUnchangedBlockIsRecoveredAsInV1)
{
    LoadLegacyImage(0);
    CorruptInstanceInEeprom(MEEM_BLOCK_Block_Unchanged_ID, 0);

    MEEM_Init();

    // Its strategy repairs it, with a write of its single instance, as in v1
    const auto status = MEEM_GetBlockStatus(MEEM_BLOCK_Block_Unchanged_ID);
    EXPECT_EQ(CachedParameters(MEEM_BLOCK_Block_Unchanged_ID), DefaultsOf(MEEM_BLOCK_Block_Unchanged_ID));
    EXPECT_FALSE(status.migrated);
    EXPECT_TRUE(status.recovered);
    EXPECT_TRUE(status.write_pending);
    EXPECT_FALSE(MEEM_block_status[MEEM_BLOCK_Block_Unchanged_ID].rewrite_all);
}

TEST_F(MigrationPassesTest, Pass1_CopiedBlocksGetTheParametersOfTheirSources)
{
    for (uint8_t k = 0; k < MEEM_LEGACY_MODEL_COUNT; k++)
    {
        SCOPED_TRACE("Legacy model " + std::to_string(k));
        LoadLegacyImage(k);
        MEEM_DeInit();

        std::map<uint8_t, std::vector<uint8_t>> stored;
        for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
        {
            if (IsCopied(k, block_id) && !IsInPass2(k, block_id))
            {
                stored[block_id] = StoreSourceParameters(SourceOf(k, block_id));
            }
        }

        MEEM_Init();

        for (const auto& [block_id, parameters] : stored)
        {
            SCOPED_TRACE("Block ID " + std::to_string(block_id));
            EXPECT_EQ(CachedParameters(block_id), parameters);
            ExpectMigratedWithRewrite(block_id);
        }
    }
}

TEST_F(MigrationPassesTest, Pass1_BlockWithoutAValidSourceKeepsItsDefaults)
{
    for (uint8_t k = 0; k < MEEM_LEGACY_MODEL_COUNT; k++)
    {
        SCOPED_TRACE("Legacy model " + std::to_string(k));
        LoadLegacyImage(k);
        MEEM_DeInit();

        // Copied blocks and blocks with a routine
        std::vector<uint8_t> block_ids;
        for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
        {
            if (HasSourceInPass1(k, block_id))
            {
                CorruptSource(AnySourceOf(k, block_id));
                FillCache(block_id);
                block_ids.push_back(block_id);
            }
        }

        MEEM_Init();

        for (const auto block_id : block_ids)
        {
            SCOPED_TRACE("Block ID " + std::to_string(block_id));
            ExpectDefaultsWithRewrite(block_id);
        }
    }
}

TEST_F(MigrationPassesTest, Pass1_RoutineConvertsAfterTheDefaultsAreRestored)
{
    // Legacy model 1 has only 'a' of Block_Changed, so 'b' keeps its default, whatever the cache held before
    LoadLegacyImage(1);
    StoreInstance(RoutineSourceOf(1, MEEM_BLOCK_Block_Changed_ID), 0, {0x22u});
    FillCache(MEEM_BLOCK_Block_Changed_ID);

    MEEM_Init();

    EXPECT_EQ(static_cast<uint8_t>(MEEM_cache_Block_Changed.a), 0x22u);
    EXPECT_EQ(static_cast<uint16_t>(MEEM_cache_Block_Changed.b), 25187u) << "The default of test/meem_config/eeprom_datamodel_migration.json";
    ExpectMigratedWithRewrite(MEEM_BLOCK_Block_Changed_ID);
}

TEST_F(MigrationPassesTest, Pass1_BlockWithoutASourceGetsItsDefaults)
{
    // Block_New has no source in any legacy model, and Block_Profiles none in legacy model 0
    LoadLegacyImage(0);
    ASSERT_EQ(MEEM_legacy_models[0].sources[MEEM_BLOCK_Block_New_ID], MEEM_SOURCE_NONE);
    ASSERT_EQ(MEEM_legacy_models[0].sources[MEEM_BLOCK_Block_Profiles_ID], MEEM_SOURCE_NONE);
    FillCache(MEEM_BLOCK_Block_New_ID);
    FillCache(MEEM_BLOCK_Block_Profiles_ID);

    MEEM_Init();

    ExpectDefaultsWithRewrite(MEEM_BLOCK_Block_New_ID);
    ExpectDefaultsWithRewrite(MEEM_BLOCK_Block_Profiles_ID);
}

TEST_F(MigrationPassesTest, Pass1_BlockFromAMultiProfileSourceGetsProfile0)
{
    LoadLegacyImage(1);
    const auto source = SourceOf(1, MEEM_BLOCK_Block_Basic_ID);
    ASSERT_EQ(source->management_type, MEEM_MGMT_MULTI_PROFILE);

    const auto profile_0 = StoreSourceParameters(source, 0);
    StoreSourceParameters(source, 1);

    MEEM_Init();

    EXPECT_EQ(SnapshotCache(MEEM_BLOCK_Block_Basic_ID), profile_0);
    ExpectMigratedWithRewrite(MEEM_BLOCK_Block_Basic_ID);
}

TEST_F(MigrationPassesTest, Pass1_MultiProfileBlockFromAnotherTypeIsRewrittenToEveryProfile)
{
    // Filled once in pass 1, and the rewrite writes it to every profile
    constexpr uint8_t block_id = MEEM_BLOCK_Block_MultiProfile_ID;
    LoadLegacyImage(1);
    const auto source = SourceOf(1, block_id);
    ASSERT_EQ(source->management_type, MEEM_MGMT_BASIC);
    ASSERT_FALSE(IsInPass2(1, block_id));

    const auto parameters = StoreSourceParameters(source);
    ON_CALL(user_callbacks_mock, SelectInitiallyActiveProfile(block_id)).WillByDefault(Return(2));

    MEEM_Init();

    EXPECT_EQ(SnapshotCache(block_id), parameters);
    EXPECT_EQ(MEEM_GetActiveProfile(block_id), 2);
    ExpectMigratedWithRewrite(block_id);
    EXPECT_TRUE(eep_sim->write_log.empty()) << "Pass 1 only reads, and legacy model 1 has no pass 2";

    ProcessMeemUntilIdle();
    ExpectEveryInstanceHolds(block_id, parameters);
    EXPECT_EQ(MEEM_GetActiveProfile(block_id), 2) << "The rewrite keeps the active profile";
}

/******************************************************************************/
/*    Pass 2                                                                  */
/******************************************************************************/
TEST_F(MigrationPassesTest, Pass2_CopiesEachProfileToItsNewPlaceAndLoadsTheActiveOne)
{
    constexpr uint8_t block_id = MEEM_BLOCK_Block_MultiProfile_ID;
    LoadLegacyImage(0);
    const auto source = SourceOf(0, block_id);
    ASSERT_TRUE(IsInPass2(0, block_id));
    ASSERT_EQ(source->instance_count, 2);
    ASSERT_EQ(MEEM_block_config[block_id].instance_count, 3);

    // The third profile is new: it gets the defaults
    const std::vector<std::vector<uint8_t>> profiles{StoreSourceParameters(source, 0), StoreSourceParameters(source, 1), DefaultsOf(block_id)};
    ON_CALL(user_callbacks_mock, SelectInitiallyActiveProfile(block_id)).WillByDefault(Return(1));

    MEEM_Init();

    // The only writes of MEEM_Init(): the profiles from profile 0 up, as generated
    EXPECT_EQ(eep_sim->write_log, ProfileWriteJobs(block_id, {0, 1, 2}));
    for (uint8_t profile = 0; profile < 3; profile++)
    {
        EXPECT_EQ(GetInstanceFromEeprom(block_id, profile), CreateInstanceImage(profiles[profile].data(), profiles[profile].size()))
            << "Profile " << int(profile);
    }

    EXPECT_EQ(MEEM_GetActiveProfile(block_id), 1);
    EXPECT_EQ(SnapshotCache(block_id), profiles[1]);
    EXPECT_TRUE(MEEM_GetBlockStatus(block_id).migrated);
    EXPECT_FALSE(MEEM_GetBlockStatus(block_id).recovered);
    ExpectNoRewriteAfterPass2(block_id);
}

TEST_F(MigrationPassesTest, Pass2_FollowsTheGeneratedOrderAndDirections)
{
    // Legacy model 2 lays out Block_MultiProfile at 8 B below its new place, with Block_Profiles right after it, inside that new place.
    // Any other order or direction would overwrite a source profile before it's read, and the image would differ.
    LoadLegacyImage(2);
    const auto profiles_source = SourceOf(2, MEEM_BLOCK_Block_Profiles_ID);
    const auto multi_source    = RoutineSourceOf(2, MEEM_BLOCK_Block_MultiProfile_ID);
    ASSERT_EQ(profiles_source->instance_count, 2);
    ASSERT_EQ(multi_source->instance_count, 3);

    // Block_Profiles is copied, its third profile is new. The routine of Block_MultiProfile converts the first 6 of its 7 parameters.
    std::vector<std::vector<uint8_t>> expected_profiles;
    std::vector<std::vector<uint8_t>> expected_multi;
    for (uint8_t profile = 0; profile < 2; profile++)
    {
        expected_profiles.push_back(StoreSourceParameters(profiles_source, profile));
    }
    expected_profiles.push_back(DefaultsOf(MEEM_BLOCK_Block_Profiles_ID));

    for (uint8_t profile = 0; profile < 3; profile++)
    {
        auto parameters = StoreSourceParameters(multi_source, profile);
        parameters.push_back(DefaultsOf(MEEM_BLOCK_Block_MultiProfile_ID).back());
        expected_multi.push_back(parameters);
    }

    // What the EEPROM holds after pass 2: every profile of both blocks at its new place, over the old places
    auto expected_image = CreateEepromSnapshot();
    for (uint8_t profile = 0; profile < 3; profile++)
    {
        PutInstance(expected_image, MEEM_BLOCK_Block_Profiles_ID, profile, expected_profiles[profile]);
        PutInstance(expected_image, MEEM_BLOCK_Block_MultiProfile_ID, profile, expected_multi[profile]);
    }

    MEEM_Init();

    auto       expected_jobs = ProfileWriteJobs(MEEM_BLOCK_Block_Profiles_ID, {0, 1, 2});
    const auto multi_jobs    = ProfileWriteJobs(MEEM_BLOCK_Block_MultiProfile_ID, {2, 1, 0});
    expected_jobs.insert(expected_jobs.end(), multi_jobs.cbegin(), multi_jobs.cend());

    EXPECT_EQ(eep_sim->write_log, expected_jobs);
    EXPECT_TRUE(eep_sim->eeprom == expected_image) << "The EEPROM after pass 2 isn't the expected image";

    for (const auto block_id : std::vector<uint8_t>{MEEM_BLOCK_Block_Profiles_ID, MEEM_BLOCK_Block_MultiProfile_ID})
    {
        SCOPED_TRACE("Block ID " + std::to_string(block_id));
        EXPECT_TRUE(MEEM_GetBlockStatus(block_id).migrated);
        EXPECT_FALSE(MEEM_GetBlockStatus(block_id).recovered);
        ExpectNoRewriteAfterPass2(block_id);
    }
    EXPECT_EQ(SnapshotCache(MEEM_BLOCK_Block_MultiProfile_ID), expected_multi[0]) << "Profile 0 is active";
    EXPECT_EQ(SnapshotCache(MEEM_BLOCK_Block_Profiles_ID), expected_profiles[0]) << "Profile 0 is active";
}

TEST_F(MigrationPassesTest, Pass2_ProfileWithoutAValidSourceGetsTheDefaults)
{
    LoadLegacyImage(2);
    const auto source     = SourceOf(2, MEEM_BLOCK_Block_Profiles_ID);
    const auto parameters = StoreSourceParameters(source, 0);
    CorruptInstance(source, 1);

    MEEM_Init();

    const auto defaults = DefaultsOf(MEEM_BLOCK_Block_Profiles_ID);
    EXPECT_EQ(GetInstanceFromEeprom(MEEM_BLOCK_Block_Profiles_ID, 0), CreateInstanceImage(parameters.data(), parameters.size()));
    EXPECT_EQ(GetInstanceFromEeprom(MEEM_BLOCK_Block_Profiles_ID, 1), CreateInstanceImage(defaults.data(), defaults.size()));
    EXPECT_EQ(GetInstanceFromEeprom(MEEM_BLOCK_Block_Profiles_ID, 2), CreateInstanceImage(defaults.data(), defaults.size()));
    EXPECT_TRUE(MEEM_GetBlockStatus(MEEM_BLOCK_Block_Profiles_ID).migrated) << "Profile 0 came from the legacy data";
    EXPECT_FALSE(MEEM_GetBlockStatus(MEEM_BLOCK_Block_Profiles_ID).recovered);
}

TEST_F(MigrationPassesTest, Pass2_BlockWithoutAnyValidSourceProfileIsRecovered)
{
    LoadLegacyImage(2);
    CorruptSource(SourceOf(2, MEEM_BLOCK_Block_Profiles_ID));

    MEEM_Init();

    ExpectEveryInstanceHolds(MEEM_BLOCK_Block_Profiles_ID, DefaultsOf(MEEM_BLOCK_Block_Profiles_ID));
    EXPECT_EQ(CachedParameters(MEEM_BLOCK_Block_Profiles_ID), DefaultsOf(MEEM_BLOCK_Block_Profiles_ID));
    EXPECT_FALSE(MEEM_GetBlockStatus(MEEM_BLOCK_Block_Profiles_ID).migrated);
    EXPECT_TRUE(MEEM_GetBlockStatus(MEEM_BLOCK_Block_Profiles_ID).recovered);
    ExpectNoRewriteAfterPass2(MEEM_BLOCK_Block_Profiles_ID);
}

TEST_F(MigrationPassesTest, Pass2_FailedWriteSetsWriteFailed)
{
    constexpr uint8_t block_id = MEEM_BLOCK_Block_MultiProfile_ID;
    LoadLegacyImage(0);
    StoreSourceParameters(SourceOf(0, block_id), 0);

    // The power fails during the first write of pass 2, so every later job fails too
    eep_sim->cut_power_during_write(1, 0);
    EXPECT_CALL(user_callbacks_mock, OnBlockWriteStarted(_)).Times(0);
    EXPECT_CALL(user_callbacks_mock, OnBlockWriteComplete(_)).Times(0);

    MEEM_Init();

    const auto status = MEEM_GetBlockStatus(block_id);
    EXPECT_TRUE(status.write_failed);
    EXPECT_FALSE(status.write_complete);
    EXPECT_TRUE(status.migrated) << "Profile 0 came from the legacy data before the write failed";
}

/******************************************************************************/
/*    Callbacks and the commit                                                */
/******************************************************************************/
TEST_F(MigrationPassesTest, InitCompleteFollowsThePassOrder)
{
    // Legacy model 0: pass 1 in definition order, Block_Profiles included, which has no source. Then Block_MultiProfile in pass 2, which
    // selects its active profile only there.
    LoadLegacyImage(0);
    {
        InSequence sequence;

        for (const auto block_id : std::vector<uint8_t>{MEEM_BLOCK_Block_Unchanged_ID, MEEM_BLOCK_Block_Moved_ID, MEEM_BLOCK_Block_Basic_ID,
                                                        MEEM_BLOCK_Block_WearLeveling_ID, MEEM_BLOCK_Block_Changed_ID, MEEM_BLOCK_Block_New_ID})
        {
            EXPECT_CALL(user_callbacks_mock, OnBlockInitComplete(block_id));
        }
        EXPECT_CALL(user_callbacks_mock, SelectInitiallyActiveProfile(MEEM_BLOCK_Block_Profiles_ID)).WillOnce(Return(0));
        EXPECT_CALL(user_callbacks_mock, OnBlockInitComplete(MEEM_BLOCK_Block_Profiles_ID));
        EXPECT_CALL(user_callbacks_mock, SelectInitiallyActiveProfile(MEEM_BLOCK_Block_MultiProfile_ID)).WillOnce(Return(0));
        EXPECT_CALL(user_callbacks_mock, OnBlockInitComplete(MEEM_BLOCK_Block_MultiProfile_ID));
    }

    MEEM_Init();
}

TEST_F(MigrationPassesTest, InitCompleteFollowsTheOrderOfPass2)
{
    // Legacy model 2: pass 2 migrates Block_Profiles before Block_MultiProfile, against their definition order
    LoadLegacyImage(2);
    {
        InSequence sequence;

        for (const auto block_id : std::vector<uint8_t>{MEEM_BLOCK_Block_Unchanged_ID, MEEM_BLOCK_Block_Moved_ID, MEEM_BLOCK_Block_Basic_ID,
                                                        MEEM_BLOCK_Block_WearLeveling_ID, MEEM_BLOCK_Block_Changed_ID, MEEM_BLOCK_Block_New_ID})
        {
            EXPECT_CALL(user_callbacks_mock, OnBlockInitComplete(block_id));
        }
        EXPECT_CALL(user_callbacks_mock, SelectInitiallyActiveProfile(MEEM_BLOCK_Block_Profiles_ID)).WillOnce(Return(0));
        EXPECT_CALL(user_callbacks_mock, OnBlockInitComplete(MEEM_BLOCK_Block_Profiles_ID));
        EXPECT_CALL(user_callbacks_mock, SelectInitiallyActiveProfile(MEEM_BLOCK_Block_MultiProfile_ID)).WillOnce(Return(0));
        EXPECT_CALL(user_callbacks_mock, OnBlockInitComplete(MEEM_BLOCK_Block_MultiProfile_ID));
    }

    MEEM_Init();
}

TEST_F(MigrationPassesTest, OnlyTheRewritesCallTheWriteCallbacks)
{
    // Pass 2 writes within MEEM_Init() without them, and the rewrites of pass 1's blocks call them, as v1's repairs do
    LoadLegacyImage(0);
    EXPECT_CALL(user_callbacks_mock, OnBlockWriteStarted(_)).Times(0);
    EXPECT_CALL(user_callbacks_mock, OnBlockWriteComplete(_)).Times(0);

    MEEM_Init();
    testing::Mock::VerifyAndClearExpectations(&user_callbacks_mock);
    EXPECT_EQ(eep_sim->write_log.size(), 3u) << "The 3 profiles of Block_MultiProfile";

    // Every block but the unchanged one and the one of pass 2 has a rewrite, then the header commits the migration
    const std::vector<uint8_t> rewritten{MEEM_BLOCK_Block_Moved_ID,   MEEM_BLOCK_Block_Basic_ID, MEEM_BLOCK_Block_WearLeveling_ID,
                                         MEEM_BLOCK_Block_Changed_ID, MEEM_BLOCK_Block_New_ID,   MEEM_BLOCK_Block_Profiles_ID};
    for (const auto block_id : rewritten)
    {
        EXPECT_CALL(user_callbacks_mock, OnBlockWriteStarted(block_id)).Times(1);
        EXPECT_CALL(user_callbacks_mock, OnBlockWriteComplete(block_id)).Times(1);
    }
    EXPECT_CALL(user_callbacks_mock, OnBlockWriteStarted(MEEM_BLOCK_Block_Unchanged_ID)).Times(0);
    EXPECT_CALL(user_callbacks_mock, OnBlockWriteStarted(MEEM_BLOCK_Block_MultiProfile_ID)).Times(0);

    eep_sim->write_log.clear();
    ProcessMeemUntilIdle();

    EXPECT_EQ(eep_sim->write_log, RewriteAndCommitJobs(rewritten));
    EXPECT_EQ(GetHeaderFromEeprom(), CurrentHeader());

    for (const auto block_id : rewritten)
    {
        EXPECT_TRUE(MEEM_GetBlockStatus(block_id).write_complete) << "Block ID " << int(block_id);
    }
    EXPECT_TRUE(MEEM_GetBlockStatus(MEEM_BLOCK_Block_Moved_ID).migrated) << "The rewrite leaves it set";
}
