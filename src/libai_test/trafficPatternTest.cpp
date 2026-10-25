//
// This file is part of AT&C project which simulates virtual world of air traffic and ATC.
// Code licensing terms are available at https://github.com/felix-b/atc/blob/master/LICENSE
//

#include "gtest/gtest.h"
#include "libworld_test.h"
#include "trafficPattern.hpp"

using namespace std;
using namespace world;
using namespace ai;

shared_ptr<Airport> createAirportEFGH(shared_ptr<TestHostServices> host);

TEST(TrafficPatternTest, buildLeftPattern_createsLeftDownwindGeometry)
{
    auto host = TestHostServices::createWithWorldAirports({ createAirportEFGH });
    auto airport = host->getWorld()->getAirport("EFGH");
    const auto& runwayEnd = airport->getRunwayOrThrow("09")->getEndOrThrow("09");
    const auto& profile = host->services().get<AircraftPerformanceProfileService>()->resolve("C172");

    const auto geometry = traffic_pattern::buildLeftPattern(profile, runwayEnd, profile.category);
    const auto threshold = runwayEnd.centerlinePoint().geo();

    EXPECT_NEAR(270.0f, geometry.downwindHeading, 0.2f);
    EXPECT_NEAR(180.0f, geometry.baseHeading, 0.2f);
    EXPECT_NEAR(90.0f, geometry.finalHeading, 0.2f);
    EXPECT_GE(geometry.patternAltitudeFeetAgl, 1000.0f);
    EXPECT_LT(geometry.finalStart.longitude, threshold.longitude);
    EXPECT_GT(geometry.baseTurn.latitude, geometry.finalStart.latitude);
    EXPECT_GT(geometry.downwindEntry.longitude, geometry.baseTurn.longitude);
    EXPECT_GT(geometry.downwindDuration.count(), 0);
    EXPECT_GT(geometry.baseDuration.count(), 0);
}

TEST(TrafficPatternTest, buildRightPattern_createsRightDownwindGeometry)
{
    auto host = TestHostServices::createWithWorldAirports({ createAirportEFGH });
    auto airport = host->getWorld()->getAirport("EFGH");
    const auto& runwayEnd = airport->getRunwayOrThrow("09")->getEndOrThrow("09");
    const auto& profile = host->services().get<AircraftPerformanceProfileService>()->resolve("C172");

    const auto geometry = traffic_pattern::buildRightPattern(profile, runwayEnd, profile.category);

    // Right pattern: downwind heading should be the same (opposite of final)
    EXPECT_NEAR(270.0f, geometry.downwindHeading, 0.2f);
    // Base heading: right turn from downwind = +90 degrees => 0 (north)
    EXPECT_NEAR(0.0f, geometry.baseHeading, 0.2f);
    // Final heading unchanged
    EXPECT_NEAR(90.0f, geometry.finalHeading, 0.2f);
    EXPECT_GE(geometry.patternAltitudeFeetAgl, 1000.0f);
    EXPECT_GT(geometry.downwindDuration.count(), 0);
    EXPECT_GT(geometry.baseDuration.count(), 0);
}

TEST(TrafficPatternTest, buildPattern_delegatesToCorrectDirection)
{
    auto host = TestHostServices::createWithWorldAirports({ createAirportEFGH });
    auto airport = host->getWorld()->getAirport("EFGH");
    const auto& runwayEnd = airport->getRunwayOrThrow("09")->getEndOrThrow("09");
    const auto& profile = host->services().get<AircraftPerformanceProfileService>()->resolve("C172");

    // Left pattern should have base heading at 180 (south)
    auto leftGeometry = traffic_pattern::buildPattern(
        profile, runwayEnd, profile.category, traffic_pattern::PatternDirection::Left);
    EXPECT_NEAR(180.0f, leftGeometry.baseHeading, 0.2f);

    // Right pattern should have base heading at 0 (north)
    auto rightGeometry = traffic_pattern::buildPattern(
        profile, runwayEnd, profile.category, traffic_pattern::PatternDirection::Right);
    EXPECT_NEAR(0.0f, rightGeometry.baseHeading, 0.2f);
}

TEST(TrafficPatternTest, buildPatternForRunway_defaultsToLeft)
{
    auto host = TestHostServices::createWithWorldAirports({ createAirportEFGH });
    auto airport = host->getWorld()->getAirport("EFGH");
    const auto& runwayEnd = airport->getRunwayOrThrow("09")->getEndOrThrow("09");
    const auto& profile = host->services().get<AircraftPerformanceProfileService>()->resolve("C172");

    // buildPatternForRunway should default to left pattern
    auto geometry = traffic_pattern::buildPatternForRunway(profile, runwayEnd, profile.category);
    EXPECT_NEAR(180.0f, geometry.baseHeading, 0.2f);
    EXPECT_NEAR(270.0f, geometry.downwindHeading, 0.2f);
}
