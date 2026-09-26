//
// This file is part of AT&C project which simulates virtual world of air traffic and ATC.
// Code licensing terms are available at https://github.com/felix-b/atc/blob/master/LICENSE
//
#include <sstream>

#include "gtest/gtest.h"
#include "aircraftPerformanceProfileService.hpp"

using namespace std;
using namespace world;

TEST(AircraftPerformanceProfileServiceTest, mergesBundledProfilesWithDoc8643Fallback) {
    stringstream performanceCsv;
    performanceCsv
        << "ICAO,takeOffV2,takeOffDistance,takeOffWTC,takeOffRECAT,takeOffMTOW,initialClimbIAS,initialClimbROC,climb150IAS,climb150ROC,climb240IAS,climb240ROC,machClimbMACH,machClimbROC,cruiseTAS,cruiseMACH,cruiseCeiling,cruiseRange,initialDescentMACH,initialDescentROD,descentIAS,descentROD,approachIAS,approachROD,approachMCS,landingVat,landingDistance,landingAPC\n"
        << "B738,145,2300,M,Upper Medium,70530,165,3000,290,2000,290,2000,0.78,1500,460,0.79,410,2000,0.78,800,280,3500,250,1500,210,147,1600,D\n";

    stringstream doc8643;
    doc8643
        << "BOEING\t737-800\tB738\tL2J\tM\n"
        << "CESSNA\t172\tC172\tL1P\tL\n";

    const auto profiles = AircraftPerformanceProfileService::parseBundledProfiles(performanceCsv, doc8643);

    auto b738 = profiles.at("B738");
    EXPECT_TRUE(b738.hasPerformanceData);
    EXPECT_TRUE(b738.hasDoc8643Reference);
    EXPECT_EQ("L2J", b738.doc8643Class);
    EXPECT_EQ("M", b738.wakeTurbulenceCategory);
    EXPECT_EQ(145, b738.takeOffV2Kt);
    EXPECT_EQ(147, b738.landingVatKt);
    EXPECT_EQ(Aircraft::Category::Jet, b738.category & Aircraft::Category::Jet);

    auto c172 = profiles.at("C172");
    EXPECT_FALSE(c172.hasPerformanceData);
    EXPECT_TRUE(c172.hasDoc8643Reference);
    EXPECT_EQ("L1P", c172.doc8643Class);
    EXPECT_EQ("L", c172.wakeTurbulenceCategory);
    EXPECT_EQ(75, c172.takeOffV2Kt);
    EXPECT_EQ(60, c172.landingVatKt);
    EXPECT_EQ(Aircraft::Category::LightProp, c172.category & Aircraft::Category::LightProp);
}

TEST(AircraftPerformanceProfileServiceTest, returnsSafeDefaultsForUnknownTypes) {
    AircraftPerformanceProfileService service(unordered_map<string, AircraftPerformanceProfileService::Profile>{});

    const auto& unknown = service.resolve("ZZZZ");

    EXPECT_EQ("ZZZZ", unknown.icao);
    EXPECT_FALSE(unknown.hasPerformanceData);
    EXPECT_FALSE(unknown.hasDoc8643Reference);
    EXPECT_EQ(145, unknown.takeOffV2Kt);
    EXPECT_EQ(140, unknown.landingVatKt);
    EXPECT_EQ(Aircraft::Category::Jet, unknown.category & Aircraft::Category::Jet);
}
