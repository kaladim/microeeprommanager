#include "test_base.hpp"

class TestBasicBlocks : public CommittedEepromTest
{
public:
    TestBasicBlocks()
    {
        // You can do set-up work for each test here.
    }

    ~TestBasicBlocks() override
    {
        // You can do clean-up work that doesn't throw exceptions here.
    }

    // If the constructor and destructor are not enough for setting up
    // and cleaning up each test, you can define the following methods:

    void SetUp() override
    {
        CommittedEepromTest::SetUp(); // Important: Call base class SetUp() first
    }

    void TearDown() override
    {
        TestBase::TearDown(); // Important: Call base class TearDown()
    }
};
