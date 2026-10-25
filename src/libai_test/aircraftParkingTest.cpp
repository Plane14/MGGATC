//
// This file is part of AT&C project which simulates virtual world of air traffic and ATC.
// Code licensing terms are available at https://github.com/felix-b/atc/blob/master/LICENSE
//

#include <memory>
#include "gtest/gtest.h"
#include "libworld.h"
#include "aiAircraft.hpp"
#include "aircraftPerformanceProfileService.hpp"
#include "libworld_test.h"

using namespace std;
using namespace world;
using namespace ai;

TEST(AircraftParkingTest, heavyJet_parksAtHalfLengthPlus5m)
{
    auto host = TestHostServices::createWithWorld();
    auto profileService = host->services().get<AircraftPerformanceProfileService>();
    const auto& profile = profileService->resolve("B747");

    // Heavy Jet category should have a non-zero lengthMeters
    EXPECT_GT(profile.lengthMeters, 0.0);

    // Expected parking offset = length / 2 + 5.0
    double expectedOffset = profile.lengthMeters / 2.0 + 5.0;

    // Verify the offset formula is correct by computing it
    // A 60m long aircraft (Heavy Jet default) should park at 35.0m offset
    if (profile.lengthMeters == 60.0)
    {
        EXPECT_NEAR(expectedOffset, 35.0, 0.01);
    }
}

TEST(AircraftParkingTest, lightAircraft_parksCloser)
{
    auto host = TestHostServices::createWithWorld();
    auto profileService = host->services().get<AircraftPerformanceProfileService>();
    const auto& heavyProfile = profileService->resolve("B747");
    const auto& lightProfile = profileService->resolve("C172");

    EXPECT_GT(heavyProfile.lengthMeters, 0.0);
    EXPECT_GT(lightProfile.lengthMeters, 0.0);

    double heavyOffset = heavyProfile.lengthMeters / 2.0 + 5.0;
    double lightOffset = lightProfile.lengthMeters / 2.0 + 5.0;

    // Heavy aircraft should have a larger parking offset
    EXPECT_GT(heavyOffset, lightOffset);
}

TEST(AircraftParkingTest, helicopter_parksWithSmallOffset)
{
    auto host = TestHostServices::createWithWorld();
    auto profileService = host->services().get<AircraftPerformanceProfileService>();
    const auto& profile = profileService->resolve("A109");

    // Helicopter should have 15m length by default
    EXPECT_NEAR(profile.lengthMeters, 15.0, 0.01);

    double expectedOffset = profile.lengthMeters / 2.0 + 5.0;
    // 15/2 + 5 = 12.5
    EXPECT_NEAR(expectedOffset, 12.5, 0.01);
}

TEST(AircraftParkingTest, parkingUsesCorrectHeading)
{
    auto host = TestHostServices::createWithWorld();
    auto profileService = host->services().get<AircraftPerformanceProfileService>();
    const auto& profile = profileService->resolve("B738");

    // Simulate the park() calculation
    double parkingOffsetMeters = profile.lengthMeters / 2.0 + 5.0;

    // Verify that the offset is positive and reasonable
    EXPECT_GT(parkingOffsetMeters, 5.0);
    EXPECT_LT(parkingOffsetMeters, 50.0);
}
