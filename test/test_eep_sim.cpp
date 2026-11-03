#include "test_base.hpp"
#include <filesystem>
#include <fstream>

class TestEepSim : public TestBase
{
public:
    TestEepSim()
    {
        // You can do set-up work for each test here.
    }

    ~TestEepSim() override
    {
        // You can do clean-up work that doesn't throw exceptions here.
    }

    // If the constructor and destructor are not enough for setting up
    // and cleaning up each test, you can define the following methods:

    void SetUp() override
    {
        TestBase::SetUp(); // Important: Call base class SetUp() first
    }

    void TearDown() override
    {
        TestBase::TearDown(); // Important: Call base class TearDown()
    }
};

TEST_F(TestEepSim, WriteLogRecordsOffsetSizeAndOrder)
{
    const std::vector<uint8_t> data(10, 0x5A);

    eep_sim->write(100, data.data(), 10);
    eep_sim->write(0, data.data(), 6);
    eep_sim->write(100, data.data(), 3);

    const std::vector<EepromSimulator::WriteJob> expected{{100, 10}, {0, 6}, {100, 3}};
    EXPECT_EQ(eep_sim->write_log, expected);

    eep_sim->read(0, std::vector<uint8_t>(10).data(), 10);
    EXPECT_EQ(eep_sim->write_log.size(), 3u) << "Reads aren't logged";
}

TEST_F(TestEepSim, PowerCutTearsTheGivenWriteJob)
{
    eep_sim->erase();
    const std::vector<uint8_t> data(8, 0x5A);

    eep_sim->cut_power_during_write(2, 3);
    eep_sim->write(0, data.data(), data.size());
    EXPECT_TRUE(eep_sim->is_powered());
    eep_sim->write(16, data.data(), data.size());
    EXPECT_FALSE(eep_sim->is_powered());

    // The first job is complete, the second wrote only its first 3 bytes
    EXPECT_TRUE(std::equal(data.cbegin(), data.cend(), eep_sim->eeprom.cbegin()));
    EXPECT_TRUE(std::all_of(eep_sim->eeprom.cbegin() + 16, eep_sim->eeprom.cbegin() + 19, [](uint8_t b) { return b == 0x5A; }));
    EXPECT_TRUE(std::all_of(eep_sim->eeprom.cbegin() + 19, eep_sim->eeprom.cbegin() + 24, [](uint8_t b) { return b == EepromSimulator::erased_state; }));

    const std::vector<EepromSimulator::WriteJob> expected{{0, 8}, {16, 8}};
    EXPECT_EQ(eep_sim->write_log, expected) << "The torn job is logged with its full size";
}

TEST_F(TestEepSim, NoJobReachesTheEepromWhileThePowerIsCut)
{
    eep_sim->erase();
    const std::vector<uint8_t> data(8, 0x5A);
    std::vector<uint8_t>       readback(8, 0x00);

    eep_sim->cut_power_during_write(1, 0);
    eep_sim->write(0, data.data(), data.size());
    eep_sim->write(16, data.data(), data.size());
    eep_sim->read(0, readback.data(), readback.size());

    EXPECT_TRUE(std::all_of(eep_sim->eeprom.cbegin(), eep_sim->eeprom.cend(), [](uint8_t b) { return b == EepromSimulator::erased_state; }));
    EXPECT_TRUE(std::all_of(readback.cbegin(), readback.cend(), [](uint8_t b) { return b == 0x00; })) << "A read delivers nothing";
    EXPECT_EQ(eep_sim->write_log.size(), 2u);

    EEAIF_status_t status;
    while (EEAIF_BUSY == (status = eep_sim->get_status()))
    {
    }
    EXPECT_EQ(status, EEAIF_NOK);

    // After the restart, the EEPROM works again
    eep_sim->restore_power();
    eep_sim->write(0, data.data(), data.size());
    EXPECT_TRUE(std::equal(data.cbegin(), data.cend(), eep_sim->eeprom.cbegin()));
}

TEST_F(TestEepSim, RestorePowerCancelsAPendingPowerCut)
{
    const std::vector<uint8_t> data(8, 0x5A);

    eep_sim->cut_power_during_write(1, 0);
    eep_sim->restore_power();
    eep_sim->write(0, data.data(), data.size());

    EXPECT_TRUE(eep_sim->is_powered());
    EXPECT_TRUE(std::equal(data.cbegin(), data.cend(), eep_sim->eeprom.cbegin()));
}

TEST_F(TestEepSim, LoadImage)
{
    const auto path  = (std::filesystem::temp_directory_path() / "meem_test_eep_sim_image.bin").string();
    const auto image = GenerateRandomBytes(eep_sim->eeprom.size());
    {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
    }

    eep_sim->load_image(path);
    EXPECT_TRUE(image == eep_sim->eeprom);

    std::filesystem::remove(path);
}

TEST_F(TestEepSim, LoadImageRejectsAMissingFileOrAnotherSize)
{
    const auto path = (std::filesystem::temp_directory_path() / "meem_test_eep_sim_short_image.bin").string();
    {
        std::ofstream f(path, std::ios::binary);
        f.write("\x01\x02", 2);
    }

    EXPECT_THROW(eep_sim->load_image(path), std::runtime_error);
    EXPECT_THROW(eep_sim->load_image(path + ".missing"), std::runtime_error);

    std::filesystem::remove(path);
}

TEST_F(TestEepSim, LoadTheGeneratedImage)
{
    eep_sim->load_image(MEEM_TEST_IMAGE_FILE);

    EXPECT_TRUE(std::equal(MEEM_identity_header, MEEM_identity_header + sizeof(MEEM_identity_header), eep_sim->eeprom.cbegin()));
}

TEST_F(TestEepSim, ReadAndWriteFromSameRegion)
{
    const auto offset = eep_sim->eeprom.size() / 4;
    const auto size = eep_sim->eeprom.size() / 2;
    auto random_data = GenerateRandomBytes(size);
    auto readback_data = std::vector<uint8_t>(random_data.size());
    auto eeprom_before_write = CreateEepromSnapshot();

    eep_sim->write(offset, random_data.data(), size);
    eep_sim->read(offset, readback_data.data(), size);

    // Written and readback data are same
    EXPECT_TRUE(std::equal(random_data.begin(),
                           random_data.begin() + size,
                           readback_data.begin()));

    // The area before written region is untouched
    EXPECT_TRUE(std::equal(eep_sim->eeprom.begin(),
                           eep_sim->eeprom.begin() + offset,
                           eeprom_before_write.begin()));

    // The area after written region is untouched
    EXPECT_TRUE(std::equal(eep_sim->eeprom.begin() + offset + size,
                           eep_sim->eeprom.end(),
                           eeprom_before_write.begin() + offset + size));
}

TEST_F(TestEepSim, EraseRegionPecisely)
{
    const auto offset = eep_sim->eeprom.size() / 4;
    const auto size = eep_sim->eeprom.size() / 2;
    FillWithRandomBytes(eep_sim->eeprom);
    auto eeprom_before_erase = CreateEepromSnapshot();

    eep_sim->erase(offset, size);

    // Erased region is all EepromSimulator::erased_state
    EXPECT_TRUE(std::all_of(eep_sim->eeprom.begin() + offset,
                            eep_sim->eeprom.begin() + offset + size,
                            [](uint8_t val)
                            { return val == EepromSimulator::erased_state; }));

    // The area before erased region is untouched
    EXPECT_TRUE(std::equal(eep_sim->eeprom.begin(),
                           eep_sim->eeprom.begin() + offset,
                           eeprom_before_erase.data()));

    // The area after erased region is untouched
    EXPECT_TRUE(std::equal(eep_sim->eeprom.begin() + offset + size,
                           eep_sim->eeprom.end(),
                           eeprom_before_erase.begin() + offset + size));
}
