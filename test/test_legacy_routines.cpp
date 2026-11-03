#include "test_legacy_base.hpp"

/*!
 * \brief Tests of the generated dispatcher MEEM_MigrateBlock(), in the migration configuration, whose legacy models
 *        each have another parameter list of Block_Changed and so a routine of their own. The conversions themselves are tested in the
 *        conversions configuration (test_conversions.cpp).
 */
class LegacyRoutinesTest : public LegacyTestBase
{
  public:
    void SetUp() override
    {
        LegacyTestBase::SetUp(); // Important: Call base class SetUp() first
        MEEM_DeInit();
    }

    /// @brief Restores the block's defaults, as the core does, and migrates it
    static bool Migrate(uint8_t block_id, uint8_t k)
    {
        MEEM_RestoreDefaults(block_id);
        return MEEM_MigrateBlock(k, block_id, 0);
    }
};

TEST_F(LegacyRoutinesTest, EachLegacyModelHasItsOwnRoutine)
{
    // Legacy model 0: 'a' and 'b' as uint8. 'b' widens to uint16.
    LoadLegacyImage(0);
    StoreInstance(RoutineSourceOf(0, MEEM_BLOCK_Block_Changed_ID), 0, {0x11u, 0xF2u});

    ASSERT_TRUE(Migrate(MEEM_BLOCK_Block_Changed_ID, 0));
    EXPECT_EQ(static_cast<uint8_t>(MEEM_cache_Block_Changed.a), 0x11u);
    EXPECT_EQ(static_cast<uint16_t>(MEEM_cache_Block_Changed.b), 0x00F2u);

    // Legacy model 1: only 'a'. 'b' keeps its default.
    LoadLegacyImage(1);
    StoreInstance(RoutineSourceOf(1, MEEM_BLOCK_Block_Changed_ID), 0, {0x22u});

    ASSERT_TRUE(Migrate(MEEM_BLOCK_Block_Changed_ID, 1));
    EXPECT_EQ(static_cast<uint8_t>(MEEM_cache_Block_Changed.a), 0x22u);
    EXPECT_EQ(static_cast<uint16_t>(MEEM_cache_Block_Changed.b), 25187u) << "The default of test/meem_config/eeprom_datamodel_migration.json";
}

TEST_F(LegacyRoutinesTest, FalseWithoutAValidSource)
{
    LoadLegacyImage(0);
    CorruptInstance(RoutineSourceOf(0, MEEM_BLOCK_Block_Changed_ID), 0);

    EXPECT_FALSE(Migrate(MEEM_BLOCK_Block_Changed_ID, 0));
}

TEST_F(LegacyRoutinesTest, FalseForABlockWithoutARoutine)
{
    // The core copies these blocks, or gives them their defaults: the dispatcher has no case for them
    for (uint8_t k = 0; k < MEEM_LEGACY_MODEL_COUNT; k++)
    {
        LoadLegacyImage(k);

        for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
        {
            if (MEEM_legacy_models[k].sources[block_id] != MEEM_SOURCE_ROUTINE)
            {
                EXPECT_FALSE(Migrate(block_id, k)) << "Legacy model " << int(k) << ", block ID " << int(block_id);
            }
        }
    }
}
