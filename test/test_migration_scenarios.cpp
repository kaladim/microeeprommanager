#include "test_legacy_base.hpp"
#include <map>

using testing::Return;

/*!
 * \brief Scenarios of a migration in the migration configuration. Each one goes from the image of a legacy model
 *        (test/meem_config/eeprom_datamodel_migration_legacy_*.json) through MEEM_Init(), the rewrites and the commit, to a restart whose
 *        MEEM_Init() finds the current header and initializes every block as in v1 from its new place. Changes to the parameters
 *        of a block are also covered by the scenarios of the conversions configuration (test_conversions.cpp), and a larger example
 *        by the -MigrationExample configuration (test_migration_example.cpp).
 */
class MigrationScenariosTest : public MigrationTestBase
{
  public:
    /// @brief Bytes [first, second) of the EEPROM
    using Place = std::pair<size_t, size_t>;

    /// @brief Migrates from the image in the EEPROM: MEEM_Init(), then the rewrites and the header write, which commits the migration
    void Migrate()
    {
        MEEM_Init();
        ProcessMeemUntilIdle();
        EXPECT_EQ(GetHeaderFromEeprom(), CurrentHeader()) << "Committed";
    }

    /// @brief Restarts the device after the commit: MEEM_Init() initializes every block as in v1, finds every one valid at its new place,
    ///        and writes nothing
    void Restart()
    {
        MEEM_DeInit();
        eep_sim->write_log.clear();
        MEEM_Init();

        EXPECT_TRUE(eep_sim->write_log.empty());
        EXPECT_FALSE(MEEM_IsBusy());
        for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
        {
            EXPECT_FALSE(MEEM_GetBlockStatus(block_id).recovered) << "Block ID " << int(block_id);
            EXPECT_FALSE(MEEM_GetBlockStatus(block_id).migrated) << "Block ID " << int(block_id);
        }
    }

    /// @brief Restarts the device after a migration from legacy model k that a power cut interrupted, so that the header in the EEPROM
    ///        doesn't commit it. A failed header write is simulated by a power cut during it too, which writes none of its bytes.
    void RestartBeforeTheCommit(uint8_t k)
    {
        ASSERT_FALSE(eep_sim->is_powered());
        EXPECT_EQ(GetHeaderFromEeprom(), LegacyHeader(k)) << "Not committed";

        eep_sim->restore_power();
        MEEM_DeInit();
        eep_sim->write_log.clear();
    }

    static std::vector<uint8_t> LegacyHeader(uint8_t k)
    {
        const uint8_t* header = MEEM_legacy_models[k].identity_header;
        return std::vector<uint8_t>(header, header + sizeof(MEEM_identity_header));
    }

    /// @brief Parameters that follow a pattern: first, first + 1, ...
    static std::vector<uint8_t> Pattern(size_t size, uint8_t first)
    {
        std::vector<uint8_t> parameters(size);
        std::iota(parameters.begin(), parameters.end(), first);
        return parameters;
    }

    /// @brief Stores parameters in every source of legacy model k that the core reads, each profile of a multi-profile source included.
    ///        They follow a pattern of their own per block and profile instead of being random, so that no outcome depends on chance: an
    ///        overwritten old instance still passes its old check with a chance of 1/65536.
    /// @return the parameters of each block with a source, per profile
    std::map<uint8_t, std::vector<std::vector<uint8_t>>> StoreEverySource(uint8_t k)
    {
        std::map<uint8_t, std::vector<std::vector<uint8_t>>> stored;

        for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
        {
            const uint8_t entry = MEEM_legacy_models[k].sources[block_id];

            if ((entry > 0xFCu) && (entry != MEEM_SOURCE_ROUTINE))
            {
                continue; /* Unchanged, or no source */
            }

            const auto    source   = AnySourceOf(k, block_id);
            const uint8_t profiles = (source->management_type == MEEM_MGMT_MULTI_PROFILE) ? source->instance_count : 1u;

            for (uint8_t profile = 0; profile < profiles; profile++)
            {
                const auto parameters =
                    Pattern(ParameterSize(source->data_size, source->management_type), static_cast<uint8_t>((0x10 * (block_id + 1)) + (3 * profile)));

                StoreSourceParameters(source, profile, parameters);
                stored[block_id].push_back(parameters);
            }
        }
        return stored;
    }

    /// @brief The bytes that a block takes at its new place, every instance included
    static Place NewPlaceOf(uint8_t block_id)
    {
        const auto block_cfg = &MEEM_block_config[block_id];
        return {block_cfg->offset_in_eeprom, block_cfg->offset_in_eeprom + (block_cfg->instance_count * (sizeof(MEEM_checksum_t) + block_cfg->data_size))};
    }

    /// @brief The bytes that a legacy source takes at its old place, every instance included
    static Place OldPlaceOf(const MEEM_legacyBlockConfig_t* source)
    {
        return {source->offset_in_eeprom, source->offset_in_eeprom + (source->instance_count * InstanceSize(source))};
    }

    /// @brief The bytes of one instance of a legacy source
    static Place OldPlaceOf(const MEEM_legacyBlockConfig_t* source, uint8_t instance_index)
    {
        return {InstanceOffset(source, instance_index), InstanceOffset(source, instance_index) + InstanceSize(source)};
    }

    static bool Overlap(const Place& a, const Place& b)
    {
        return (a.first < b.second) && (b.first < a.second);
    }

    /// @brief Expects the profiles of a multi-profile block to hold the given parameters in the EEPROM
    void ExpectProfilesHold(uint8_t block_id, const std::vector<std::vector<uint8_t>>& profiles)
    {
        ASSERT_EQ(profiles.size(), MEEM_block_config[block_id].instance_count);

        for (uint8_t profile = 0; profile < profiles.size(); profile++)
        {
            EXPECT_EQ(GetInstanceFromEeprom(block_id, profile), CreateInstanceImage(profiles[profile].data(), profiles[profile].size()))
                << "Profile " << int(profile);
        }
    }

    /// @brief Expects a block to have its defaults after a migration, at its new place and in its cache: 'recovered'
    void ExpectDefaults(uint8_t block_id)
    {
        auto content = SnapshotCache(block_id);
        auto params  = DefaultsOf(block_id);
        std::copy(params.cbegin(), params.cend(), content.end() - params.size());

        EXPECT_TRUE(MEEM_GetBlockStatus(block_id).recovered);
        EXPECT_FALSE(MEEM_GetBlockStatus(block_id).migrated);
        EXPECT_EQ(CachedParameters(block_id), params);
        ExpectEveryInstanceHolds(block_id, content);
    }
};

/******************************************************************************/
/*    Each kind of change to a block                                          */
/******************************************************************************/
TEST_F(MigrationScenariosTest, MovedBlockIsCopiedFromItsOldPlace)
{
    // Legacy model 0 has Block_Removed in front of Block_Moved, which moves down into its old place.
    constexpr uint8_t block_id = MEEM_BLOCK_Block_Moved_ID;
    LoadLegacyImage(0);
    const auto source = SourceOf(0, block_id);
    ASSERT_GT(source->offset_in_eeprom, MEEM_block_config[block_id].offset_in_eeprom);
    ASSERT_EQ(source->management_type, MEEM_block_config[block_id].management_type);
    const auto parameters = StoreSourceParameters(source);

    Migrate();
    EXPECT_TRUE(MEEM_GetBlockStatus(block_id).migrated);
    ExpectEveryInstanceHolds(block_id, parameters);

    Restart();
    EXPECT_EQ(SnapshotCache(block_id), parameters);
}

TEST_F(MigrationScenariosTest, BackupCopySourceGivesItsFirstValidCopy)
{
    // Legacy model 0: Block_Basic was a 'backup copy' block, whose first copy fails its check.
    constexpr uint8_t block_id = MEEM_BLOCK_Block_Basic_ID;
    LoadLegacyImage(0);
    const auto source = SourceOf(0, block_id);
    ASSERT_EQ(source->management_type, MEEM_MGMT_BACKUP_COPY);
    ASSERT_EQ(MEEM_block_config[block_id].management_type, MEEM_MGMT_BASIC);

    const auto second = GenerateRandomBytes(source->data_size);
    StoreInstance(source, 0, GenerateRandomBytes(source->data_size));
    StoreInstance(source, 1, second);
    CorruptInstance(source, 0);

    Migrate();
    EXPECT_TRUE(MEEM_GetBlockStatus(block_id).migrated);

    Restart();
    EXPECT_EQ(SnapshotCache(block_id), second);
}

TEST_F(MigrationScenariosTest, WearLevelingSourceGivesItsMostRecentInstanceToFewerInstances)
{
    // Legacy model 0: 5 -> 3 instances. The most recent one lies in the middle of the old
    // place, after the sequence counter wrapped around.
    constexpr uint8_t block_id = MEEM_BLOCK_Block_WearLeveling_ID;
    LoadLegacyImage(0);
    const auto source = SourceOf(0, block_id);
    ASSERT_EQ(source->instance_count, 5u);
    ASSERT_EQ(MEEM_block_config[block_id].instance_count, 3u);

    const std::vector<uint8_t> sequence_counters{254u, 255u, 0u, 252u, 253u};
    std::vector<uint8_t>       most_recent;
    for (uint8_t instance_index = 0; instance_index < source->instance_count; instance_index++)
    {
        auto data = GenerateRandomBytes(source->data_size);
        data[0]   = sequence_counters[instance_index];
        StoreInstance(source, instance_index, data);

        if (instance_index == 2)
        {
            most_recent = data;
        }
    }

    Migrate();
    EXPECT_TRUE(MEEM_GetBlockStatus(block_id).migrated);
    ExpectEveryInstanceHolds(block_id, most_recent); // The rewrite starts the sequence over

    Restart();
    EXPECT_EQ(CachedParameters(block_id), ParametersOf(most_recent, MEEM_MGMT_WEAR_LEVELING));
}

TEST_F(MigrationScenariosTest, ProfilesAreCopiedByIndexAndANewOneGetsTheDefaults)
{
    // Legacy model 0: Block_MultiProfile grows from 2 to 3 profiles, in pass 2.
    constexpr uint8_t block_id = MEEM_BLOCK_Block_MultiProfile_ID;
    LoadLegacyImage(0);
    const auto source = SourceOf(0, block_id);
    ASSERT_EQ(source->instance_count, 2u);
    ASSERT_EQ(MEEM_block_config[block_id].instance_count, 3u);

    const std::vector<std::vector<uint8_t>> profiles{StoreSourceParameters(source, 0), StoreSourceParameters(source, 1), DefaultsOf(block_id)};

    Migrate();
    EXPECT_TRUE(MEEM_GetBlockStatus(block_id).migrated);
    ExpectProfilesHold(block_id, profiles);

    for (uint8_t active = 0; active < 3; active++)
    {
        SCOPED_TRACE("Active profile " + std::to_string(active));
        ON_CALL(user_callbacks_mock, SelectInitiallyActiveProfile(block_id)).WillByDefault(Return(active));

        Restart();
        EXPECT_EQ(SnapshotCache(block_id), profiles[active]);
    }
}

TEST_F(MigrationScenariosTest, AddedParameterKeepsItsDefault)
{
    // Legacy model 1 has only 'a' of Block_Changed.
    LoadLegacyImage(1);
    StoreInstance(RoutineSourceOf(1, MEEM_BLOCK_Block_Changed_ID), 0, {0x5Au});

    Migrate();
    EXPECT_TRUE(MEEM_GetBlockStatus(MEEM_BLOCK_Block_Changed_ID).migrated);

    Restart();
    EXPECT_EQ(static_cast<uint8_t>(MEEM_cache_Block_Changed.a), 0x5Au);
    EXPECT_EQ(static_cast<uint16_t>(MEEM_cache_Block_Changed.b), 25187u) << "The default of test/meem_config/eeprom_datamodel_migration.json";
}

TEST_F(MigrationScenariosTest, ChangedTypeIsConverted)
{
    // Legacy model 0: 'b' of Block_Changed widens from uint8 to uint16.
    LoadLegacyImage(0);
    StoreInstance(RoutineSourceOf(0, MEEM_BLOCK_Block_Changed_ID), 0, {0x5Au, 0xFEu});

    Migrate();
    EXPECT_TRUE(MEEM_GetBlockStatus(MEEM_BLOCK_Block_Changed_ID).migrated);

    Restart();
    EXPECT_EQ(static_cast<uint8_t>(MEEM_cache_Block_Changed.a), 0x5Au);
    EXPECT_EQ(static_cast<uint16_t>(MEEM_cache_Block_Changed.b), 0xFEu);
}

TEST_F(MigrationScenariosTest, GrownMultiplicityKeepsTheDefaultsOfTheNewElements)
{
    // Legacy model 2: 'param' of Block_MultiProfile grows from 6 to 7 elements, converted by a routine in pass 2.
    constexpr uint8_t block_id = MEEM_BLOCK_Block_MultiProfile_ID;
    LoadLegacyImage(2);
    const auto source = RoutineSourceOf(2, block_id);
    ASSERT_EQ(source->data_size, 6u);
    ASSERT_EQ(MEEM_block_config[block_id].data_size, 7u);

    std::vector<std::vector<uint8_t>> profiles;
    for (uint8_t profile = 0; profile < 3; profile++)
    {
        auto parameters = StoreSourceParameters(source, profile);
        parameters.push_back(DefaultsOf(block_id).back());
        profiles.push_back(parameters);
    }

    Migrate();
    EXPECT_TRUE(MEEM_GetBlockStatus(block_id).migrated);
    ExpectProfilesHold(block_id, profiles);

    ON_CALL(user_callbacks_mock, SelectInitiallyActiveProfile(block_id)).WillByDefault(Return(2));
    Restart();
    EXPECT_EQ(SnapshotCache(block_id), profiles[2]);
}

TEST_F(MigrationScenariosTest, BlockFromAMultiProfileSourceGetsProfile0)
{
    // Legacy model 1: Block_Basic was a multi-profile block.
    constexpr uint8_t block_id = MEEM_BLOCK_Block_Basic_ID;
    LoadLegacyImage(1);
    const auto source = SourceOf(1, block_id);
    ASSERT_EQ(source->management_type, MEEM_MGMT_MULTI_PROFILE);

    const auto profile_0 = StoreSourceParameters(source, 0);
    StoreSourceParameters(source, 1);

    Migrate();
    EXPECT_TRUE(MEEM_GetBlockStatus(block_id).migrated);

    Restart();
    EXPECT_EQ(SnapshotCache(block_id), profile_0);
}

TEST_F(MigrationScenariosTest, BlockThatBecomesMultiProfileHasTheDataInEveryProfile)
{
    // Legacy model 1: Block_MultiProfile was a basic block. Pass 1 fills it, and its rewrite writes every profile.
    constexpr uint8_t block_id = MEEM_BLOCK_Block_MultiProfile_ID;
    LoadLegacyImage(1);
    const auto source = SourceOf(1, block_id);
    ASSERT_EQ(source->management_type, MEEM_MGMT_BASIC);
    const auto parameters = StoreSourceParameters(source);

    Migrate();
    EXPECT_TRUE(MEEM_GetBlockStatus(block_id).migrated);
    ExpectProfilesHold(block_id, {parameters, parameters, parameters});

    ON_CALL(user_callbacks_mock, SelectInitiallyActiveProfile(block_id)).WillByDefault(Return(1));
    Restart();
    EXPECT_EQ(SnapshotCache(block_id), parameters);
}

TEST_F(MigrationScenariosTest, AddedBlocksGetTheirDefaults)
{
    // Neither Block_New nor the multi-profile Block_Profiles has a source in legacy model 0.
    LoadLegacyImage(0);

    Migrate();
    ExpectDefaults(MEEM_BLOCK_Block_New_ID);
    ExpectDefaults(MEEM_BLOCK_Block_Profiles_ID);

    Restart();
    EXPECT_EQ(CachedParameters(MEEM_BLOCK_Block_New_ID), DefaultsOf(MEEM_BLOCK_Block_New_ID));
    EXPECT_EQ(CachedParameters(MEEM_BLOCK_Block_Profiles_ID), DefaultsOf(MEEM_BLOCK_Block_Profiles_ID));
}

TEST_F(MigrationScenariosTest, RemovedBlockIsDroppedEvenWhereItsBytesPassACurrentCheck)
{
    // Legacy model 2's removed Block_Filler, its first block, covers the new place of Block_Unchanged, which has no
    // source in legacy model 2. Its data holds a valid instance of Block_Unchanged there, which init would take as in v1 if the rewrite
    // didn't overwrite it.
    constexpr uint8_t block_id      = MEEM_BLOCK_Block_Unchanged_ID;
    constexpr size_t  filler_offset = sizeof(MEEM_identity_header);
    constexpr size_t  filler_size   = sizeof(MEEM_checksum_t) + 112u;
    LoadLegacyImage(2);
    ASSERT_EQ(MEEM_legacy_models[2].sources[block_id], MEEM_SOURCE_NONE);
    ASSERT_EQ(RoutineSourceOf(2, MEEM_BLOCK_Block_MultiProfile_ID)->offset_in_eeprom, filler_offset + filler_size) << "Block_Filler is in front of it";
    ASSERT_TRUE(NewPlaceOf(block_id).first >= filler_offset + sizeof(MEEM_checksum_t) && NewPlaceOf(block_id).second <= filler_offset + filler_size);

    // The leftover inside Block_Filler's data, and Block_Filler's checksum over it
    const auto leftover = StoreValidFirstInstance(block_id);
    const auto filler   = CreateInstanceImage(&eep_sim->eeprom[filler_offset + sizeof(MEEM_checksum_t)], filler_size - sizeof(MEEM_checksum_t));
    std::copy(filler.cbegin(), filler.cend(), eep_sim->eeprom.begin() + filler_offset);
    ASSERT_EQ(GetInstanceFromEeprom(block_id, 0), leftover);

    Migrate();
    ExpectDefaults(block_id);

    Restart();
    EXPECT_EQ(SnapshotCache(block_id), DefaultsOf(block_id));
}

/******************************************************************************/
/*    An interrupted migration                                                */
/******************************************************************************/
TEST_F(MigrationScenariosTest, PowerCutDuringTheRewritesMigratesAgain)
{
    // Legacy model 0. Pass 2 has written Block_MultiProfile over the old place of Block_Basic. The rewrites of Block_Moved and Block_Basic are
    // done, Block_Basic's over the old place of Block_Moved, when the power fails during the second instance of Block_WearLeveling: its
    // rewrite has gone over the old place of Block_MultiProfile.
    LoadLegacyImage(0);
    ASSERT_TRUE(Overlap(NewPlaceOf(MEEM_BLOCK_Block_MultiProfile_ID), OldPlaceOf(SourceOf(0, MEEM_BLOCK_Block_Basic_ID))));
    ASSERT_TRUE(Overlap(NewPlaceOf(MEEM_BLOCK_Block_Basic_ID), OldPlaceOf(SourceOf(0, MEEM_BLOCK_Block_Moved_ID))));
    ASSERT_TRUE(Overlap(NewPlaceOf(MEEM_BLOCK_Block_WearLeveling_ID), OldPlaceOf(SourceOf(0, MEEM_BLOCK_Block_MultiProfile_ID))));

    const auto unchanged = StoreValidFirstInstance(MEEM_BLOCK_Block_Unchanged_ID);
    const auto stored    = StoreEverySource(0);

    MEEM_Init();
    eep_sim->cut_power_during_write(4, 5);
    ProcessMeemUntilIdle();

    auto expected_jobs = ProfileWriteJobs(MEEM_BLOCK_Block_MultiProfile_ID, {0, 1, 2});
    for (const auto& jobs : {ProfileWriteJobs(MEEM_BLOCK_Block_Moved_ID, {0}), ProfileWriteJobs(MEEM_BLOCK_Block_Basic_ID, {0}),
                             ProfileWriteJobs(MEEM_BLOCK_Block_WearLeveling_ID, {0, 1})})
    {
        expected_jobs.insert(expected_jobs.end(), jobs.cbegin(), jobs.cend());
    }
    ASSERT_GE(eep_sim->write_log.size(), expected_jobs.size());
    ASSERT_EQ(std::vector<EepromSimulator::WriteJob>(eep_sim->write_log.cbegin(), eep_sim->write_log.cbegin() + expected_jobs.size()), expected_jobs)
        << "The jobs until the power cut";
    RestartBeforeTheCommit(0);

    // The header still names legacy model 0, so the restart migrates again. Old places that were overwritten fail their old checks, and
    // those blocks get their defaults. The others are migrated again.
    Migrate();
    ExpectDefaults(MEEM_BLOCK_Block_Moved_ID);
    ExpectDefaults(MEEM_BLOCK_Block_Basic_ID);
    ExpectDefaults(MEEM_BLOCK_Block_MultiProfile_ID);
    EXPECT_TRUE(MEEM_GetBlockStatus(MEEM_BLOCK_Block_WearLeveling_ID).migrated);
    EXPECT_TRUE(MEEM_GetBlockStatus(MEEM_BLOCK_Block_Changed_ID).migrated);

    Restart();
    EXPECT_EQ(GetInstanceFromEeprom(MEEM_BLOCK_Block_Unchanged_ID, 0), unchanged) << "Never overwritten";
    EXPECT_EQ(CachedParameters(MEEM_BLOCK_Block_WearLeveling_ID), stored.at(MEEM_BLOCK_Block_WearLeveling_ID)[0]);
    EXPECT_EQ(static_cast<uint8_t>(MEEM_cache_Block_Changed.a), stored.at(MEEM_BLOCK_Block_Changed_ID)[0][0]);
    EXPECT_EQ(static_cast<uint16_t>(MEEM_cache_Block_Changed.b), stored.at(MEEM_BLOCK_Block_Changed_ID)[0][1]);
}

TEST_F(MigrationScenariosTest, PowerCutDuringPass2MigratesAgain)
{
    // Legacy model 2. Pass 2 has written every profile of Block_Profiles to its new place, and the highest profile of Block_MultiProfile
    // over the old place of Block_Profiles. The power fails during the next profile, whose first bytes tear the highest old profile of
    // Block_MultiProfile.
    constexpr uint8_t multi_id    = MEEM_BLOCK_Block_MultiProfile_ID;
    constexpr uint8_t profiles_id = MEEM_BLOCK_Block_Profiles_ID;
    LoadLegacyImage(2);
    const auto multi_source    = RoutineSourceOf(2, multi_id);
    const auto profiles_source = SourceOf(2, profiles_id);
    const auto multi_profile_2 = ProfileWriteJobs(multi_id, {2})[0];
    const auto multi_profile_1 = ProfileWriteJobs(multi_id, {1})[0];
    for (uint8_t profile = 0; profile < profiles_source->instance_count; profile++)
    {
        ASSERT_TRUE(Overlap({multi_profile_2.offset, multi_profile_2.offset + multi_profile_2.size}, OldPlaceOf(profiles_source, profile)));
    }
    ASSERT_TRUE(Overlap({multi_profile_1.offset, multi_profile_1.offset + 4}, OldPlaceOf(multi_source, 2)));
    ASSERT_FALSE(Overlap({multi_profile_1.offset, multi_profile_1.offset + 4}, OldPlaceOf(multi_source, 1)));

    const auto stored = StoreEverySource(2);
    eep_sim->cut_power_during_write(5, 4);

    MEEM_Init();

    auto       expected_jobs = ProfileWriteJobs(profiles_id, {0, 1, 2});
    const auto multi_jobs    = ProfileWriteJobs(multi_id, {2, 1});
    expected_jobs.insert(expected_jobs.end(), multi_jobs.cbegin(), multi_jobs.cend());
    ASSERT_GE(eep_sim->write_log.size(), expected_jobs.size());
    ASSERT_EQ(std::vector<EepromSimulator::WriteJob>(eep_sim->write_log.cbegin(), eep_sim->write_log.cbegin() + expected_jobs.size()), expected_jobs)
        << "The jobs until the power cut";

    const auto profiles_defaults = DefaultsOf(profiles_id);
    ExpectProfilesHold(profiles_id, {stored.at(profiles_id)[0], stored.at(profiles_id)[1], profiles_defaults}); // Migrated before the power cut
    RestartBeforeTheCommit(2);

    // The restart migrates again. Block_Profiles' old profiles fail their checks now, so every profile of it gets the defaults, over those
    // that the first attempt had migrated. Block_MultiProfile gets the defaults in its torn profile 2.
    Migrate();
    ExpectDefaults(profiles_id);

    std::vector<std::vector<uint8_t>> multi_profiles{stored.at(multi_id)[0], stored.at(multi_id)[1], DefaultsOf(multi_id)};
    multi_profiles[0].push_back(multi_profiles[2].back());
    multi_profiles[1].push_back(multi_profiles[2].back());
    EXPECT_TRUE(MEEM_GetBlockStatus(multi_id).migrated);
    ExpectProfilesHold(multi_id, multi_profiles);

    Restart();
    EXPECT_EQ(SnapshotCache(multi_id), multi_profiles[0]);
    EXPECT_EQ(SnapshotCache(profiles_id), profiles_defaults);
}

TEST_F(MigrationScenariosTest, FailedHeaderWriteMigratesAgain)
{
    // Legacy model 0. Every rewrite is done when the header write fails, so the next init repeats the migration with the outcomes of a power
    // cut during the rewrites: each old place that a new place covers is overwritten.
    LoadLegacyImage(0);
    const auto unchanged = StoreValidFirstInstance(MEEM_BLOCK_Block_Unchanged_ID);
    const auto stored    = StoreEverySource(0);

    const auto wl_source = SourceOf(0, MEEM_BLOCK_Block_WearLeveling_ID);
    ASSERT_TRUE(Overlap(NewPlaceOf(MEEM_BLOCK_Block_New_ID), OldPlaceOf(RoutineSourceOf(0, MEEM_BLOCK_Block_Changed_ID))));
    ASSERT_TRUE(Overlap(NewPlaceOf(MEEM_BLOCK_Block_Changed_ID), OldPlaceOf(wl_source, 3)));
    for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
    {
        ASSERT_FALSE(Overlap(NewPlaceOf(block_id), OldPlaceOf(wl_source, 4))) << "Block ID " << int(block_id);
    }

    MEEM_Init();
    eep_sim->write_log.clear();

    // The header write fails, and none of its bytes reach the EEPROM
    const auto jobs = RewriteAndCommitJobs({MEEM_BLOCK_Block_Moved_ID, MEEM_BLOCK_Block_Basic_ID, MEEM_BLOCK_Block_WearLeveling_ID, MEEM_BLOCK_Block_Changed_ID,
                                            MEEM_BLOCK_Block_New_ID, MEEM_BLOCK_Block_Profiles_ID});
    eep_sim->cut_power_during_write(static_cast<unsigned>(jobs.size()), 0);
    ProcessMeemUntilIdle();

    EXPECT_EQ(eep_sim->write_log, jobs);
    EXPECT_FALSE(MEEM_global_status.identity_pending);
    RestartBeforeTheCommit(0);

    // Block_WearLeveling keeps its data: the rewrites have torn the instances 0, 2 and 3 of its old place, but not the most recent one, 4
    Migrate();
    ExpectDefaults(MEEM_BLOCK_Block_Moved_ID);
    ExpectDefaults(MEEM_BLOCK_Block_Basic_ID);
    ExpectDefaults(MEEM_BLOCK_Block_MultiProfile_ID);
    ExpectDefaults(MEEM_BLOCK_Block_Changed_ID);
    EXPECT_TRUE(MEEM_GetBlockStatus(MEEM_BLOCK_Block_WearLeveling_ID).migrated);

    Restart();
    EXPECT_EQ(GetInstanceFromEeprom(MEEM_BLOCK_Block_Unchanged_ID, 0), unchanged) << "Never overwritten";
    EXPECT_EQ(CachedParameters(MEEM_BLOCK_Block_WearLeveling_ID), stored.at(MEEM_BLOCK_Block_WearLeveling_ID)[0]);
}
