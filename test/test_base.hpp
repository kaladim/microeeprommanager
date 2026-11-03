#ifndef TEST_BASE_HPP
#define TEST_BASE_HPP

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <array>
#include <sstream>
#include <string>
#include <iomanip>
#include <random>
#include <algorithm>
#include <cstring>
#include "eeprom_simulator.hpp"
#include "reference_crc16.hpp"
#include "MEEM.h"
#include "MEEM_GenConfig.h"
#include "MEEM_Internal.h"
#include "MEEM_UserCallbacks_Mock.hpp"

extern std::unique_ptr<EepromSimulator> eep_sim;

class TestBase : public testing::Test
{
  protected:
    // Create a random device
    std::random_device rd;

    // Initialize a random number generator with the random device
    std::mt19937 _rng{rd()};

    // Mock for user callbacks - available in all tests
    testing::NiceMock<MockUserCallbacks> user_callbacks_mock{};

  public:
    TestBase()
    {
        // You can do set-up work for each test here.
    }

    ~TestBase() override
    {
        // You can do clean-up work that doesn't throw exceptions here.
    }

    // If the constructor and destructor are not enough for setting up
    // and cleaning up each test, you can define the following methods:

    virtual void SetUp() override
    {
        // Set the mock instance pointer so callbacks delegate to this mock
        MockUserCallbacks::instance = &user_callbacks_mock;

        // The simulator is shared by all tests: no power cut and no write log is left over from a previous one
        eep_sim->restore_power();
        eep_sim->write_log.clear();
    }

    virtual void TearDown() override
    {
        // Clear the mock instance pointer after each test
        MockUserCallbacks::instance = nullptr;
    }

    void ProcessMeemUntilIdle()
    {
        do
        {
            MEEM_PeriodicTask();
        } while (MEEM_IsBusy());
    }

    void ChangeAllDataInBlock(uint8_t block_id)
    {
        auto block_cfg = &MEEM_block_config[block_id];
        int  i         = static_cast<int>(block_cfg->management_type == MEEM_MGMT_WEAR_LEVELING);

        // Change the block's data
        for (; i < block_cfg->data_size; i++)
        {
            block_cfg->cache[i] += 0x11;
        }
    }

    bool IsOwnAreaWrittenOnly(uint8_t block_id, std::vector<uint8_t>& eeprom_before_write)
    {
        auto block_cfg = &MEEM_block_config[block_id];

        // 1. Compare the area before block start
        if (not std::equal(eeprom_before_write.begin(), eeprom_before_write.begin() + block_cfg->offset_in_eeprom, eep_sim->eeprom.begin()))
        {
            return false;
        }

        // Compare the area after block end
        const auto block_area_size_in_eeprom = (block_cfg->data_size + sizeof(MEEM_checksum_t)) * block_cfg->instance_count;

        return std::equal(eeprom_before_write.begin() + block_cfg->offset_in_eeprom + block_area_size_in_eeprom, eeprom_before_write.end(),
                          eep_sim->eeprom.begin() + block_cfg->offset_in_eeprom + block_area_size_in_eeprom);
    }

    /// @brief Filter blocks by management type
    /// @param block_management_type
    /// @return IDs of blocks with matching management type
    std::vector<uint8_t> FilterBlocksByManagementType(uint8_t block_management_type)
    {
        std::vector<uint8_t> blocks_ids{};
        for (uint8_t i = 0; i < MEEM_BLOCK_COUNT; i++)
        {
            if (MEEM_block_config[i].management_type == block_management_type)
            {
                blocks_ids.push_back(i);
            }
        }
        return blocks_ids;
    }

    /// @brief Filter blocks by management type and instance count
    /// @param block_management_type
    /// @param instance_count
    /// @return IDs of blocks with matching instance count
    std::vector<uint8_t> FilterBlocksByManagementTypeAndInstanceCount(uint8_t block_management_type, uint8_t instance_count)
    {
        std::vector<uint8_t> blocks_ids{};
        for (uint8_t i = 0; i < MEEM_BLOCK_COUNT; i++)
        {
            if ((MEEM_block_config[i].management_type == block_management_type) and (MEEM_block_config[i].instance_count == instance_count))
            {
                blocks_ids.push_back(i);
            }
        }
        return blocks_ids;
    }

    void CorruptInstanceInEeprom(uint8_t block_id, uint8_t instance_id)
    {
        assert(block_id < MEEM_BLOCK_COUNT);
        const auto block_cfg = &MEEM_block_config[block_id];
        assert(instance_id < block_cfg->instance_count);

        // Define the range for the random numbers
        std::uniform_int_distribution<> dist(0, (block_cfg->data_size - 1));

        // Generate a random index in the defined range
        const auto rnd_index               = dist(_rng);
        const auto instance_size_in_eeprom = sizeof(MEEM_checksum_t) + block_cfg->data_size;
        eep_sim->eeprom[block_cfg->offset_in_eeprom + (instance_id * instance_size_in_eeprom) + rnd_index] ^= 1;
    }

    std::vector<uint8_t> CreateEepromSnapshot()
    {
        // Make a copy of the entire EEPROM
        return std::vector<uint8_t>(eep_sim->eeprom);
    }

    /// @brief Stores an instance with random data and a valid checksum at the first instance of a block
    /// @return the stored instance: [checksum][data]
    std::vector<uint8_t> StoreValidFirstInstance(uint8_t block_id)
    {
        const auto            block_cfg = &MEEM_block_config[block_id];
        const auto            data      = GenerateRandomBytes(block_cfg->data_size);
        const MEEM_checksum_t checksum  = ReferenceCrc16(data.data(), data.size());

        std::vector<uint8_t> instance(sizeof(checksum));
        std::memcpy(instance.data(), &checksum, sizeof(checksum));
        instance.insert(instance.end(), data.cbegin(), data.cend());

        std::copy(instance.cbegin(), instance.cend(), eep_sim->eeprom.begin() + block_cfg->offset_in_eeprom);
        return instance;
    }

    /// @brief Reads the first instance of a block into the work buffer with the read task
    /// @param start_stage MEEM_IO_INITIATE for a checked read, MEEM_IO_INITIATE_UNCHECKED for one without the check
    /// @return the read task's result
    MEEM_status_t ReadFirstInstance(uint8_t block_id, MEEM_ioStage_t start_stage)
    {
        MEEM_status_t status;

        MEEM_StartReadOperation(block_id);
        MEEM_global_status.io_request.offset_in_eeprom = MEEM_block_config[block_id].offset_in_eeprom;
        MEEM_global_status.io_request.stage            = start_stage;

        do
        {
            status = MEEM_ReadOperationTask();
        } while (MEEM_BUSY == status);

        return status;
    }

    /// @brief Requests a rewrite of a block, a write that covers every instance, by setting its bits as MEEM_Init() does
    void RequestRewrite(uint8_t block_id)
    {
        MEEM_block_status[block_id].write_pending  = true;
        MEEM_block_status[block_id].rewrite_all    = true;
        MEEM_block_status[block_id].write_complete = false;
    }

    /// @brief Requests the commit of a reset or a migration, the write of the identity header, by setting its bit as MEEM_Init() does
    void RequestCommit()
    {
        MEEM_global_status.identity_pending = true;
    }

    /// @brief The write job of the identity header
    static EepromSimulator::WriteJob HeaderWriteJob()
    {
        return {0, sizeof(MEEM_identity_header)};
    }

    /// @brief The current identity header, MEEM_identity_header
    static std::vector<uint8_t> CurrentHeader()
    {
        return std::vector<uint8_t>(MEEM_identity_header, MEEM_identity_header + sizeof(MEEM_identity_header));
    }

    /// @brief The 6 bytes at offset 0 of the EEPROM, where the identity header is stored
    std::vector<uint8_t> GetHeaderFromEeprom()
    {
        return std::vector<uint8_t>(eep_sim->eeprom.cbegin(), eep_sim->eeprom.cbegin() + sizeof(MEEM_identity_header));
    }

    /// @brief The image of an instance, as the core writes it: [checksum][data]
    std::vector<uint8_t> CreateInstanceImage(const uint8_t* data, size_t size)
    {
        const MEEM_checksum_t checksum = ReferenceCrc16(data, size);

        std::vector<uint8_t> image(sizeof(checksum));
        std::memcpy(image.data(), &checksum, sizeof(checksum));
        image.insert(image.end(), data, data + size);
        return image;
    }

    std::vector<uint8_t> GetInstanceFromEeprom(uint8_t block_id, uint8_t instance_id)
    {
        const auto block_cfg     = &MEEM_block_config[block_id];
        const auto instance_size = sizeof(MEEM_checksum_t) + block_cfg->data_size;
        const auto begin         = eep_sim->eeprom.cbegin() + block_cfg->offset_in_eeprom + (instance_id * instance_size);

        return std::vector<uint8_t>(begin, begin + instance_size);
    }

    std::vector<uint8_t> SnapshotCache(uint8_t block_id)
    {
        const auto block_cfg = &MEEM_block_config[block_id];
        return std::vector<uint8_t>(block_cfg->cache, block_cfg->cache + block_cfg->data_size);
    }

    /// @brief Expects every instance of the block to hold the content, a 'wear-leveling' instance with its sequence counter 0 … n-1
    void ExpectEveryInstanceHolds(uint8_t block_id, std::vector<uint8_t> content)
    {
        const auto block_cfg = &MEEM_block_config[block_id];

        for (uint8_t instance_id = 0; instance_id < block_cfg->instance_count; instance_id++)
        {
            if (block_cfg->management_type == MEEM_MGMT_WEAR_LEVELING)
            {
                content[0] = instance_id;
            }
            EXPECT_EQ(GetInstanceFromEeprom(block_id, instance_id), CreateInstanceImage(content.data(), content.size())) << "Instance " << int(instance_id);
        }
    }

    bool IsInWorkBuffer(const std::vector<uint8_t>& instance)
    {
        return std::equal(instance.cbegin(), instance.cend(), MEEM_work_buffer);
    }

    std::vector<uint8_t> GenerateRandomBytes(size_t length)
    {
        // Create a distribution.  For raw bytes, we want a uniform distribution across the entire byte range (0-255).
        std::uniform_int_distribution<> distribution(0, 255);

        // Generate random bytes.
        std::vector<uint8_t> random_bytes(length);

        // Use std::generate to fill the vector with random bytes:
        std::generate(random_bytes.begin(), random_bytes.end(), [&]() { return distribution(_rng); });
        return random_bytes;
    }
    void FillWithRandomBytes(std::vector<uint8_t>& dest)
    {
        // Create a distribution.  For raw bytes, we want a uniform distribution across the entire byte range (0-255).
        std::uniform_int_distribution<> distribution(0, 255);

        // Use std::generate to fill the vector with random bytes:
        std::generate(dest.begin(), dest.end(), [&]() { return distribution(_rng); });
    }

    std::string ToHexString(const uint8_t bytes[], size_t len)
    {
        std::stringstream ss;
        ss << std::hex << std::setfill('0') << std::uppercase;
        for (size_t i = 0; i < len; ++i)
        {
            ss << std::setw(2) << static_cast<unsigned int>(bytes[i]);
            if ((i + 1) % 16 == 0)
            { // Newline every 16 bytes
                ss << "\n";
            }
            else if (i < (len - 1))
            { // Space between bytes (except last)
                ss << " ";
            }
        }
        return ss.str();
    }
};

/*!
 * \brief Base of the tests that judge the block data as v1 did: before each test, the simulated EEPROM holds the configuration's image
 *        (meem_add_test_image() in CMake): the current identity header, as after a commit, so that MEEM_Init()
 *        initializes the blocks as in v1 instead of resetting them, and every block with its defaults.
 *        So no test sees what an earlier one left in the EEPROM, such as a random instance that passes its check.
 *        A test that erases the EEPROM or loads another image replaces the header with it.
 */
class CommittedEepromTest : public TestBase
{
  public:
    void SetUp() override
    {
        TestBase::SetUp(); // Important: Call base class SetUp() first
        eep_sim->load_image(MEEM_TEST_IMAGE_FILE);
    }

    /// @brief Erases every block, but not the identity header: what a blank EEPROM was to v1's tests. MEEM_Init() initializes the blocks as in
    ///        v1, while it resets every block of a blank EEPROM.
    void EraseBlocks()
    {
        eep_sim->erase(sizeof(MEEM_identity_header), eep_sim->eeprom.size() - sizeof(MEEM_identity_header));
    }
};

#endif // TEST_BASE_HPP