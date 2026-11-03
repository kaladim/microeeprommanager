#ifndef TEST_LEGACY_BASE_HPP
#define TEST_LEGACY_BASE_HPP

#include "test_base.hpp"
#include <numeric>
#include "MEEM_GenInterface.h"
#include "MEEM_Migration.h"

/*!
 * \brief Base of the tests of a configuration with legacy models (meem_add_test_config() with LEGACY in CMake), which can load the image
 *        that the image generator builds of each legacy model, and store instances of the legacy sources in it.
 */
class LegacyTestBase : public TestBase
{
  public:
    /// @brief The images of the legacy models, in the order of MEEM_legacy_models (MEEM_TEST_LEGACY_IMAGE_FILES)
    static std::vector<std::string> LegacyImageFiles()
    {
        std::vector<std::string> files;
        std::stringstream        list(MEEM_TEST_LEGACY_IMAGE_FILES);

        for (std::string file; std::getline(list, file, '|');)
        {
            files.push_back(file);
        }
        return files;
    }

    /// @brief The size of a block's parameters: a 'wear-leveling' block's data starts with its sequence counter, which isn't one
    static uint16_t ParameterSize(uint16_t data_size, uint8_t management_type)
    {
        return data_size - ((management_type == MEEM_MGMT_WEAR_LEVELING) ? 1u : 0u);
    }

    /// @brief Loads the image of legacy model k into the simulated EEPROM: its header, and each of its blocks with its defaults
    void LoadLegacyImage(uint8_t k)
    {
        eep_sim->load_image(LegacyImageFiles().at(k));
    }

    /// @brief Whether a current block is copied from legacy model k, by an index into its legacy blocks
    static bool IsCopied(uint8_t k, uint8_t block_id)
    {
        return MEEM_legacy_models[k].sources[block_id] <= 0xFCu;
    }

    /// @brief The legacy source of a block that is copied from legacy model k
    static const MEEM_legacyBlockConfig_t* SourceOf(uint8_t k, uint8_t block_id)
    {
        EXPECT_TRUE(IsCopied(k, block_id)) << "Block ID " << int(block_id) << " isn't copied from legacy model " << int(k);
        return &MEEM_legacy_models[k].blocks[MEEM_legacy_models[k].sources[block_id]];
    }

    /// @brief The legacy source of a block that a routine converts from legacy model k. The source table doesn't index it, but the legacy
    ///        block array holds the sources that the core reads in the order of the current blocks.
    static const MEEM_legacyBlockConfig_t* RoutineSourceOf(uint8_t k, uint8_t block_id)
    {
        const uint8_t* sources = MEEM_legacy_models[k].sources;
        EXPECT_EQ(sources[block_id], MEEM_SOURCE_ROUTINE) << "Block ID " << int(block_id) << " has no routine for legacy model " << int(k);

        const auto index = std::count_if(sources, sources + block_id, [](uint8_t source) { return (source <= 0xFCu) || (source == MEEM_SOURCE_ROUTINE); });
        return &MEEM_legacy_models[k].blocks[index];
    }

    static size_t InstanceSize(const MEEM_legacyBlockConfig_t* source)
    {
        return sizeof(MEEM_checksum_t) + source->data_size;
    }

    static size_t InstanceOffset(const MEEM_legacyBlockConfig_t* source, uint8_t instance_index)
    {
        return source->offset_in_eeprom + (instance_index * InstanceSize(source));
    }

    /// @brief Stores a valid instance of a legacy source: [checksum][data], where a 'wear-leveling' instance's data starts with its sequence counter
    void StoreInstance(const MEEM_legacyBlockConfig_t* source, uint8_t instance_index, const std::vector<uint8_t>& data)
    {
        ASSERT_EQ(data.size(), source->data_size);
        const auto image = CreateInstanceImage(data.data(), data.size());
        std::copy(image.cbegin(), image.cend(), eep_sim->eeprom.begin() + InstanceOffset(source, instance_index));
    }

    /// @brief Flips a bit in the data of an instance of a legacy source, which then fails its check
    void CorruptInstance(const MEEM_legacyBlockConfig_t* source, uint8_t instance_index)
    {
        eep_sim->eeprom[InstanceOffset(source, instance_index) + sizeof(MEEM_checksum_t) + source->data_size - 1u] ^= 0x01u;
    }

    void EraseInstance(const MEEM_legacyBlockConfig_t* source, uint8_t instance_index)
    {
        eep_sim->erase(InstanceOffset(source, instance_index), InstanceSize(source));
    }
};

/*!
 * \brief Base of the tests of a migration in MEEM_Init(): they load the image of a legacy model, store parameters in its
 *        sources, and compare what MEEM_Init() and the periodic task make of them with the caches and the EEPROM.
 */
class MigrationTestBase : public LegacyTestBase
{
  public:
    void SetUp() override
    {
        LegacyTestBase::SetUp(); // Important: Call base class SetUp() first
        MEEM_DeInit();
    }

    /// @brief Whether pass 2 migrates a block from legacy model k: the order after the source table lists it
    static bool IsInPass2(uint8_t k, uint8_t block_id)
    {
        const uint8_t* order = &MEEM_legacy_models[k].sources[MEEM_BLOCK_COUNT];

        for (uint8_t n = 0; n < order[0]; n++)
        {
            if (order[1 + (2 * n)] == block_id)
            {
                return true;
            }
        }
        return false;
    }

    /// @brief The legacy source of a block that is copied or converted from legacy model k
    static const MEEM_legacyBlockConfig_t* AnySourceOf(uint8_t k, uint8_t block_id)
    {
        return IsCopied(k, block_id) ? SourceOf(k, block_id) : RoutineSourceOf(k, block_id);
    }

    /// @brief The parameters within the data of an instance or a cache: without a 'wear-leveling' sequence counter
    static std::vector<uint8_t> ParametersOf(const std::vector<uint8_t>& data, uint8_t management_type)
    {
        return std::vector<uint8_t>(data.cbegin() + ((management_type == MEEM_MGMT_WEAR_LEVELING) ? 1 : 0), data.cend());
    }

    /// @brief The parameters in a block's cache: without a 'wear-leveling' sequence counter
    std::vector<uint8_t> CachedParameters(uint8_t block_id)
    {
        return ParametersOf(SnapshotCache(block_id), MEEM_block_config[block_id].management_type);
    }

    /// @brief The defaults of a block's parameters, as MEEM_RestoreDefaults() puts them into its cache, which is left as it was
    std::vector<uint8_t> DefaultsOf(uint8_t block_id)
    {
        const auto cache = SnapshotCache(block_id);

        MEEM_RestoreDefaults(block_id);
        const auto defaults = CachedParameters(block_id);

        std::copy(cache.cbegin(), cache.cend(), MEEM_block_config[block_id].cache);
        return defaults;
    }

    /// @brief Stores the parameters in the instance of a legacy source that the readers take: the single instance, the first copy,
    ///        the given profile, or the most recent 'wear-leveling' instance: the last one, with the sequence counters 0 ... n-1, after
    ///        older instances that hold other parameters
    void StoreSourceParameters(const MEEM_legacyBlockConfig_t* source, uint8_t profile, const std::vector<uint8_t>& parameters)
    {
        ASSERT_EQ(parameters.size(), ParameterSize(source->data_size, source->management_type));

        if (source->management_type == MEEM_MGMT_WEAR_LEVELING)
        {
            for (uint8_t instance_index = 0; instance_index < source->instance_count; instance_index++)
            {
                std::vector<uint8_t> data{instance_index};

                for (const auto parameter : parameters)
                {
                    const bool most_recent = (instance_index == (source->instance_count - 1));
                    data.push_back(most_recent ? parameter : static_cast<uint8_t>(parameter + source->instance_count - instance_index));
                }
                StoreInstance(source, instance_index, data);
            }
        }
        else
        {
            StoreInstance(source, (source->management_type == MEEM_MGMT_MULTI_PROFILE) ? profile : 0, parameters);
        }
    }

    /// @brief Stores random parameters in the instance of a legacy source that the readers take, as StoreSourceParameters() above does
    /// @return the parameters
    std::vector<uint8_t> StoreSourceParameters(const MEEM_legacyBlockConfig_t* source, uint8_t profile = 0)
    {
        const auto parameters = GenerateRandomBytes(ParameterSize(source->data_size, source->management_type));

        StoreSourceParameters(source, profile, parameters);
        return parameters;
    }

    /// @brief Makes every instance of a legacy source fail its check
    void CorruptSource(const MEEM_legacyBlockConfig_t* source)
    {
        for (uint8_t instance_index = 0; instance_index < source->instance_count; instance_index++)
        {
            CorruptInstance(source, instance_index);
        }
    }

    /// @brief The write jobs of the given profiles of a multi-profile block, in the given order
    static std::vector<EepromSimulator::WriteJob> ProfileWriteJobs(uint8_t block_id, const std::vector<uint8_t>& profiles)
    {
        const auto                             block_cfg     = &MEEM_block_config[block_id];
        const auto                             instance_size = sizeof(MEEM_checksum_t) + block_cfg->data_size;
        std::vector<EepromSimulator::WriteJob> jobs;

        for (const auto profile : profiles)
        {
            jobs.push_back({block_cfg->offset_in_eeprom + (profile * instance_size), instance_size});
        }
        return jobs;
    }

    /// @brief The write jobs of the rewrites of the given blocks, every instance of each, in the order of the round robin, then the header
    static std::vector<EepromSimulator::WriteJob> RewriteAndCommitJobs(const std::vector<uint8_t>& block_ids)
    {
        std::vector<EepromSimulator::WriteJob> jobs;

        for (const auto block_id : block_ids)
        {
            std::vector<uint8_t> instances(MEEM_block_config[block_id].instance_count);
            std::iota(instances.begin(), instances.end(), uint8_t{0});

            const auto block_jobs = ProfileWriteJobs(block_id, instances); // The instances of any block lie one after the other
            jobs.insert(jobs.end(), block_jobs.cbegin(), block_jobs.cend());
        }
        jobs.push_back(HeaderWriteJob());
        return jobs;
    }
};

#endif // TEST_LEGACY_BASE_HPP
