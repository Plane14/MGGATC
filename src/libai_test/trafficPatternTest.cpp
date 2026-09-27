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
