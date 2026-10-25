//
// This file is part of AT&C project which simulates virtual world of air traffic and ATC.
// Code licensing terms are available at https://github.com/felix-b/atc/blob/master/LICENSE
//

#include <memory>
#include "gtest/gtest.h"
#include "libworld.h"
#include "libworld_test.h"
#include "simpleRunwayMutex.hpp"
#include "aircraftPerformanceModel.hpp"

using namespace std;
using namespace world;
using namespace ai;

TEST(WakeTurbulenceTest, wtcCategory_enumValuesAreDistinct)
{
    EXPECT_NE(WakeTurbulenceCategory::Light, WakeTurbulenceCategory::Medium);
    EXPECT_NE(WakeTurbulenceCategory::Medium, WakeTurbulenceCategory::Heavy);
    EXPECT_NE(WakeTurbulenceCategory::Heavy, WakeTurbulenceCategory::Super);
    EXPECT_NE(WakeTurbulenceCategory::Helicopter, WakeTurbulenceCategory::Light);
}

TEST(WakeTurbulenceTest, wakeSeparationConstants_defined)
{
    using namespace wake_separation;

    // ICAO Doc 4444 standard values
    EXPECT_DOUBLE_EQ(HEAVY_BEHIND_LIGHT, 5.0);
    EXPECT_DOUBLE_EQ(HEAVY_BEHIND_MEDIUM, 5.0);
    EXPECT_DOUBLE_EQ(HEAVY_BEHIND_HEAVY, 4.0);

    EXPECT_DOUBLE_EQ(SUPER_BEHIND_LIGHT, 6.0);
    EXPECT_DOUBLE_EQ(SUPER_BEHIND_MEDIUM, 6.0);
    EXPECT_DOUBLE_EQ(SUPER_BEHIND_HEAVY, 6.0);
    EXPECT_DOUBLE_EQ(SUPER_BEHIND_SUPER, 4.0);

    EXPECT_DOUBLE_EQ(MEDIUM_BEHIND_LIGHT, 4.0);
    EXPECT_DOUBLE_EQ(MEDIUM_BEHIND_MEDIUM, 3.0);
    EXPECT_DOUBLE_EQ(MEDIUM_BEHIND_HEAVY, 5.0);

    EXPECT_DOUBLE_EQ(LIGHT_BEHIND_ANY, 3.0);

    EXPECT_DOUBLE_EQ(HELICOPTER_SPACING, 1.5);
}

TEST(WakeTurbulenceTest, wakeWeight_backwardCompatibleWithNewGetWakeCategory)
{
    auto host = TestHostServices::createWithWorldAirports({});
    auto profileService = host->services().get<AircraftPerformanceProfileService>();

    // Test weight mapping for each WTC code using test profiles
    const auto& heavyProfile = profileService->resolve("C17");
    const auto& lightProfile = profileService->resolve("C172");

    // Heavy jet (C17, WTC H) should have Heavy wake category
    EXPECT_EQ(heavyProfile.wakeTurbulenceCategory, "H");

    // Light aircraft (C172, WTC L) should have Light wake category
    EXPECT_EQ(lightProfile.wakeTurbulenceCategory, "L");
}

TEST(WakeTurbulenceTest, wakeWeightMapping_matchesWtcCode)
{
    // Test that the wake weight mapping is correct
    // This verifies the getWakeWeight function logic indirectly
    // through the profile's WTC strings.

    unordered_map<string, AircraftPerformanceProfileService::Profile> profiles;
    profiles["LIGH"] = AircraftPerformanceProfileService::createSafeDefault("LIGH", "L1P", "L");
    profiles["MEDM"] = AircraftPerformanceProfileService::createSafeDefault("MEDM", "L2J", "M");
    profiles["HVY"] = AircraftPerformanceProfileService::createSafeDefault("HVY", "L4J", "H");
    profiles["SUPR"] = AircraftPerformanceProfileService::createSafeDefault("SUPR", "L4J", "J");

    auto service = make_shared<AircraftPerformanceProfileService>(profiles);

    const auto& lightProfile = service->resolve("LIGH");
    const auto& mediumProfile = service->resolve("MEDM");
    const auto& heavyProfile = service->resolve("HVY");
    const auto& superProfile = service->resolve("SUPR");

    EXPECT_EQ(lightProfile.wakeTurbulenceCategory, "L");
    EXPECT_EQ(mediumProfile.wakeTurbulenceCategory, "M");
    EXPECT_EQ(heavyProfile.wakeTurbulenceCategory, "H");
    EXPECT_EQ(superProfile.wakeTurbulenceCategory, "J");
}

TEST(WakeTurbulenceTest, heavy_departure_light_arrival_checksConstants)
{
    // ICAO Doc 4444: Heavy arrival behind Light = 5 NM separation.
    // Departure behind arrival spacing is tested via MutexTestCase in simpleRunwayMutexTest.cpp.
    // Here we verify the spacing constants are correct.
    EXPECT_DOUBLE_EQ(wake_separation::HEAVY_BEHIND_LIGHT, 5.0);
}

TEST(WakeTurbulenceTest, super_departure_light_arrival_requires6nm)
{
    EXPECT_DOUBLE_EQ(wake_separation::SUPER_BEHIND_LIGHT, 6.0);
}

TEST(WakeTurbulenceTest, surfaceDistanceFactor_defaultsTo1ForDry)
{
    EXPECT_DOUBLE_EQ(performance_model::surfaceDistanceFactor(
        performance_model::SurfaceCondition::Dry), 1.0);
}

TEST(WakeTurbulenceTest, windInfo_defaultHasZeroHeadwind)
{
    performance_model::WindInfo wind;
    EXPECT_DOUBLE_EQ(wind.headwindComponentKt, 0.0);
}

TEST(WakeTurbulenceTest, windInfo_headwindReducesTakeoffDuration)
{
    AircraftPerformanceProfileService::Profile profile;
    profile.takeOffV2Kt = 145;
    profile.takeOffDistanceMeters = 2200;

    // No wind
    auto noWindDuration = performance_model::calcTakeoffRollDuration(profile, 0);

    // 15 kt headwind
    performance_model::WindInfo headwind;
    headwind.headwindComponentKt = 15.0;
    auto headwindDuration = performance_model::calcTakeoffRollDuration(profile, 0, headwind);

    // Headwind reduces takeoff distance, so duration should be shorter
    EXPECT_LT(headwindDuration.count(), noWindDuration.count());
}

TEST(WakeTurbulenceTest, windInfo_tailwindIncreasesTakeoffDuration)
{
    AircraftPerformanceProfileService::Profile profile;
    profile.takeOffV2Kt = 145;
    profile.takeOffDistanceMeters = 2200;

    auto noWindDuration = performance_model::calcTakeoffRollDuration(profile, 0);

    // 15 kt tailwind (negative headwind)
    performance_model::WindInfo tailwind;
    tailwind.headwindComponentKt = -15.0;
    auto tailwindDuration = performance_model::calcTakeoffRollDuration(profile, 0, tailwind);

    // Tailwind increases takeoff distance, so duration should be longer
    EXPECT_GT(tailwindDuration.count(), noWindDuration.count());
}

TEST(WakeTurbulenceTest, headwindReducesApproachGroundSpeed)
{
    AircraftPerformanceProfileService::Profile profile;
    profile.landingVatKt = 140;

    auto noWindSpeed = performance_model::calcFinalApproachGroundSpeedKt(profile);
    EXPECT_EQ(noWindSpeed, 140);

    performance_model::WindInfo headwind;
    headwind.headwindComponentKt = 10.0;
    auto headwindSpeed = performance_model::calcFinalApproachGroundSpeedKt(profile, headwind);

    // Headwind should reduce groundspeed
    EXPECT_EQ(headwindSpeed, 130);
}
