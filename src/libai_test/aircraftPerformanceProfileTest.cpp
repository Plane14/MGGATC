//
// This file is part of AT&C project which simulates virtual world of air traffic and ATC.
// Code licensing terms are available at https://github.com/felix-b/atc/blob/master/LICENSE
//

#include <memory>
#include "gtest/gtest.h"
#include "aircraftPerformanceProfileService.hpp"
#include "libworld_test.h"

using namespace std;
using namespace world;

TEST(AircraftPerformanceProfileTest, heavyJet_hasCorrectLengthMeters)
{
    auto profile = AircraftPerformanceProfileService::createSafeDefault("B747", "L4J", "H");
    EXPECT_NEAR(profile.lengthMeters, 60.0, 0.01);
}

TEST(AircraftPerformanceProfileTest, jet_hasCorrectLengthMeters)
{
    auto profile = AircraftPerformanceProfileService::createSafeDefault("B738", "L2J", "M");
    EXPECT_NEAR(profile.lengthMeters, 35.0, 0.01);
}

TEST(AircraftPerformanceProfileTest, turboprop_hasCorrectLengthMeters)
{
    auto profile = AircraftPerformanceProfileService::createSafeDefault("ATR72", "L2T", "M");
    EXPECT_NEAR(profile.lengthMeters, 30.0, 0.01);
}

TEST(AircraftPerformanceProfileTest, lightProp_hasCorrectLengthMeters)
{
    auto profile = AircraftPerformanceProfileService::createSafeDefault("C172", "L1P", "L");
    EXPECT_NEAR(profile.lengthMeters, 12.0, 0.01);
}

TEST(AircraftPerformanceProfileTest, helicopter_hasCorrectLengthMeters)
{
    auto profile = AircraftPerformanceProfileService::createSafeDefault("A109", "H1T", "L");
    EXPECT_NEAR(profile.lengthMeters, 15.0, 0.01);
}

TEST(AircraftPerformanceProfileTest, resolve_returnsProfileWithData)
{
    unordered_map<string, AircraftPerformanceProfileService::Profile> profiles;
    profiles["TEST"] = AircraftPerformanceProfileService::createSafeDefault("TEST", "L2J", "H");
    auto service = make_shared<AircraftPerformanceProfileService>(profiles);

    const auto& profile = service->resolve("TEST");
    EXPECT_EQ(profile.icao, "TEST");
    EXPECT_NE(profile.lengthMeters, 0.0);
}

TEST(AircraftPerformanceProfileTest, resolve_createsSafeDefaultForUnknownIcao)
{
    unordered_map<string, AircraftPerformanceProfileService::Profile> profiles;
    // No profile for "FOOBAR" - should get a safe default
    profiles["B738"] = AircraftPerformanceProfileService::createSafeDefault("B738", "L2J", "M");
    auto service = make_shared<AircraftPerformanceProfileService>(profiles);

    const auto& profile = service->resolve("FOOBAR");
    EXPECT_EQ(profile.icao, "FOOBAR");
    // Safe default for unknown aircraft with no doc8643/WTC falls back to Jet
    EXPECT_NEAR(profile.lengthMeters, 35.0, 0.01);
    EXPECT_EQ(profile.takeOffV2Kt, 145);
}

TEST(AircraftPerformanceProfileTest, profile_defaultsHavePerformanceData)
{
    auto host = TestHostServices::createWithWorld();
    auto profileService = host->services().get<AircraftPerformanceProfileService>();

    const auto& b738 = profileService->resolve("B738");
    const auto& c172 = profileService->resolve("C172");
    const auto& a109 = profileService->resolve("A109");

    EXPECT_GT(b738.lengthMeters, 0.0);
    EXPECT_GT(c172.lengthMeters, 0.0);
    EXPECT_GT(a109.lengthMeters, 0.0);

    EXPECT_GT(b738.takeOffV2Kt, 0);
    EXPECT_GT(c172.takeOffV2Kt, 0);
    EXPECT_GT(a109.takeOffV2Kt, 0);
}
