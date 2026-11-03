#include "test_base.hpp"
#include <array>

using testing::Return;

/*!
 * \brief Tests of the header check and the reset in MEEM_Init(): any stored header other than the current one resets
 *        every block, which the periodic task then rewrites, and the header written last commits the reset.
 */
class ResetTest : public CommittedEepromTest
{
  protected:
    /// @brief The configuration's image: the current header, then every block with its defaults
    std::vector<uint8_t> image;

  public:
    void SetUp() override
    {
        CommittedEepromTest::SetUp(); // Important: Call base class SetUp() first
        image = CreateEepromSnapshot();
        MEEM_DeInit();
    }

    /// @brief Restarts the device: the power is back, then MEEM_DeInit() and MEEM_Init()
    void Restart()
    {
        eep_sim->restore_power();
        MEEM_DeInit();
        MEEM_Init();
    }

    /// @brief Stores an intact identity header of another EEPROM layout identity: [CRC-16][identity], both little-endian
    void StoreHeaderOfAnotherDataModel()
    {
        std::array<uint8_t, 4> identity;
        std::copy_n(MEEM_identity_header + sizeof(MEEM_checksum_t), identity.size(), identity.begin());

        const uint16_t current_crc = ReferenceCrc16(identity.data(), identity.size());
        ASSERT_EQ(MEEM_identity_header[0] | (MEEM_identity_header[1] << 8), current_crc) << "The header isn't [CRC-16][identity]";

        identity[0] ^= 0x01;
        const uint16_t crc = ReferenceCrc16(identity.data(), identity.size());

        eep_sim->eeprom[0] = static_cast<uint8_t>(crc);
        eep_sim->eeprom[1] = static_cast<uint8_t>(crc >> 8);
        std::copy(identity.cbegin(), identity.cend(), eep_sim->eeprom.begin() + sizeof(MEEM_checksum_t));
    }

    /// @brief The defaults of a block, from the configuration's image, whose first instance holds them ('wear-leveling': with counter 0)
    std::vector<uint8_t> DefaultsFromImage(uint8_t block_id)
    {
        const auto begin = image.cbegin() + MEEM_block_config[block_id].offset_in_eeprom + sizeof(MEEM_checksum_t);
        return std::vector<uint8_t>(begin, begin + MEEM_block_config[block_id].data_size);
    }

    /// @brief The write jobs of a reset: every instance of every block, in the order of the round robin, then the header
    std::vector<EepromSimulator::WriteJob> ResetWriteJobs()
    {
        std::vector<EepromSimulator::WriteJob> jobs;

        for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
        {
            const auto block_cfg     = &MEEM_block_config[block_id];
            const auto instance_size = sizeof(MEEM_checksum_t) + block_cfg->data_size;

            for (uint8_t instance_id = 0; instance_id < block_cfg->instance_count; instance_id++)
            {
                jobs.push_back({block_cfg->offset_in_eeprom + (instance_id * instance_size), instance_size});
            }
        }
        jobs.push_back(HeaderWriteJob());
        return jobs;
    }

    /// @brief Expects MEEM_Init() to have reset every block: its defaults in the cache, 'recovered', a rewrite, and the commit pending
    void ExpectReset()
    {
        for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
        {
            SCOPED_TRACE("Block ID: " + std::to_string(block_id));
            const auto status   = MEEM_GetBlockStatus(block_id);
            const auto defaults = DefaultsFromImage(block_id);
            const auto cache    = SnapshotCache(block_id);
            const auto skip     = (MEEM_block_config[block_id].management_type == MEEM_MGMT_WEAR_LEVELING) ? 1 : 0; // The sequence counter

            EXPECT_TRUE(status.recovered);
            EXPECT_FALSE(status.migrated);
            EXPECT_TRUE(status.write_pending);
            EXPECT_TRUE(MEEM_block_status[block_id].rewrite_all);
            EXPECT_TRUE(std::equal(defaults.cbegin() + skip, defaults.cend(), cache.cbegin() + skip)) << "The cache doesn't hold the defaults";
        }
        EXPECT_TRUE(MEEM_global_status.identity_pending);
    }

    /// @brief Expects the reset to be committed: every instance of every block holds its defaults, and the current header is stored
    void ExpectCommittedReset()
    {
        for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
        {
            SCOPED_TRACE("Block ID: " + std::to_string(block_id));
            ExpectEveryInstanceHolds(block_id, DefaultsFromImage(block_id));
        }
        EXPECT_EQ(GetHeaderFromEeprom(), CurrentHeader());
        EXPECT_FALSE(MEEM_IsBusy());
    }

    /// @brief Expects the next init to find the committed blocks intact, as in v1
    void ExpectRestartWithoutRecovery()
    {
        Restart();

        for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
        {
            EXPECT_FALSE(MEEM_GetBlockStatus(block_id).recovered) << "Block #" << static_cast<int>(block_id);
        }
        EXPECT_FALSE(MEEM_IsBusy()) << "Nothing to repair or rewrite";
    }
};

/******************************************************************************/
/*    What the EEPROM holds                                                   */
/******************************************************************************/
/** EEPROM contents that don't start with the current identity header */
enum class StoredContent {
    Blank,
    Zeroed,
    OtherDataModel, /**< An image of a model the generator didn't get: a lost legacy model, a firmware downgrade, another product */
    TornHeader,     /**< The current image, but its header write was cut short over a blank EEPROM */
    V1Image         /**< v1 laid the blocks out from offset 0, so its first bytes are block data */
};

class ResetFromStoredContentTest : public ResetTest, public testing::WithParamInterface<StoredContent>
{
  public:
    void Store(StoredContent content)
    {
        switch (content)
        {
            case StoredContent::Blank:
                eep_sim->erase();
                break;

            case StoredContent::Zeroed:
                std::fill(eep_sim->eeprom.begin(), eep_sim->eeprom.end(), uint8_t{0x00});
                break;

            case StoredContent::OtherDataModel:
                StoreHeaderOfAnotherDataModel();
                break;

            case StoredContent::TornHeader:
                eep_sim->erase(sizeof(MEEM_identity_header) / 2, sizeof(MEEM_identity_header) / 2);
                break;

            case StoredContent::V1Image:
                std::copy(image.cbegin() + sizeof(MEEM_identity_header), image.cend(), eep_sim->eeprom.begin());
                eep_sim->erase(eep_sim->eeprom.size() - sizeof(MEEM_identity_header), sizeof(MEEM_identity_header));
                break;
        }
    }
};

INSTANTIATE_TEST_SUITE_P(Contents, ResetFromStoredContentTest,
                         testing::Values(StoredContent::Blank, StoredContent::Zeroed, StoredContent::OtherDataModel, StoredContent::TornHeader,
                                         StoredContent::V1Image),
                         [](const testing::TestParamInfo<StoredContent>& info)
                         {
                             switch (info.param)
                             {
                                 case StoredContent::Blank:
                                     return "Blank";
                                 case StoredContent::Zeroed:
                                     return "Zeroed";
                                 case StoredContent::OtherDataModel:
                                     return "OtherDataModel";
                                 case StoredContent::TornHeader:
                                     return "TornHeader";
                                 default:
                                     return "V1Image";
                             }
                         });

TEST_P(ResetFromStoredContentTest, ResetsEveryBlockAndCommits)
{
    Store(GetParam());
    ASSERT_NE(GetHeaderFromEeprom(), CurrentHeader());

    for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
    {
        EXPECT_CALL(user_callbacks_mock, OnBlockInitComplete(block_id)).Times(1);
    }

    MEEM_Init();
    ExpectReset();
    testing::Mock::VerifyAndClearExpectations(&user_callbacks_mock);

    ProcessMeemUntilIdle();
    EXPECT_EQ(eep_sim->write_log, ResetWriteJobs());
    ExpectCommittedReset();
    ExpectRestartWithoutRecovery();
}

/******************************************************************************/
/*    Current header                                                          */
/******************************************************************************/
TEST_F(ResetTest, InitFromACurrentImageWritesNothing)
{
    MEEM_Init();
    ProcessMeemUntilIdle();

    EXPECT_TRUE(eep_sim->write_log.empty());
    EXPECT_FALSE(MEEM_global_status.identity_pending);

    for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
    {
        EXPECT_FALSE(MEEM_GetBlockStatus(block_id).recovered) << "Block #" << static_cast<int>(block_id);
        EXPECT_FALSE(MEEM_block_status[block_id].rewrite_all) << "Block #" << static_cast<int>(block_id);
    }
}

/******************************************************************************/
/*    Reset                                                                   */
/******************************************************************************/
TEST_F(ResetTest, OldDataThatPassesItsCheckIsOverwritten)
{
    // Leftovers of another layout at the blocks' places: every instance passes its check
    for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
    {
        const auto block_cfg     = &MEEM_block_config[block_id];
        const auto instance_size = sizeof(MEEM_checksum_t) + block_cfg->data_size;

        for (uint8_t instance_id = 0; instance_id < block_cfg->instance_count; instance_id++)
        {
            const auto data     = GenerateRandomBytes(block_cfg->data_size);
            const auto instance = CreateInstanceImage(data.data(), data.size());
            std::copy(instance.cbegin(), instance.cend(), eep_sim->eeprom.begin() + block_cfg->offset_in_eeprom + (instance_id * instance_size));
        }
    }
    StoreHeaderOfAnotherDataModel();

    MEEM_Init();
    ExpectReset(); // Nothing is read: the caches hold the defaults

    ProcessMeemUntilIdle();
    ExpectCommittedReset();
}

TEST_F(ResetTest, ResetSelectsTheInitiallyActiveProfile)
{
    const auto mp_blocks_ids = FilterBlocksByManagementType(MEEM_MGMT_MULTI_PROFILE);
    ASSERT_FALSE(mp_blocks_ids.empty()) << "Your test configuration needs at least 1 multi-profile block";

    for (auto block_id : mp_blocks_ids)
    {
        EXPECT_CALL(user_callbacks_mock, SelectInitiallyActiveProfile(block_id)).WillOnce(Return(MEEM_block_config[block_id].instance_count - 1));
    }
    eep_sim->erase();

    MEEM_Init();
    testing::Mock::VerifyAndClearExpectations(&user_callbacks_mock);
    ProcessMeemUntilIdle();

    for (auto block_id : mp_blocks_ids)
    {
        EXPECT_EQ(MEEM_GetActiveProfile(block_id), MEEM_block_config[block_id].instance_count - 1) << "Block #" << static_cast<int>(block_id);
    }
    ExpectCommittedReset();
}

/******************************************************************************/
/*    Failures                                                                */
/******************************************************************************/
TEST_F(ResetTest, PowerCutDuringTheResetResetsAgain)
{
    const auto jobs = ResetWriteJobs();

    // Every write job of the reset, the header write last: a torn header matches no model either
    for (size_t job = 1; job <= jobs.size(); job++)
    {
        SCOPED_TRACE("Power cut during write job " + std::to_string(job) + " of " + std::to_string(jobs.size()));
        eep_sim->erase();
        Restart();

        eep_sim->cut_power_during_write(static_cast<unsigned>(job), jobs[job - 1].size / 2);
        ProcessMeemUntilIdle();
        ASSERT_FALSE(eep_sim->is_powered());
        ASSERT_NE(GetHeaderFromEeprom(), CurrentHeader());

        Restart();
        ExpectReset();
        ProcessMeemUntilIdle();
        ExpectCommittedReset();
    }
}

TEST_F(ResetTest, DeInitBeforeTheCommitResetsAgain)
{
    eep_sim->erase();
    MEEM_Init();

    // Some of the rewrites, but not the header
    for (int tick = 0; tick < 5; tick++)
    {
        MEEM_PeriodicTask();
    }
    ASSERT_TRUE(MEEM_global_status.identity_pending);

    Restart(); // Against the documented condition of MEEM_DeInit()
    ExpectReset();
    ProcessMeemUntilIdle();
    ExpectCommittedReset();
}

TEST_F(ResetTest, FailedHeaderReadInitializesAsInV1)
{
    // Another model's header, which init can't see when the driver fails
    StoreHeaderOfAnotherDataModel();
    const auto stored_header = GetHeaderFromEeprom();

    eep_sim->return_nok_for_next_jobs(); // Simulate driver failure
    MEEM_Init();
    eep_sim->return_ok_for_next_jobs();

    // v1: every block is unreadable and recovered, and only the strategy decides about a repair
    for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
    {
        SCOPED_TRACE("Block ID: " + std::to_string(block_id));
        const bool repair = (MEEM_block_config[block_id].data_recovery_strategy == MEEM_RECOVER_DEFAULTS_AND_REPAIR);

        EXPECT_TRUE(MEEM_GetBlockStatus(block_id).recovered);
        EXPECT_EQ(static_cast<bool>(MEEM_GetBlockStatus(block_id).write_pending), repair);
        EXPECT_FALSE(MEEM_block_status[block_id].rewrite_all);
    }
    EXPECT_FALSE(MEEM_global_status.identity_pending);

    ProcessMeemUntilIdle();
    EXPECT_EQ(std::count(eep_sim->write_log.cbegin(), eep_sim->write_log.cend(), HeaderWriteJob()), 0);
    EXPECT_EQ(GetHeaderFromEeprom(), stored_header) << "The header was touched";
}
