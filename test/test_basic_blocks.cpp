#include "test_base.hpp"

class TestBasicBlocks : public TestBase
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
    }

    void TearDown() override
    {
        // Code here will be called immediately after each test (right before the destructor).
    }
};
