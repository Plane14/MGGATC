//
// This file is part of AT&C project which simulates virtual world of air traffic and ATC.
// Code licensing terms are available at https://github.com/felix-b/atc/blob/master/LICENSE
//

#include "gtest/gtest.h"
#include "libworld_test.h"
#include "simplePhraseologyService.hpp"
#include "intentTypes.hpp"
#include "clearanceTypes.hpp"

using namespace std;
using namespace world;

namespace
{
    shared_ptr<Airport> createAdvisoryAirport(shared_ptr<TestHostServices> host)
    {
        auto runway = make_shared<Runway>(
            Runway::End("09", 0, 0, UniPoint::fromGeo(host, { 30.00, 45.00 })),
            Runway::End("27", 0, 0, UniPoint::fromGeo(host, { 30.00, 45.06 })),
            30);

        Airport::Header header("EFGH", "Advisory Test", GeoPoint(30.00, 45.03), 123);
        auto tower = WorldBuilder::assembleAirportTower(host, header, nullptr, {
            {
                ControllerPosition::Type::Local,
                FREQUENCY_UNICOM_1228,
                GeoPolygon::empty(),
                "UNICOM"
            }
        });

        return WorldBuilder::assembleAirport(host, header, { runway }, {}, {}, {}, tower, nullptr);
    }
}

TEST(SimplePhraseologyServiceTest, verbalizePatternReport_usesPatternLegCall)
{
    auto host = TestHostServices::createWithWorldAirports({ createAdvisoryAirport });
    auto airport = host->getWorld()->getAirport("EFGH");
    auto advisory = airport->localAt(airport->header().datum());
    auto flight = host->addIfrFlight(101, "EFGH", "KXYZ", airport->header().datum(), Altitude::ground(), "C172").ptr;
    SimplePhraseologyService phraseology(host);

    auto text = phraseology.verbalizeIntent(make_shared<PilotReportPatternIntent>(
        1,
        flight,
        advisory,
        "09",
        PilotReportPatternIntent::Leg::Downwind))->plainText();

    EXPECT_NE(string::npos, text.find("left downwind runway"));
}

TEST(SimplePhraseologyServiceTest, advisoryTakeoffUsesDiscretionPhrase)
{
    auto host = TestHostServices::createWithWorldAirports({ createAdvisoryAirport });
    auto airport = host->getWorld()->getAirport("EFGH");
    auto advisory = airport->localAt(airport->header().datum());
    auto flight = host->addIfrFlight(102, "EFGH", "KXYZ", airport->header().datum(), Altitude::ground(), "C172").ptr;
    SimplePhraseologyService phraseology(host);

    Clearance::Header header = {
        1,
        Clearance::Type::TakeoffClearance,
        chrono::microseconds(0),
        advisory,
        flight
    };
    auto clearance = make_shared<TakeoffClearance>(header, "09", false, 90.0f, 0);
    auto text = phraseology.verbalizeIntent(make_shared<TowerClearedForTakeoffIntent>(
        2,
        0,
        advisory,
        flight,
        true,
        clearance,
        vector<TrafficAdvisory>(),
        0))->plainText();

    EXPECT_NE(string::npos, text.find("depart at your discretion"));
    EXPECT_EQ(string::npos, text.find("cleared for"));
}
