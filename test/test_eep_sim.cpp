#include "test_base.hpp"

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
    }

    void TearDown() override
    {
        // Code here will be called immediately after each test (right before the destructor).
    }
};

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
