#include "test_legacy_base.hpp"
#include <fstream>

/*!
 * \brief Tests of the legacy configurations in the generated MEEM_Migration.c, in the migration configuration:
 *        against the images that the image generator builds of the legacy models, and against what the fixture models
 *        test/meem_config/eeprom_datamodel_migration*.json are made to cover.
 */
class LegacyTablesTest : public LegacyTestBase
{
  public:
    static std::vector<uint8_t> ReadHeader(const std::string& image_file)
    {
        std::vector<uint8_t> header(sizeof(MEEM_identity_header));
        std::ifstream        image(image_file, std::ios::binary);

        image.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
        EXPECT_TRUE(image) << "Can't read " << image_file;
        return header;
    }
};

TEST_F(LegacyTablesTest, EachLegacyImageStartsWithTheHeaderOfItsModel)
{
    const auto files = LegacyImageFiles();
    ASSERT_EQ(files.size(), MEEM_LEGACY_MODEL_COUNT);

    for (uint8_t k = 0; k < MEEM_LEGACY_MODEL_COUNT; k++)
    {
        SCOPED_TRACE("Legacy model " + std::to_string(k));
        const auto& model_header = MEEM_legacy_models[k].identity_header;

        EXPECT_EQ(ReadHeader(files[k]), std::vector<uint8_t>(model_header, model_header + sizeof(model_header)));
        EXPECT_NE(ReadHeader(files[k]), CurrentHeader());
    }
}

TEST_F(LegacyTablesTest, CopiedBlocksHaveTheSameParameters)
{
    for (uint8_t k = 0; k < MEEM_LEGACY_MODEL_COUNT; k++)
    {
        for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
        {
            SCOPED_TRACE("Legacy model " + std::to_string(k) + ", block ID " + std::to_string(block_id));
            const uint8_t source = MEEM_legacy_models[k].sources[block_id];

            if (source <= 0xFCu)
            {
                const auto& legacy_block = MEEM_legacy_models[k].blocks[source];
                const auto& block_cfg    = MEEM_block_config[block_id];

                EXPECT_EQ(ParameterSize(legacy_block.data_size, legacy_block.management_type), ParameterSize(block_cfg.data_size, block_cfg.management_type));
                EXPECT_LE(sizeof(MEEM_checksum_t) + legacy_block.data_size, MEEM_WORKBUFFER_SIZE);

                if (legacy_block.management_type == MEEM_MGMT_WEAR_LEVELING)
                {
                    EXPECT_LE(legacy_block.instance_count, MEEM_MAX_WL_INSTANCE_COUNT);
                }
            }
        }
    }
}

TEST_F(LegacyTablesTest, SourceTablesOfTheFixtures)
{
    ASSERT_EQ(MEEM_LEGACY_MODEL_COUNT, 3u);

    // Legacy model 0, with the current platform settings: one block unchanged, copies of every management type, a routine
    const std::vector<uint8_t> expected_0{MEEM_SOURCE_UNCHANGED, 0u, 1u, 2u, 3u, MEEM_SOURCE_ROUTINE, MEEM_SOURCE_NONE, MEEM_SOURCE_NONE};
    // Legacy model 1, without page alignment: every block moved
    const std::vector<uint8_t> expected_1{0u, 1u, 2u, 3u, 4u, MEEM_SOURCE_ROUTINE, MEEM_SOURCE_NONE, MEEM_SOURCE_NONE};
    // Legacy model 2, without page alignment: only the multi-profile blocks, Block_MultiProfile with another parameter list
    const std::vector<uint8_t> expected_2{MEEM_SOURCE_NONE, MEEM_SOURCE_NONE, MEEM_SOURCE_NONE, MEEM_SOURCE_ROUTINE,
                                          MEEM_SOURCE_NONE, MEEM_SOURCE_NONE, MEEM_SOURCE_NONE, 1u};

    ASSERT_EQ(MEEM_BLOCK_COUNT, expected_0.size());
    EXPECT_EQ(std::vector<uint8_t>(MEEM_legacy_models[0].sources, MEEM_legacy_models[0].sources + MEEM_BLOCK_COUNT), expected_0);
    EXPECT_EQ(std::vector<uint8_t>(MEEM_legacy_models[1].sources, MEEM_legacy_models[1].sources + MEEM_BLOCK_COUNT), expected_1);
    EXPECT_EQ(std::vector<uint8_t>(MEEM_legacy_models[2].sources, MEEM_legacy_models[2].sources + MEEM_BLOCK_COUNT), expected_2);

    // Block_Basic is a backup copy block in legacy model 0, which only a legacy source needs the reader of
    EXPECT_EQ(MEEM_legacy_models[0].blocks[MEEM_legacy_models[0].sources[MEEM_BLOCK_Block_Basic_ID]].management_type, MEEM_MGMT_BACKUP_COPY);
}

TEST_F(LegacyTablesTest, PassTwoOrderFollowsTheSourceTables)
{
    // Block_MultiProfile has 2 profiles in legacy model 0, and moves down without overlapping them: from profile 0
    ASSERT_TRUE(MEEM_LEGACY_USING_PASS_2);
    const uint8_t* order_0 = &MEEM_legacy_models[0].sources[MEEM_BLOCK_COUNT];
    EXPECT_EQ(std::vector<uint8_t>(order_0, order_0 + 3), (std::vector<uint8_t>{1u, MEEM_BLOCK_Block_MultiProfile_ID, MEEM_PASS2_ASCENDING}));

    // In legacy model 1, Block_MultiProfile is a basic block, which pass 1 fills
    EXPECT_EQ(MEEM_legacy_models[1].sources[MEEM_BLOCK_COUNT], 0u);

    // In legacy model 2, Block_Profiles lies in the new place of Block_MultiProfile, which goes after it, and Block_MultiProfile moves up
    // into its own old place: from the highest profile down
    const uint8_t* order_2 = &MEEM_legacy_models[2].sources[MEEM_BLOCK_COUNT];
    EXPECT_EQ(std::vector<uint8_t>(order_2, order_2 + 5),
              (std::vector<uint8_t>{2u, MEEM_BLOCK_Block_Profiles_ID, MEEM_PASS2_ASCENDING, MEEM_BLOCK_Block_MultiProfile_ID, MEEM_PASS2_DESCENDING}));
}

TEST_F(LegacyTablesTest, SizesCoverTheLegacySourcesThatAreRead)
{
    // Block_WearLeveling has 5 instances in legacy model 0, the current one 3
    EXPECT_EQ(MEEM_MAX_WL_INSTANCE_COUNT, 5);

    // The largest instance that is read has 9 B of data, Block_WearLeveling's. Legacy model 0's removed Block_Removed, 20 B, is never read.
    EXPECT_EQ(MEEM_WORKBUFFER_SIZE, 11);

    // The readers of every management type, while the current model has no backup copy block
    EXPECT_TRUE(MEEM_LEGACY_USING_BASIC_BLOCKS);
    EXPECT_TRUE(MEEM_LEGACY_USING_BACKUP_COPY_BLOCKS);
    EXPECT_TRUE(MEEM_LEGACY_USING_MULTI_PROFILE_BLOCKS);
    EXPECT_TRUE(MEEM_LEGACY_USING_WEAR_LEVELING_BLOCKS);
    EXPECT_FALSE(MEEM_USING_BACKUP_COPY_BLOCKS);
}
