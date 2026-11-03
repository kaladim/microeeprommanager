#include "test_base.hpp"
#include "MEEM_CRCIF_Mock.hpp"
#include <string>

using testing::_;
using testing::AtLeast;
using testing::DoDefault;
using testing::Return;

/*!
 * \brief Tests of the checksum calculation on the MCU's CRC unit ('crc_implementation' 'hardware'), whose driver is a mock.
 * \note  The tests are built without NDEBUG, so every MEEM_Init() begins with the self-test of the CRC unit in MEEM_ValidateConfiguration().
 */
class HardwareCrcTest : public CommittedEepromTest
{
  protected:
    testing::NiceMock<MockCRCIF> crc_unit{};

  public:
    void SetUp() override
    {
        CommittedEepromTest::SetUp(); // Important: Call base class SetUp() first
        MockCRCIF::instance = &crc_unit;
    }

    void TearDown() override
    {
        MockCRCIF::instance = nullptr;
        TestBase::TearDown(); // Important: Call base class TearDown()
    }

    /// @brief Initializes the mEEM, and stores changed data with valid checksums in every block
    void InitAndStoreAllBlocks()
    {
        MEEM_DeInit();
        MEEM_Init();
        ProcessMeemUntilIdle();
        MEEM_Resume();

        for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
        {
            ChangeAllDataInBlock(block_id);
            ASSERT_TRUE(MEEM_InitiateBlockWrite(block_id));
        }
        ProcessMeemUntilIdle();
    }

    std::vector<std::vector<uint8_t>> SnapshotCaches()
    {
        std::vector<std::vector<uint8_t>> caches;

        for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
        {
            const auto block_cfg = &MEEM_block_config[block_id];
            caches.emplace_back(block_cfg->cache, block_cfg->cache + block_cfg->data_size);
        }
        return caches;
    }

    /// @brief Expects every block to be initialized from the EEPROM, with the given cache contents
    void ExpectAllBlocksInitializedWith(const std::vector<std::vector<uint8_t>>& caches)
    {
        EXPECT_TRUE(caches == SnapshotCaches());

        for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
        {
            EXPECT_FALSE(MEEM_GetBlockStatus(block_id).recovered) << "Block #" << static_cast<int>(block_id);
        }
    }
};

using HardwareCrcDeathTest = HardwareCrcTest;

/******************************************************************************/
/*    Self-test                                                               */
/******************************************************************************/
TEST_F(HardwareCrcTest, SelfTestRunsTheCheckInputThroughTheUnit)
{
    EXPECT_CALL(crc_unit, BeginCalculation(_, 9))
        .WillOnce(
            [this](const uint8_t* data, uint16_t size)
            {
                EXPECT_EQ(std::string(reinterpret_cast<const char*>(data), size), "123456789");
                return crc_unit.model.BeginCalculation(data, size);
            });
    EXPECT_CALL(crc_unit, GetResult(_)).Times(1);

    MEEM_ValidateConfiguration();
}

TEST_F(HardwareCrcTest, SelfTestWaitsForABusyUnit)
{
    crc_unit.model.result_delay = 3;

    // The application holds the unit for the first three begins
    EXPECT_CALL(crc_unit, BeginCalculation(_, _)).WillOnce(Return(CRCIF_BUSY)).WillOnce(Return(CRCIF_BUSY)).WillOnce(Return(CRCIF_BUSY)).WillOnce(DoDefault());
    EXPECT_CALL(crc_unit, GetResult(_)).Times(4);

    MEEM_ValidateConfiguration();
}

TEST_F(HardwareCrcDeathTest, SelfTestFailsWithAMisconfiguredUnit)
{
    crc_unit.model.initial_value = 0x0000; // Calculates CRC-16/XMODEM instead of CRC-16/IBM-3740

    EXPECT_DEATH(MEEM_ValidateConfiguration(), "0x29B1");
}

TEST_F(HardwareCrcDeathTest, SelfTestFailsWithAFailingUnit)
{
    EXPECT_CALL(crc_unit, GetResult(_)).WillRepeatedly(Return(CRCIF_NOK));

    EXPECT_DEATH(MEEM_ValidateConfiguration(), "0x29B1");
}

/******************************************************************************/
/*    Uninitialized unit                                                      */
/******************************************************************************/
TEST_F(HardwareCrcDeathTest, UninitializedUnitFailsTheSelfTest)
{
    EXPECT_CALL(crc_unit, BeginCalculation(_, _)).WillRepeatedly(Return(CRCIF_NOT_INITIALIZED));

    EXPECT_DEATH(MEEM_ValidateConfiguration(), "CRCIF_NOT_INITIALIZED");
}

TEST_F(HardwareCrcDeathTest, UninitializedUnitFailsAnInstanceCheck)
{
    MEEM_DeInit();
    MEEM_Init();
    ProcessMeemUntilIdle();
    (void) StoreValidFirstInstance(0);

    EXPECT_CALL(crc_unit, GetResult(_)).WillRepeatedly(Return(CRCIF_NOT_INITIALIZED));

    EXPECT_DEATH(ReadFirstInstance(0, MEEM_IO_INITIATE), "CRCIF_NOT_INITIALIZED");
}

TEST_F(HardwareCrcDeathTest, UninitializedUnitFailsAWrite)
{
    MEEM_DeInit();
    MEEM_Init();
    ProcessMeemUntilIdle();
    MEEM_Resume();
    ASSERT_TRUE(MEEM_InitiateBlockWrite(0));

    EXPECT_CALL(crc_unit, BeginCalculation(_, _)).WillRepeatedly(Return(CRCIF_NOT_INITIALIZED));

    EXPECT_DEATH(ProcessMeemUntilIdle(), "CRCIF_NOT_INITIALIZED");
}

/******************************************************************************/
/*    Waiting for the unit                                                    */
/******************************************************************************/
TEST_F(HardwareCrcTest, InitWaitsForADelayedResult)
{
    constexpr uint8_t DELAY = 5;

    InitAndStoreAllBlocks();
    const auto caches = SnapshotCaches();
    MEEM_DeInit();

    crc_unit.model.result_delay = DELAY;
    EXPECT_CALL(crc_unit, GetResult(_)).Times(AtLeast((DELAY + 1) * (1 + MEEM_BLOCK_COUNT))); // The self-test and at least one check per block

    MEEM_Init();

    ExpectAllBlocksInitializedWith(caches);
}

TEST_F(HardwareCrcTest, InitRetriesABusyUnit)
{
    constexpr int REFUSALS = 5;

    InitAndStoreAllBlocks();
    const auto caches = SnapshotCaches();
    MEEM_DeInit();

    // The self-test gets the unit, then the application holds it for the first instance check
    auto& begin = EXPECT_CALL(crc_unit, BeginCalculation(_, _)).WillOnce(DoDefault());
    for (int i = 0; i < REFUSALS; i++)
    {
        begin.WillOnce(Return(CRCIF_BUSY));
    }
    begin.WillRepeatedly(DoDefault());

    MEEM_Init();

    ExpectAllBlocksInitializedWith(caches);
}

TEST_F(HardwareCrcTest, WriteWaitsForADelayedResult)
{
    constexpr uint8_t DELAY = 5;

    MEEM_DeInit();
    MEEM_Init();
    ProcessMeemUntilIdle();
    MEEM_Resume();
    crc_unit.model.result_delay = DELAY;

    for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
    {
        SCOPED_TRACE("Block ID: " + std::to_string(block_id));

        ChangeAllDataInBlock(block_id);
        ASSERT_TRUE(MEEM_InitiateBlockWrite(block_id));
        MEEM_PeriodicTask(); // Starts the write: copies the cache to the work buffer
        const auto eeprom_before_write = CreateEepromSnapshot();

        // One tick begins the calculation, and the result comes DELAY polls later
        for (int tick = 0; tick <= DELAY; tick++)
        {
            MEEM_PeriodicTask();
            EXPECT_TRUE(MEEM_IsBusy());
        }
        EXPECT_TRUE(eeprom_before_write == eep_sim->eeprom) << "Written before the checksum was in";

        ProcessMeemUntilIdle();
        EXPECT_TRUE(MEEM_GetBlockStatus(block_id).write_complete);
        EXPECT_FALSE(MEEM_GetBlockStatus(block_id).write_failed);
    }

    // The written instances pass their checks
    const auto caches = SnapshotCaches();
    MEEM_DeInit();
    MEEM_Init();
    ExpectAllBlocksInitializedWith(caches);
}

TEST_F(HardwareCrcTest, WriteRetriesABusyUnit)
{
    constexpr int     REFUSALS = 4;
    constexpr uint8_t block_id = 0;

    MEEM_DeInit();
    MEEM_Init();
    ProcessMeemUntilIdle();
    MEEM_Resume();

    // The application holds the unit for the first begins of the write
    auto& begin = EXPECT_CALL(crc_unit, BeginCalculation(_, _));
    for (int i = 0; i < REFUSALS; i++)
    {
        begin.WillOnce(Return(CRCIF_BUSY));
    }
    begin.WillRepeatedly(DoDefault());

    ChangeAllDataInBlock(block_id);
    ASSERT_TRUE(MEEM_InitiateBlockWrite(block_id));
    MEEM_PeriodicTask(); // Starts the write: copies the cache to the work buffer
    const auto eeprom_before_write = CreateEepromSnapshot();

    for (int tick = 0; tick < REFUSALS; tick++)
    {
        MEEM_PeriodicTask();
        EXPECT_TRUE(MEEM_IsBusy());
    }
    EXPECT_TRUE(eeprom_before_write == eep_sim->eeprom) << "Written before the checksum was in";

    ProcessMeemUntilIdle();
    EXPECT_TRUE(MEEM_GetBlockStatus(block_id).write_complete);
    EXPECT_FALSE(MEEM_GetBlockStatus(block_id).write_failed);
}

TEST_F(HardwareCrcTest, ProfileSwitchWaitsForADelayedResult)
{
    constexpr uint8_t DELAY = 5;

    MEEM_DeInit();
    MEEM_Init();
    ProcessMeemUntilIdle();
    MEEM_Resume();
    crc_unit.model.result_delay = DELAY;

    for (auto block_id : FilterBlocksByManagementType(MEEM_MGMT_MULTI_PROFILE))
    {
        SCOPED_TRACE("Block ID: " + std::to_string(block_id));
        const uint8_t target_profile = (MEEM_GetActiveProfile(block_id) + 1) % MEEM_block_config[block_id].instance_count;

        ASSERT_TRUE(MEEM_InitiateSwitchToProfile(block_id, target_profile));

        int ticks = 0;
        for (; (ticks < 100) && !MEEM_IsMultiProfileBlockReady(block_id); ticks++)
        {
            MEEM_PeriodicTask();
        }
        EXPECT_TRUE(MEEM_IsMultiProfileBlockReady(block_id));
        EXPECT_GT(ticks, DELAY) << "The fetch completed before the result was in";
        ProcessMeemUntilIdle();
    }
}

/******************************************************************************/
/*    Failing unit                                                            */
/******************************************************************************/
/** Where the unit fails: when it's asked to begin, or in the result */
enum class FailingCall {
    Begin,
    GetResult
};

class HardwareCrcFailureTest : public HardwareCrcTest, public testing::WithParamInterface<FailingCall>
{
  public:
    /// @brief Lets the unit fail from the given call of the failing function on
    void LetTheUnitFail(int from_call = 1)
    {
        if (GetParam() == FailingCall::Begin)
        {
            auto& begin = EXPECT_CALL(crc_unit, BeginCalculation(_, _));
            for (int i = 1; i < from_call; i++)
            {
                begin.WillOnce(DoDefault());
            }
            begin.WillRepeatedly(Return(CRCIF_NOK));
        }
        else
        {
            auto& get_result = EXPECT_CALL(crc_unit, GetResult(_));
            for (int i = 1; i < from_call; i++)
            {
                get_result.WillOnce(DoDefault());
            }
            get_result.WillRepeatedly(Return(CRCIF_NOK));
        }
    }
};

INSTANTIATE_TEST_SUITE_P(BeginAndResult, HardwareCrcFailureTest, testing::Values(FailingCall::Begin, FailingCall::GetResult),
                         [](const testing::TestParamInfo<FailingCall>& info) { return (info.param == FailingCall::Begin) ? "Begin" : "GetResult"; });

TEST_P(HardwareCrcFailureTest, CheckCountsAsAnUnreadableInstance)
{
    MEEM_DeInit();
    MEEM_Init();
    ProcessMeemUntilIdle();
    LetTheUnitFail();

    for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
    {
        SCOPED_TRACE("Block ID: " + std::to_string(block_id));
        (void) StoreValidFirstInstance(block_id);

        EXPECT_EQ(ReadFirstInstance(block_id, MEEM_IO_INITIATE), MEEM_NOK);
    }
}

TEST_P(HardwareCrcFailureTest, InitRecoversEveryBlock)
{
    InitAndStoreAllBlocks();
    MEEM_DeInit();
    LetTheUnitFail(2); // The self-test passes

    MEEM_Init();

    for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
    {
        EXPECT_TRUE(MEEM_GetBlockStatus(block_id).recovered) << "Block #" << static_cast<int>(block_id);
    }
}

TEST_P(HardwareCrcFailureTest, RewriteIsSkippedAndFails)
{
    MEEM_DeInit();
    MEEM_Init();
    ProcessMeemUntilIdle();
    MEEM_Resume();
    LetTheUnitFail();

    for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
    {
        SCOPED_TRACE("Block ID: " + std::to_string(block_id));
        const auto eeprom_before_rewrite = CreateEepromSnapshot();

        RequestRewrite(block_id);
        ProcessMeemUntilIdle();

        EXPECT_TRUE(MEEM_GetBlockStatus(block_id).write_failed);
        EXPECT_TRUE(MEEM_GetBlockStatus(block_id).write_complete);
        EXPECT_FALSE(MEEM_block_status[block_id].rewrite_all);
        EXPECT_TRUE(eeprom_before_rewrite == eep_sim->eeprom) << "The rewrite wasn't skipped";
    }
}

TEST_P(HardwareCrcFailureTest, WriteIsSkippedAndFails)
{
    MEEM_DeInit();
    MEEM_Init();
    ProcessMeemUntilIdle();
    MEEM_Resume();
    LetTheUnitFail();

    for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
    {
        SCOPED_TRACE("Block ID: " + std::to_string(block_id));
        const auto eeprom_before_write = CreateEepromSnapshot();

        EXPECT_CALL(user_callbacks_mock, OnBlockWriteComplete(block_id)).Times(1);
        ChangeAllDataInBlock(block_id);
        ASSERT_TRUE(MEEM_InitiateBlockWrite(block_id));
        ProcessMeemUntilIdle();

        EXPECT_TRUE(MEEM_GetBlockStatus(block_id).write_failed);
        EXPECT_TRUE(MEEM_GetBlockStatus(block_id).write_complete);
        EXPECT_TRUE(eeprom_before_write == eep_sim->eeprom) << "The write wasn't skipped";
    }
}
