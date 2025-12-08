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
#include "eeprom_simulator.hpp"
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

#endif // TEST_BASE_HPP