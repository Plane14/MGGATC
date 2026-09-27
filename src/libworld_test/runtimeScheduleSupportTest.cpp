//
// This file is part of AT&C project which simulates virtual world of air traffic and ATC.
// Code licensing terms are available at https://github.com/felix-b/atc/blob/master/LICENSE
//
#include "gtest/gtest.h"
#include "runtimeScheduleSupport.hpp"

using namespace runtime_schedule;

TEST(RuntimeScheduleSupportTest, parsesLiveAircraftPointResponse) {
    const string json = R"json(
        {
            "ac": [
                {
                    "hex": "abc123",
                    "flight": " aal123  ",
                    "r": "N123AA",
                    "t": "B738",
                    "lat": 33.9425,
                    "lon": -118.4081,
                    "alt_baro": "ground",
                    "gs": 12.0
                },
                {
                    "hex": "def456",
                    "flight": "DAL456",
                    "r": "N456DL",
                    "t": "A321",
                    "lat": 34.0500,
                    "lon": -118.2500,
                    "alt_baro": 12000,
                    "gs": 250.5
                }
            ]
        }
    )json";

    const auto aircraft = parsePointResponse(json);

    ASSERT_EQ(2, aircraft.size());
    EXPECT_EQ("AAL123", aircraft[0].callSign);
    EXPECT_EQ("N123AA", aircraft[0].registration);
    EXPECT_EQ("B738", aircraft[0].modelIcao);
    EXPECT_TRUE(aircraft[0].onGround);
    EXPECT_DOUBLE_EQ(0, aircraft[0].altitudeFeet);
    EXPECT_FALSE(aircraft[1].onGround);
    EXPECT_DOUBLE_EQ(12000, aircraft[1].altitudeFeet);
}

TEST(RuntimeScheduleSupportTest, parsesRouteResponseWithIntermediateStop) {
    const string json = R"json(
        {
            "callsign": "AAL123",
            "number": "123",
            "airline_code": "AAL",
            "airport_codes": "KJFK-KORD-KLAX",
            "plausible": true,
            "_airports": [
                { "icao": "KJFK", "lat": 40.6398, "lon": -73.7789 },
                { "icao": "KORD", "lat": 41.9786, "lon": -87.9048 },
                { "icao": "KLAX", "lat": 33.9425, "lon": -118.4081 }
            ]
        }
    )json";

    const RouteData route = parseRouteResponse(json);

    ASSERT_TRUE(route.known);
    EXPECT_TRUE(route.plausible);
    EXPECT_EQ("AAL123", route.callSign);
    EXPECT_EQ("123", route.flightNo);
    EXPECT_EQ("AAL", route.airlineIcao);
    ASSERT_EQ(3, route.airports.size());
    EXPECT_EQ("KORD", route.airports[1].icao);
}

TEST(RuntimeScheduleSupportTest, classifiesIntermediateInboundAsTurnaround) {
    const RouteData route = parseRouteResponse(R"json(
        {
            "callsign": "AAL123",
            "number": "123",
            "airline_code": "AAL",
            "airport_codes": "KJFK-KORD-KLAX",
            "plausible": true,
            "_airports": [
                { "icao": "KJFK", "lat": 40.6398, "lon": -73.7789 },
                { "icao": "KORD", "lat": 41.9786, "lon": -87.9048 },
                { "icao": "KLAX", "lat": 33.9425, "lon": -118.4081 }
            ]
        }
    )json");

    const LiveAircraft aircraft = {
        "ABC123",
        "AAL123",
        "N123AA",
        "B738",
        GeoPoint(41.70, -87.85),
        4000,
        210,
        false
    };
    LiveScheduleCandidate candidate;

    ASSERT_TRUE(tryBuildCandidate("KORD", GeoPoint(41.9786, -87.9048), aircraft, route, candidate));
    EXPECT_EQ(CandidateType::Turnaround, candidate.type);
    EXPECT_EQ("KJFK", candidate.originIcao);
    EXPECT_EQ("KORD", candidate.destinationIcao);
    EXPECT_EQ("KLAX", candidate.onwardDestinationIcao);
}

TEST(RuntimeScheduleSupportTest, classifiesGroundOriginAsDepartureOnly) {
    const RouteData route = parseRouteResponse(R"json(
        {
            "callsign": "SWA555",
            "number": "555",
            "airline_code": "SWA",
            "airport_codes": "KLAX-KLAS",
            "plausible": true,
            "_airports": [
                { "icao": "KLAX", "lat": 33.9425, "lon": -118.4081 },
                { "icao": "KLAS", "lat": 36.0801, "lon": -115.1522 }
            ]
        }
    )json");

    const LiveAircraft aircraft = {
        "DEF456",
        "SWA555",
        "N555SW",
        "B738",
        GeoPoint(33.9430, -118.4075),
        0,
        5,
        true
    };
    LiveScheduleCandidate candidate;

    ASSERT_TRUE(tryBuildCandidate("KLAX", GeoPoint(33.9425, -118.4081), aircraft, route, candidate));
    EXPECT_EQ(CandidateType::DepartureOnly, candidate.type);
    EXPECT_EQ("KLAX", candidate.originIcao);
    EXPECT_EQ("KLAS", candidate.destinationIcao);
}

TEST(RuntimeScheduleSupportTest, parsesMilitaryAirbasesCsv) {
    stringstream csv(R"csv(
icao,name,country,operation,primary_model_icao,secondary_model_icao,tertiary_model_icao
KLSV,Nellis Air Force Base,United States,FIGHTER,F16,F35,H60
ETAR,Ramstein Air Base,Germany,TRANSPORT,C17,C130,H60
)csv");

    const auto airbases = parseMilitaryAirbasesCsv(csv);

    ASSERT_EQ(2, airbases.size());
    EXPECT_EQ("KLSV", airbases[0].icao);
    EXPECT_EQ("Nellis Air Force Base", airbases[0].name);
    EXPECT_EQ("FIGHTER", airbases[0].operation);
    EXPECT_EQ("F16", airbases[0].primaryModelIcao);
    ASSERT_TRUE(findMilitaryAirbase(airbases, "etar"));
    EXPECT_EQ("C17", findMilitaryAirbase(airbases, "ETAR")->primaryModelIcao);
}
