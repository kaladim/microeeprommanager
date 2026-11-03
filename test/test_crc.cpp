#include "test_base.hpp"
#include <vector>

class TestCrc : public TestBase
{
  public:
    void SetUp() override
    {
        TestBase::SetUp(); // Important: Call base class SetUp() first
    }

    void TearDown() override
    {
        TestBase::TearDown(); // Important: Call base class TearDown()
    }
};

TEST_F(TestCrc, CheckValue)
{
    const std::vector<uint8_t> data{'1', '2', '3', '4', '5', '6', '7', '8', '9'};

    EXPECT_EQ(MEEM_CalculateChecksum(data.data(), static_cast<uint16_t>(data.size())), 0x29B1);
}

TEST_F(TestCrc, MatchesTheReferenceOnRandomData)
{
    std::uniform_int_distribution<int> random_byte(0, 0xFF);

    for (uint16_t size : {0, 1, 2, 7, 64, 255, 1000})
    {
        std::vector<uint8_t> data(size);
        std::generate(data.begin(), data.end(), [&]() { return static_cast<uint8_t>(random_byte(_rng)); });

        EXPECT_EQ(MEEM_CalculateChecksum(data.data(), size), ReferenceCrc16(data.data(), data.size())) << "Data size: " << size;
    }
}

#if (MEEM_CRC_IMPLEMENTATION == MEEM_CRC_TABLE)
TEST_F(TestCrc, GeneratedTable)
{
    for (int byte = 0; byte <= 0xFF; byte++)
    {
        // Each entry is the CRC register after shifting the byte through a zero register
        uint16_t expected = static_cast<uint16_t>(byte << 8);

        for (int bit = 0; bit < 8; bit++)
        {
            expected = (expected & 0x8000) ? static_cast<uint16_t>((expected << 1) ^ 0x1021) : static_cast<uint16_t>(expected << 1);
        }
        EXPECT_EQ(MEEM_crc_table[byte], expected) << "Entry " << byte;
    }
}
#endif
