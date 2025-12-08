#include "test_base.hpp"

class BackupCopyBlockTest : public TestBase
{
  public:
    BackupCopyBlockTest()
    {
        // You can do set-up work for each test here.
    }

    ~BackupCopyBlockTest() override
    {
        // You can do clean-up work that doesn't throw exceptions here.
    }

    // If the constructor and destructor are not enough for setting up
    // and cleaning up each test, you can define the following methods:

    void SetUp() override {}

    void TearDown() override
    {
        // Code here will be called immediately after each test (right before the destructor).
    }

    bool BothInstancesIdenticalInEeeprom(uint8_t block_id)
    {
        auto       block_cfg               = &MEEM_block_config[block_id];
        const auto instance_size_in_eeprom = (block_cfg->data_size + sizeof(MEEM_checksum_t));

        return std::equal(eep_sim->eeprom.begin() + block_cfg->offset_in_eeprom,
                          eep_sim->eeprom.begin() + block_cfg->offset_in_eeprom + instance_size_in_eeprom,
                          eep_sim->eeprom.begin() + block_cfg->offset_in_eeprom + instance_size_in_eeprom);
    }

    void InitFromJustOneValidInstance(uint8_t block_id, uint8_t instance_id)
    {
        const auto block_cfg = &MEEM_block_config[block_id];

        MEEM_Init();
        ProcessMeemUntilIdle();

        // Generate test random data
        auto rnd_data = GenerateRandomBytes(block_cfg->data_size);

        // Write random data to the EEPROM
        std::copy(rnd_data.cbegin(), rnd_data.cend(), block_cfg->cache);
        MEEM_Resume();
        MEEM_InitiateBlockWrite(block_id);
        ProcessMeemUntilIdle();

        // Corrupt one instance
        CorruptInstanceInEeprom(block_id, instance_id);

        // Re-init
        MEEM_DeInit();
        MEEM_Init();

        // Should initialize without errors
        EXPECT_FALSE(MEEM_GetBlockStatus(block_id).recovered);

        // Expect the cache to contain initially generated random data immediately after init
        EXPECT_TRUE(std::equal(rnd_data.begin(), rnd_data.begin() + block_cfg->data_size, block_cfg->cache));

        ProcessMeemUntilIdle();

        // Expect the block's EEPROM area to be repaired
        EXPECT_TRUE(BothInstancesIdenticalInEeeprom(block_id));
    }
};

TEST_F(BackupCopyBlockTest, InitFromJustOneValidInstance)
{
    std::vector<uint8_t> backup_blocks_ids = FilterBlocksByManagementType(MEEM_MGMT_BACKUP_COPY);
    for (auto block_id : backup_blocks_ids)
    {
        InitFromJustOneValidInstance(block_id, 0);
        InitFromJustOneValidInstance(block_id, 1);
    }
}

TEST_F(BackupCopyBlockTest, EnsureBothInstancesWritten)
{
    MEEM_Init();
    ProcessMeemUntilIdle();
    MEEM_Resume();

    std::vector<uint8_t> backup_blocks_ids = FilterBlocksByManagementType(MEEM_MGMT_BACKUP_COPY);

    for (auto block_id : backup_blocks_ids)
    {
        auto eeprom_before_change = CreateEepromSnapshot();

        ChangeAllDataInBlock(block_id);
        MEEM_InitiateBlockWrite(block_id);
        ProcessMeemUntilIdle();

        EXPECT_TRUE(IsOwnAreaWrittenOnly(block_id, eeprom_before_change));
        EXPECT_TRUE(BothInstancesIdenticalInEeeprom(block_id));
    }
}
