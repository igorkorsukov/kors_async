#include <gtest/gtest.h>

class Async_Tests : public ::testing::Test
{
public:
};

TEST_F(Async_Tests, Tests1)
{
    EXPECT_EQ(42, 42);
}
