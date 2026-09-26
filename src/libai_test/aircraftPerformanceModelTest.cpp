//
// This file is part of AT&C project which simulates virtual world of air traffic and ATC.
// Code licensing terms are available at https://github.com/felix-b/atc/blob/master/LICENSE
//
#include "gtest/gtest.h"

#include "aircraftPerformanceModel.hpp"

using namespace ai;
using namespace world;

TEST(AircraftPerformanceModelTest, derivesLandingAndFinalTargetsFromProfile) {
    AircraftPerformanceProfileService::Profile profile;
    profile.landingVatKt = 147;
    profile.approachRodFpm = 1500;
    profile.landingDistanceMeters = 1600;
    profile.taxiHighSpeedMetersPerSecond = 10.0;

    EXPECT_NEAR(2.0, performance_model::calcFinalApproachMinutes(profile), 0.01);
    EXPECT_EQ(147, performance_model::calcFinalApproachGroundSpeedKt(profile));
    EXPECT_EQ(750, performance_model::calcPreFlareSinkRateFpm(profile));
    EXPECT_EQ(138, performance_model::calcTouchdownSpeedKt(profile));
    EXPECT_GT(performance_model::calcLandingRollDuration(profile).count(), 0);
}

TEST(AircraftPerformanceModelTest, derivesTakeoffTargetsFromProfile) {
    AircraftPerformanceProfileService::Profile profile;
    profile.takeOffV2Kt = 145;
    profile.takeOffDistanceMeters = 2300;
    profile.initialClimbRocFpm = 3000;
    profile.climb150RocFpm = 2000;

    EXPECT_GT(performance_model::calcTakeoffRollDuration(profile, 0).count(), 0);
    EXPECT_GT(performance_model::calcLiftOffDuration(profile).count(), 0);
    EXPECT_GT(performance_model::calcAirborneAccelerationDuration(profile).count(), 0);
    EXPECT_GT(performance_model::calcPostTakeoffTransitionDuration(profile).count(), 0);
}
