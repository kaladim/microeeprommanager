#include "test_base.hpp"

template <size_t N>
struct TestData
{
    std::array<uint8_t, N> sequence_counters;
    uint8_t expected_most_recent_index;
};

class WearLevelingBlocksTest : public TestBase
{
public:
    WearLevelingBlocksTest()
    {
        // You can do set-up work for each test here.
    }

    ~WearLevelingBlocksTest() override
    {
        // You can do clean-up work that doesn't throw exceptions here.
    }

    // If the constructor and destructor are not enough for setting up
    // and cleaning up each test, you can define the following methods:

    void SetUp() override
    {
    }

    void TearDown() override
    {
        // Code here will be called immediately after each test (right before the destructor).
    }

    /// @brief Verifies the initialization happens always from most recent valid instance.
    /// @param block_id
    /// @param write_cycles_count number of write cycles to perform in advance
    void InitFromMostRecentlValidInstance(uint8_t block_id, size_t write_cycles_count)
    {
        assert(write_cycles_count > 0);

        const auto block_config = &MEEM_block_config[block_id];
        uint8_t *data_cache = &block_config->cache[1];                 // [0] is the sequence counter, we avoid it!
        std::vector<uint8_t> random_data(block_config->data_size - 1); // Skip the sequence counter!

        MEEM_Init();
        MEEM_Resume();

        for (auto i = 0; i < write_cycles_count; i++)
        {
            FillWithRandomBytes(random_data);
            std::copy(random_data.cbegin(), random_data.cend(), data_cache);
            MEEM_InitiateBlockWrite(block_id);
            ProcessMeemUntilIdle();
        }

        MEEM_DeInit();
        MEEM_Init();
        EXPECT_TRUE(std::equal(random_data.begin(), random_data.end(), data_cache));

        // std::cout << "Random data:" << std::endl;
        // std::cout << ToHexString(random_data.data(), random_data.size()) << std::endl;
        // std::cout << "Block cache:" << std::endl;
        // std::cout << ToHexString(data_cache, block_config->data_size - 1) << std::endl;
        // std::cout << "EEPROM:" << std::endl;
        // std::cout << ToHexString(eep_sim->eeprom.data(), eep_sim->eeprom.size()) << std::endl;
    }

    /// @brief Tests directly the search algorithm on array of sequence counters.
    /// @tparam N
    /// @tparam InstanceCount
    /// @param block_id
    /// @param test_data
    template <size_t InstanceCount, size_t N>
    void FindingMostRecentValidInstance(uint8_t block_id, const std::array<TestData<InstanceCount>, N> &test_data)
    {
        for (const auto &row : test_data)
        {
            EXPECT_EQ(row.expected_most_recent_index, MEEM_FindIndexOfMostRecentInstance(row.sequence_counters.data(), MEEM_block_config[block_id].instance_count));
        }
    }
};

TEST_F(WearLevelingBlocksTest, InitFromBlankEeprom)
{
    std::vector<uint8_t> wl_blocks_ids = FilterBlocksByManagementType(MEEM_MGMT_WEAR_LEVELING);
    for (auto block_id : wl_blocks_ids)
    {
        eep_sim->erase();
        MEEM_Init();
        EXPECT_EQ(MEEM_block_status[block_id].index_of_active_instance, 0);
    }
}

TEST_F(WearLevelingBlocksTest, MostRecentValidInstanceNormalWay)
{
    auto wl_blocks_ids = FilterBlocksByManagementType(MEEM_MGMT_WEAR_LEVELING);

    for (auto block_id : wl_blocks_ids)
    {
        constexpr std::array<size_t, 20> write_cycles_count{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 254, 255, 256, 257};
        for (auto i = 0; i < write_cycles_count.size(); i++)
        {
            InitFromMostRecentlValidInstance(block_id, write_cycles_count[i]);
        }
    }
}

TEST_F(WearLevelingBlocksTest, FindingMostRecentValidInstanceAmongMinimumAmountOfInstances)
{
    constexpr std::array<TestData<2>, 7> test_data_2_instances{{{{0xff, 0xff}, 0xff}, // No valid index at all
                                                                {{0x00, 0xff}, 0},
                                                                {{0x00, 0x01}, 1},
                                                                {{0xff, 0x01}, 1},
                                                                {{0xFD, 0xFE}, 1},
                                                                {{0xFE, 0x00}, 1},
                                                                {{0xFE, 0xff}, 0}}};

    const auto wl_blocks_ids = FilterBlocksByManagementTypeAndInstanceCount(MEEM_MGMT_WEAR_LEVELING, 2);
    if (wl_blocks_ids.size() == 0)
    {
        throw std::runtime_error("Your test configuration doesn't contain WL blocks with 2 instances. But it needs at least 1!");
    }
    FindingMostRecentValidInstance(wl_blocks_ids[0], test_data_2_instances);
}

TEST_F(WearLevelingBlocksTest, FindingMostRecentValidInstanceAmongMaximumAmountOfInstances)
{
    constexpr std::array<TestData<15>, 15> test_data_15_instances{{
        {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}, 0xff}, // No valid index
        {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x55}, 14},
        {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}, 6},
        {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xFE, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}, 7},
        {{0xff, 0x01, 0x02, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}, 2},
        {{0xff, 0xff, 0x02, 0x03, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}, 3},
        {{0xff, 0xff, 0x02, 0x03, 0xff, 0xff, 0xff, 0x07, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}, 7},
        {{0x16, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0xff, 0x12, 0xff, 0xff, 0x15}, 0},
        {{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E}, 14},
        {{0x0E, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D}, 0},
        {{0x16, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15}, 0},
        {{0xff, 0xff, 0xff, 0x00, 0xFE, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}, 3}, // Gaps with heavily skipped increments
        {{0xff, 0xff, 0xAA, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xff, 0xFB, 0xFC, 0xFD, 0xFE, 0xff}, 2}, // Gaps with heavily skipped increments
        {{0x01, 0x02, 0x03, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xff, 0xFB, 0xFC, 0xFD, 0xFE, 0x00}, 2}, // Gaps with skipped increments
        {{0xff, 0x00, 0x01, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xff, 0xFB, 0xFC, 0xFD, 0xFE, 0xff}, 2}  // Gaps with skipped increments
    }};

    const auto wl_blocks_ids = FilterBlocksByManagementTypeAndInstanceCount(MEEM_MGMT_WEAR_LEVELING, 15);
    if (wl_blocks_ids.size() == 0)
    {
        throw std::runtime_error("Your test configuration doesn't contain WL blocks with 15 instances. But it needs at least 1!");
    }
    FindingMostRecentValidInstance(wl_blocks_ids[0], test_data_15_instances);
}
