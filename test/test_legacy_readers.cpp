#include "test_legacy_base.hpp"

/*!
 * \brief Tests of the readers of the legacy sources, in the migration configuration: MEEM_ReadLegacyInstance(),
 *        which serves the migration routines, and MEEM_CopyLegacyBlock(), which serves the copied blocks. They're called directly on the
 *        images of the legacy models, whose sources cover every management type (test/meem_config/eeprom_datamodel_migration_legacy_*.json).
 */
class LegacyReadersTest : public LegacyTestBase
{
  public:
    void SetUp() override
    {
        LegacyTestBase::SetUp(); // Important: Call base class SetUp() first
        MEEM_DeInit();
    }

    void TearDown() override
    {
        eep_sim->return_ok_for_next_jobs();
        LegacyTestBase::TearDown();
    }

    /// @brief Stores the instances of a 'wear-leveling' source with random parameters and the given sequence counters
    /// @return the data of each instance
    std::vector<std::vector<uint8_t>> StoreWearLevelingInstances(const MEEM_legacyBlockConfig_t* source, const std::vector<uint8_t>& sequence_counters)
    {
        std::vector<std::vector<uint8_t>> instances;

        for (uint8_t instance_index = 0; instance_index < sequence_counters.size(); instance_index++)
        {
            auto data = GenerateRandomBytes(source->data_size);
            data[0]   = sequence_counters[instance_index];
            StoreInstance(source, instance_index, data);
            instances.push_back(data);
        }
        return instances;
    }

    /// @brief The parameters within the data of an instance or a cache: without a 'wear-leveling' sequence counter
    static std::vector<uint8_t> ParametersOf(const std::vector<uint8_t>& data, uint8_t management_type)
    {
        return std::vector<uint8_t>(data.cbegin() + ((management_type == MEEM_MGMT_WEAR_LEVELING) ? 1 : 0), data.cend());
    }

    /// @brief The parameters of an instance of a legacy source in the simulated EEPROM
    std::vector<uint8_t> ParametersInEeprom(const MEEM_legacyBlockConfig_t* source, uint8_t instance_index)
    {
        const auto begin = eep_sim->eeprom.cbegin() + InstanceOffset(source, instance_index) + sizeof(MEEM_checksum_t);
        return ParametersOf(std::vector<uint8_t>(begin, begin + source->data_size), source->management_type);
    }

    /// @brief Reads a legacy source with MEEM_ReadLegacyInstance()
    /// @return the parameters it points to, none if it returns NULL
    static std::vector<uint8_t> ReadParameters(const MEEM_legacyBlockConfig_t* source, uint8_t profile)
    {
        const uint8_t* parameters = MEEM_ReadLegacyInstance(source, profile);

        if (parameters == nullptr)
        {
            return {};
        }
        return std::vector<uint8_t>(parameters, parameters + ParameterSize(source->data_size, source->management_type));
    }

    /// @brief The parameters in a block's cache: without a 'wear-leveling' sequence counter
    std::vector<uint8_t> CachedParameters(uint8_t block_id)
    {
        return ParametersOf(SnapshotCache(block_id), MEEM_block_config[block_id].management_type);
    }

    /// @brief Fills a block's cache with a pattern that no legacy source holds, so that a copy shows
    static void FillCache(uint8_t block_id, uint8_t pattern)
    {
        std::memset(MEEM_block_config[block_id].cache, pattern, MEEM_block_config[block_id].data_size);
    }
};

/******************************************************************************/
/*    Basic                                                                   */
/******************************************************************************/
TEST_F(LegacyReadersTest, Basic_ReadsTheSingleInstanceAtItsLegacyOffset)
{
    LoadLegacyImage(1);
    const auto source = SourceOf(1, MEEM_BLOCK_Block_Moved_ID);
    ASSERT_EQ(source->management_type, MEEM_MGMT_BASIC);
    ASSERT_NE(source->offset_in_eeprom, MEEM_block_config[MEEM_BLOCK_Block_Moved_ID].offset_in_eeprom);

    const auto data = GenerateRandomBytes(source->data_size);
    StoreInstance(source, 0, data);

    EXPECT_EQ(MEEM_ReadLegacyInstance(source, 0), &MEEM_work_buffer[sizeof(MEEM_checksum_t)]);
    EXPECT_EQ(ReadParameters(source, 0), data);
}

TEST_F(LegacyReadersTest, Basic_NullForAnInvalidOrAnErasedInstance)
{
    LoadLegacyImage(1);
    const auto source = SourceOf(1, MEEM_BLOCK_Block_Moved_ID);

    CorruptInstance(source, 0);
    EXPECT_EQ(MEEM_ReadLegacyInstance(source, 0), nullptr);

    EraseInstance(source, 0);
    EXPECT_EQ(MEEM_ReadLegacyInstance(source, 0), nullptr);
}

/******************************************************************************/
/*    Backup copy: a management type that only a legacy source has            */
/******************************************************************************/
TEST_F(LegacyReadersTest, BackupCopy_TheFirstCopyWhenBothAreValid)
{
    ASSERT_FALSE(MEEM_USING_BACKUP_COPY_BLOCKS) << "Only the reader of the legacy source is compiled for this type";

    LoadLegacyImage(0);
    const auto source = SourceOf(0, MEEM_BLOCK_Block_Basic_ID);
    ASSERT_EQ(source->management_type, MEEM_MGMT_BACKUP_COPY);

    const auto first  = GenerateRandomBytes(source->data_size);
    auto       second = first;
    second[0] ^= 0xFFu;
    StoreInstance(source, 0, first);
    StoreInstance(source, 1, second);

    EXPECT_EQ(ReadParameters(source, 0), first);
}

TEST_F(LegacyReadersTest, BackupCopy_TheSecondCopyWhenTheFirstIsInvalid)
{
    LoadLegacyImage(0);
    const auto source = SourceOf(0, MEEM_BLOCK_Block_Basic_ID);

    const auto second = GenerateRandomBytes(source->data_size);
    StoreInstance(source, 1, second);
    CorruptInstance(source, 0);

    EXPECT_EQ(ReadParameters(source, 0), second);
}

TEST_F(LegacyReadersTest, BackupCopy_NullWhenNoCopyIsValid)
{
    LoadLegacyImage(0);
    const auto source = SourceOf(0, MEEM_BLOCK_Block_Basic_ID);

    CorruptInstance(source, 0);
    EraseInstance(source, 1);

    EXPECT_EQ(MEEM_ReadLegacyInstance(source, 0), nullptr);
}

/******************************************************************************/
/*    Multi-profile                                                           */
/******************************************************************************/
TEST_F(LegacyReadersTest, MultiProfile_ReadsTheGivenProfile)
{
    LoadLegacyImage(0);
    const auto source = SourceOf(0, MEEM_BLOCK_Block_MultiProfile_ID);
    ASSERT_EQ(source->management_type, MEEM_MGMT_MULTI_PROFILE);
    ASSERT_EQ(source->instance_count, 2u);

    const auto profile_0 = GenerateRandomBytes(source->data_size);
    auto       profile_1 = GenerateRandomBytes(source->data_size);
    profile_1[0]         = static_cast<uint8_t>(~profile_0[0]);
    StoreInstance(source, 0, profile_0);
    StoreInstance(source, 1, profile_1);

    EXPECT_EQ(ReadParameters(source, 0), profile_0);
    EXPECT_EQ(ReadParameters(source, 1), profile_1);
}

TEST_F(LegacyReadersTest, MultiProfile_NothingAboveTheOldProfileCount)
{
    LoadLegacyImage(0);
    const auto source = SourceOf(0, MEEM_BLOCK_Block_MultiProfile_ID);

    // The current block has a third profile, the source only two. Where a third would be, the image has room for a valid instance.
    const uint8_t new_profile = source->instance_count;
    ASSERT_LT(new_profile, MEEM_block_config[MEEM_BLOCK_Block_MultiProfile_ID].instance_count);
    ASSERT_LE(InstanceOffset(source, new_profile + 1u), SourceOf(0, MEEM_BLOCK_Block_WearLeveling_ID)->offset_in_eeprom);
    StoreInstance(source, new_profile, GenerateRandomBytes(source->data_size));

    EXPECT_EQ(MEEM_ReadLegacyInstance(source, new_profile), nullptr);
}

TEST_F(LegacyReadersTest, MultiProfile_NullForAnInvalidProfile)
{
    LoadLegacyImage(0);
    const auto source   = SourceOf(0, MEEM_BLOCK_Block_MultiProfile_ID);
    const auto defaults = ParametersInEeprom(source, 0);

    CorruptInstance(source, 1);

    EXPECT_EQ(MEEM_ReadLegacyInstance(source, 1), nullptr);
    EXPECT_EQ(ReadParameters(source, 0), defaults);
}

/******************************************************************************/
/*    Wear-leveling                                                           */
/******************************************************************************/
TEST_F(LegacyReadersTest, WearLeveling_TheMostRecentInstanceWithoutItsSequenceCounter)
{
    LoadLegacyImage(0);
    const auto source = SourceOf(0, MEEM_BLOCK_Block_WearLeveling_ID);
    ASSERT_EQ(source->management_type, MEEM_MGMT_WEAR_LEVELING);
    ASSERT_EQ(source->instance_count, 5u);
    ASSERT_GT(source->instance_count, MEEM_block_config[MEEM_BLOCK_Block_WearLeveling_ID].instance_count) << "More instances than any current block";

    // The most recent instance is the last one, beyond the current block's instance count
    const auto instances = StoreWearLevelingInstances(source, {6, 7, 8, 9, 10});

    EXPECT_EQ(MEEM_ReadLegacyInstance(source, 0), &MEEM_work_buffer[sizeof(MEEM_checksum_t) + 1u]);
    EXPECT_EQ(ReadParameters(source, 0), ParametersOf(instances[4], source->management_type));
}

TEST_F(LegacyReadersTest, WearLeveling_TheMostRecentInstanceAcrossTheWrapAroundOfTheSequenceCounter)
{
    LoadLegacyImage(0);
    const auto source = SourceOf(0, MEEM_BLOCK_Block_WearLeveling_ID);

    // The sequence counter wraps around from 254 to 0
    const auto instances = StoreWearLevelingInstances(source, {253, 254, 0, 1, 252});

    EXPECT_EQ(ReadParameters(source, 0), ParametersOf(instances[3], source->management_type));
}

TEST_F(LegacyReadersTest, WearLeveling_SkipsAnInvalidMostRecentInstance)
{
    LoadLegacyImage(0);
    const auto source = SourceOf(0, MEEM_BLOCK_Block_WearLeveling_ID);

    const auto instances = StoreWearLevelingInstances(source, {6, 7, 8, 9, 10});
    CorruptInstance(source, 4);

    EXPECT_EQ(ReadParameters(source, 0), ParametersOf(instances[3], source->management_type));
}

TEST_F(LegacyReadersTest, WearLeveling_SkipsErasedInstances)
{
    // The image holds only the first instance, with sequence counter 0 and the defaults
    LoadLegacyImage(0);
    const auto source   = SourceOf(0, MEEM_BLOCK_Block_WearLeveling_ID);
    const auto defaults = ParametersInEeprom(source, 0);

    EXPECT_EQ(ReadParameters(source, 0), defaults);
}

TEST_F(LegacyReadersTest, WearLeveling_NullWhenNoInstanceIsValid)
{
    LoadLegacyImage(0);
    const auto source = SourceOf(0, MEEM_BLOCK_Block_WearLeveling_ID);

    StoreWearLevelingInstances(source, {6, 7, 8, 9, 10});
    for (uint8_t instance_index = 0; instance_index < source->instance_count; instance_index++)
    {
        CorruptInstance(source, instance_index);
    }

    EXPECT_EQ(MEEM_ReadLegacyInstance(source, 0), nullptr);
}

/******************************************************************************/
/*    Any management type                                                     */
/******************************************************************************/
TEST_F(LegacyReadersTest, TheSourceOfARoutine)
{
    // Block_Changed has another parameter list in legacy model 0. Its routine reads the source, the last legacy block: 'a' and 'b' with their defaults.
    LoadLegacyImage(0);
    const auto source = RoutineSourceOf(0, MEEM_BLOCK_Block_Changed_ID);
    ASSERT_EQ(source, &MEEM_legacy_models[0].blocks[4]);

    EXPECT_EQ(ReadParameters(source, 0), (std::vector<uint8_t>{97u, 98u}));
}

TEST_F(LegacyReadersTest, NullForEverySourceWhenTheEepromCantBeRead)
{
    for (uint8_t k = 0; k < MEEM_LEGACY_MODEL_COUNT; k++)
    {
        LoadLegacyImage(k);
        eep_sim->return_nok_for_next_jobs();

        for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
        {
            if (IsCopied(k, block_id))
            {
                SCOPED_TRACE("Legacy model " + std::to_string(k) + ", block ID " + std::to_string(block_id));
                FillCache(block_id, 0xEEu);
                const auto cache = SnapshotCache(block_id);

                EXPECT_EQ(MEEM_ReadLegacyInstance(SourceOf(k, block_id), 0), nullptr);
                EXPECT_FALSE(MEEM_CopyLegacyBlock(block_id, SourceOf(k, block_id), 0));
                EXPECT_EQ(SnapshotCache(block_id), cache);
            }
        }
        eep_sim->return_ok_for_next_jobs();
    }
}

/******************************************************************************/
/*    Copy into the cache                                                     */
/******************************************************************************/
TEST_F(LegacyReadersTest, Copy_EveryCopiedBlockFromTheImages)
{
    // Every management type on both sides, among them MultiProfile -> Basic (profile 0) and Basic -> MultiProfile
    for (uint8_t k = 0; k < MEEM_LEGACY_MODEL_COUNT; k++)
    {
        LoadLegacyImage(k);

        for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
        {
            if (IsCopied(k, block_id))
            {
                SCOPED_TRACE("Legacy model " + std::to_string(k) + ", block ID " + std::to_string(block_id));
                const auto source = SourceOf(k, block_id);

                // The image holds a valid first instance of every block, the defaults of its model
                const auto expected = ParametersInEeprom(source, 0);
                FillCache(block_id, 0xEEu);

                EXPECT_TRUE(MEEM_CopyLegacyBlock(block_id, source, 0));
                EXPECT_EQ(CachedParameters(block_id), expected);
            }
        }
    }
}

TEST_F(LegacyReadersTest, Copy_TheGivenProfileOfAMultiProfileSource)
{
    LoadLegacyImage(0);
    const auto block_id = MEEM_BLOCK_Block_MultiProfile_ID;
    const auto source   = SourceOf(0, block_id);
    ASSERT_EQ(source->management_type, MEEM_MGMT_MULTI_PROFILE);

    const auto profile_1 = GenerateRandomBytes(source->data_size);
    StoreInstance(source, 1, profile_1);

    EXPECT_TRUE(MEEM_CopyLegacyBlock(block_id, source, 1));
    EXPECT_EQ(SnapshotCache(block_id), profile_1);
}

TEST_F(LegacyReadersTest, Copy_ProfileZeroOfAMultiProfileSourceIntoABlockOfAnotherType)
{
    LoadLegacyImage(1);
    const auto block_id = MEEM_BLOCK_Block_Basic_ID;
    const auto source   = SourceOf(1, block_id);
    ASSERT_EQ(source->management_type, MEEM_MGMT_MULTI_PROFILE);
    ASSERT_EQ(MEEM_block_config[block_id].management_type, MEEM_MGMT_BASIC);

    const auto profile_0 = GenerateRandomBytes(source->data_size);
    StoreInstance(source, 0, profile_0);
    StoreInstance(source, 1, GenerateRandomBytes(source->data_size));

    EXPECT_TRUE(MEEM_CopyLegacyBlock(block_id, source, 0));
    EXPECT_EQ(SnapshotCache(block_id), profile_0);
}

TEST_F(LegacyReadersTest, Copy_TheWearLevelingCacheKeepsItsSequenceCounter)
{
    LoadLegacyImage(0);
    const auto block_id = MEEM_BLOCK_Block_WearLeveling_ID;
    const auto source   = SourceOf(0, block_id);

    const auto instances                 = StoreWearLevelingInstances(source, {6, 7, 8, 9, 10});
    MEEM_block_config[block_id].cache[0] = 0x42u;

    EXPECT_TRUE(MEEM_CopyLegacyBlock(block_id, source, 0));
    EXPECT_EQ(MEEM_block_config[block_id].cache[0], 0x42u);
    EXPECT_EQ(CachedParameters(block_id), ParametersOf(instances[4], source->management_type));
}

TEST_F(LegacyReadersTest, Copy_LeavesTheCacheUnchangedWithoutAValidSource)
{
    LoadLegacyImage(0);
    const auto block_id = MEEM_BLOCK_Block_Moved_ID;
    const auto source   = SourceOf(0, block_id);

    CorruptInstance(source, 0);
    FillCache(block_id, 0xEEu);
    const auto cache = SnapshotCache(block_id);

    EXPECT_FALSE(MEEM_CopyLegacyBlock(block_id, source, 0));
    EXPECT_EQ(SnapshotCache(block_id), cache);
}
