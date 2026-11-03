#include "test_legacy_base.hpp"
#include <map>

/*!
 * \brief A migration example end to end, on the neutral model of the footprint harness, in the
 *        -MigrationExample configuration: Block09 changes as a production application's ConfigCommon does
 *        (test/footprint/production_like/eeprom_datamodel_migration.json), and the unchanged model is its legacy model. MEEM_Init()
 *        initializes the 8 blocks in front of Block09 as in v1, converts Block09 with the generated routine, and copies the 3 blocks behind
 *        it from their old places. The periodic task then rewrites 209 B and writes the current header last.
 */
class MigrationExampleTest : public MigrationTestBase
{
  public:
#pragma pack(push, 1)
    /// @brief The parameters of Block09 in the legacy model
    struct LegacyBlock09 {
        uint8_t  p01;
        uint8_t  p02;
        uint16_t p03;
        uint16_t p04;
        uint16_t p05;
    };
#pragma pack(pop)

    /// @brief The blocks behind Block09, which move by the 4 bytes that it grows
    static inline const std::vector<uint8_t> moved{MEEM_BLOCK_Block10_ID, MEEM_BLOCK_Block11_ID, MEEM_BLOCK_Block12_ID};

    /// @brief What the legacy image holds besides the defaults
    struct LegacyData {
        std::vector<std::vector<uint8_t>>       unchanged;        /**< The instance of each block in front of Block09: [checksum][data] */
        LegacyBlock09                           block09;          /**< The parameters of Block09 */
        std::map<uint8_t, std::vector<uint8_t>> moved_parameters; /**< The parameters of each moved block */
    };

    void SetUp() override
    {
        MigrationTestBase::SetUp(); // Important: Call base class SetUp() first
        LoadLegacyImage(0);
    }

    /// @brief Stores data in every block of the legacy image
    LegacyData StoreLegacyData()
    {
        LegacyData data{};

        // The blocks in front of Block09 have the same places in both models
        for (uint8_t block_id = 0; block_id < MEEM_BLOCK_Block09_ID; block_id++)
        {
            data.unchanged.push_back(StoreValidFirstInstance(block_id));
        }

        data.block09     = {0x12u, 0x34u, 0x5678u, 0xABCDu, 0x1357u};
        const auto bytes = reinterpret_cast<const uint8_t*>(&data.block09);
        StoreInstance(RoutineSourceOf(0, MEEM_BLOCK_Block09_ID), 0, std::vector<uint8_t>(bytes, bytes + sizeof(data.block09)));

        for (const auto block_id : moved)
        {
            data.moved_parameters[block_id] = StoreSourceParameters(SourceOf(0, block_id));
        }
        return data;
    }

    /// @brief Expects the caches to hold what the migration makes of the legacy data
    void ExpectMigratedCaches(const LegacyData& data)
    {
        for (uint8_t block_id = 0; block_id < MEEM_BLOCK_Block09_ID; block_id++)
        {
            EXPECT_EQ(SnapshotCache(block_id),
                      std::vector<uint8_t>(data.unchanged[block_id].cbegin() + sizeof(MEEM_checksum_t), data.unchanged[block_id].cend()))
                << "Block ID " << int(block_id);
        }

        // p02 is dropped, p04 widens to uint32, p06 and p07 are new
        EXPECT_EQ(static_cast<uint8_t>(MEEM_cache_Block09.p01), 0x12u);
        EXPECT_EQ(static_cast<uint16_t>(MEEM_cache_Block09.p06), 20u) << "Its default";
        EXPECT_EQ(static_cast<uint16_t>(MEEM_cache_Block09.p03), 0x5678u);
        EXPECT_EQ(static_cast<uint32_t>(MEEM_cache_Block09.p04), 0xABCDu);
        EXPECT_EQ(static_cast<uint16_t>(MEEM_cache_Block09.p05), 0x1357u);
        EXPECT_EQ(static_cast<uint8_t>(MEEM_cache_Block09.p07), 128u) << "Its default";

        for (const auto& [block_id, parameters] : data.moved_parameters)
        {
            EXPECT_EQ(CachedParameters(block_id), parameters) << "Block ID " << int(block_id);
        }
    }
};

TEST_F(MigrationExampleTest, LegacyModelIsTheUnchangedNeutralModel)
{
    // The golden header of test/vectors/identity_vectors.json, which the legacy image starts with
    const std::vector<uint8_t> header{0x91u, 0x1Fu, 0x86u, 0x77u, 0x98u, 0x6Cu};
    ASSERT_EQ(MEEM_LEGACY_MODEL_COUNT, 1u);
    EXPECT_EQ(std::vector<uint8_t>(MEEM_legacy_models[0].identity_header, MEEM_legacy_models[0].identity_header + sizeof(MEEM_identity_header)), header);
    EXPECT_EQ(GetHeaderFromEeprom(), header);
    EXPECT_NE(CurrentHeader(), header);

    for (uint8_t block_id = 0; block_id < MEEM_BLOCK_Block09_ID; block_id++)
    {
        EXPECT_EQ(MEEM_legacy_models[0].sources[block_id], MEEM_SOURCE_UNCHANGED) << "Block ID " << int(block_id);
    }
    EXPECT_EQ(MEEM_legacy_models[0].sources[MEEM_BLOCK_Block09_ID], MEEM_SOURCE_ROUTINE);

    // The offsets of the blocks behind Block09, in the legacy model and in the current one
    EXPECT_EQ(RoutineSourceOf(0, MEEM_BLOCK_Block09_ID)->offset_in_eeprom, 450u);
    EXPECT_EQ(MEEM_block_config[MEEM_BLOCK_Block09_ID].offset_in_eeprom, 450u);
    const std::vector<uint16_t> old_offsets{460u, 501u, 519u};
    const std::vector<uint16_t> new_offsets{464u, 505u, 523u};
    for (size_t i = 0; i < moved.size(); i++)
    {
        EXPECT_EQ(SourceOf(0, moved[i])->offset_in_eeprom, old_offsets[i]) << "Block ID " << int(moved[i]);
        EXPECT_EQ(MEEM_block_config[moved[i]].offset_in_eeprom, new_offsets[i]) << "Block ID " << int(moved[i]);
    }
    EXPECT_EQ(MEEM_USED_EEPROM_BYTES, 659u);
}

TEST_F(MigrationExampleTest, InitMigratesBlock09AndTheMovedBlocks)
{
    const auto data = StoreLegacyData();

    MEEM_Init();

    EXPECT_TRUE(eep_sim->write_log.empty()) << "Pass 1 only reads, and the model has no pass 2";
    EXPECT_TRUE(MEEM_global_status.identity_pending);
    ExpectMigratedCaches(data);

    for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
    {
        SCOPED_TRACE("Block ID " + std::to_string(block_id));
        const bool changed = (block_id >= MEEM_BLOCK_Block09_ID);
        const auto status  = MEEM_GetBlockStatus(block_id);

        EXPECT_EQ(bool(status.migrated), changed);
        EXPECT_FALSE(status.recovered);
        EXPECT_EQ(bool(status.write_pending), changed) << "A rewrite";
    }
}

TEST_F(MigrationExampleTest, RewritesOf209BytesThenTheHeader)
{
    const auto data = StoreLegacyData();
    MEEM_Init();

    ProcessMeemUntilIdle();

    // Block09 (14 B), Block10 (41 B), Block11 (18 B) and the 4 instances of Block12 (4 x 34 B), then the header
    const auto expected_jobs = RewriteAndCommitJobs({MEEM_BLOCK_Block09_ID, MEEM_BLOCK_Block10_ID, MEEM_BLOCK_Block11_ID, MEEM_BLOCK_Block12_ID});
    EXPECT_EQ(eep_sim->write_log, expected_jobs);

    size_t rewritten = 0;
    for (size_t job = 0; (job + 1) < eep_sim->write_log.size(); job++)
    {
        rewritten += eep_sim->write_log[job].size;
    }
    EXPECT_EQ(rewritten, 209u);
    ASSERT_FALSE(eep_sim->write_log.empty());
    EXPECT_EQ(eep_sim->write_log.back(), HeaderWriteJob());
    EXPECT_EQ(GetHeaderFromEeprom(), CurrentHeader());

    // The next init finds the current header and every block valid, at its new place
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
    ExpectMigratedCaches(data);
}
