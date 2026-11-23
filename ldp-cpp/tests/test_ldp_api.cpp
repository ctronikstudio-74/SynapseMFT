#include "gtest/gtest.h"
#include "../include/ldp_api.h"

// Esempio di test per LDP_Initialize
TEST(LDPApiTest, Initialize)
{
    int result = LDP_Initialize();
    EXPECT_EQ(result, 0); // Supponendo che 0 sia successo
    LDP_Shutdown();
}

// Esempio di test per LDP_Shutdown
TEST(LDPApiTest, Shutdown)
{
    LDP_Initialize();
    int result = LDP_Shutdown();
    EXPECT_EQ(result, 0); // Supponendo che 0 sia successo
}

// Altri test possono essere aggiunti qui

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
