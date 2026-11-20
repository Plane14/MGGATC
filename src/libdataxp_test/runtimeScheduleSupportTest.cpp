//
// This file is part of AT&C project which simulates virtual world of air traffic and ATC.
// Code licensing terms are available at https://github.com/felix-b/atc/blob/master/LICENSE
//

#include <memory>
#include <sstream>
#include "gtest/gtest.h"
#include "libworld.h"
#include "runtimeScheduleSupport.hpp"

using namespace std;
using namespace world;
using namespace runtime_schedule;

namespace dataxp_test
{
    TEST(runtimeScheduleSupportTest, json_parses_scalars_arrays_objects)
    {
        auto root = JsonValue::parse(
            R"({"a": 1.5, "b": "x", "c": [true, null], "d": {"e": -2e3}})");

        EXPECT_TRUE(root.isObject());
        EXPECT_DOUBLE_EQ(root.at("a").numberOr(0), 1.5);
        EXPECT_EQ(root.at("b").stringOr(""), "x");
        EXPECT_TRUE(root.at("c").arrayItems()[0].boolOr(false));
        EXPECT_TRUE(root.at("c").arrayItems()[1].isNull());
        EXPECT_DOUBLE_EQ(root.at("d").at("e").numberOr(0), -2000);
    }

    TEST(runtimeScheduleSupportTest, json_string_escapes_and_unicode)
    {
        auto root = JsonValue::parse(R"("a\nb\t\"q\"Aé")");
        ASSERT_TRUE(root.isString());
        EXPECT_EQ(root.stringOr(""), string("a\nb\t\"q\"A\xC3\xA9"));
    }

    TEST(runtimeScheduleSupportTest, json_rejects_malformed_input)
    {
        EXPECT_THROW(JsonValue::parse("{"), runtime_error);
        EXPECT_THROW(JsonValue::parse("[1,]"), runtime_error);
        EXPECT_THROW(JsonValue::parse("tru"), runtime_error);
        EXPECT_THROW(JsonValue::parse("1 2"), runtime_error);
    }

    TEST(runtimeScheduleSupportTest, json_numberOr_parses_numeric_strings)
    {
        EXPECT_DOUBLE_EQ(JsonValue("42.5").numberOr(0), 42.5);
        EXPECT_DOUBLE_EQ(JsonValue("abc").numberOr(7), 7);
    }

    TEST(runtimeScheduleSupportTest, point_response_parses_aircraft_and_ground_flag)
    {
        auto aircraft = parsePointResponse(R"({"ac":[
            {"hex":"a1","flight":"BAW123 ","r":"G-ABCD","t":"B738","lat":51.5,"lon":-0.1,"alt_baro":"GROUND","gs":25},
            {"hex":"a2","flight":"UAL999","r":"N1","t":"A320","lat":51.6,"lon":-0.2,"alt_baro":35000,"gs":450},
            {"hex":"a3","flight":"","lat":51.6,"lon":-0.2},
            {"hex":"a4","flight":"DLH1"}
        ]})");

        ASSERT_EQ(aircraft.size(), 2);
        EXPECT_EQ(aircraft[0].callSign, "BAW123");
        EXPECT_EQ(aircraft[0].registration, "G-ABCD");
        EXPECT_TRUE(aircraft[0].onGround);
        EXPECT_DOUBLE_EQ(aircraft[0].groundSpeedKt, 25);
        EXPECT_EQ(aircraft[1].callSign, "UAL999");
        EXPECT_FALSE(aircraft[1].onGround);
        EXPECT_DOUBLE_EQ(aircraft[1].altitudeFeet, 35000);
    }

    TEST(runtimeScheduleSupportTest, route_response_handles_error_and_unknowns)
    {
        auto error = parseRouteResponse(R"({"error":"not found"})");
        EXPECT_FALSE(error.known);

        auto unknown = parseRouteResponse(R"({"callsign":"X1","airport_codes":"UNKNOWN"})");
        EXPECT_FALSE(unknown.known);

        auto route = parseRouteResponse(R"({
            "callsign":"BAW123","number":"UNKNOWN","airline_code":"UNKNOWN","plausible":true,
            "airport_codes":"EGLL LFPG",
            "_airports":[{"icao":"EGLL","lat":51.47,"lon":-0.45},{"icao":"LFPG","lat":49.0,"lon":2.55}]
        })");
        EXPECT_TRUE(route.known);
        EXPECT_EQ(route.callSign, "BAW123");
        EXPECT_TRUE(route.flightNo.empty());
        EXPECT_TRUE(route.airlineIcao.empty());
        ASSERT_EQ(route.airports.size(), 2);
        EXPECT_EQ(route.airports[0].icao, "EGLL");
    }

    TEST(runtimeScheduleSupportTest, ground_state_requires_low_altitude_and_speed)
    {
        LiveAircraft flying{"", "X", "", "", GeoPoint(0,0), 35000, 450, false};
        EXPECT_FALSE(isGroundState(flying));

        LiveAircraft lowSlow{"", "X", "", "", GeoPoint(0,0), 800, 60, false};
        EXPECT_TRUE(isGroundState(lowSlow));

        LiveAircraft flagged{"", "X", "", "", GeoPoint(0,0), 30000, 400, true};
        EXPECT_TRUE(isGroundState(flagged));

        LiveAircraft fast{"", "X", "", "", GeoPoint(0,0), 500, 150, false};
        EXPECT_FALSE(isGroundState(fast));
    }

    TEST(runtimeScheduleSupportTest, candidate_at_origin_requires_on_ground)
    {
        RouteData route{"CS1", "F1", "AIR", true, true,
            {{"EGLL", GeoPoint(51.47,-0.45)}, {"LFPG", GeoPoint(49.0,2.55)}}};

        LiveScheduleCandidate c;
        LiveAircraft parked{"", "CS1", "", "B738", GeoPoint(51.47,-0.45), 0, 5, true};
        ASSERT_TRUE(tryBuildCandidate("EGLL", GeoPoint(51.47,-0.45), parked, route, c));
        EXPECT_EQ(c.type, CandidateType::DepartureOnly);
        EXPECT_EQ(c.destinationIcao, "LFPG");

        // same aircraft airborne at origin airport => not a departure candidate
        LiveAircraft airborne{"", "CS1", "", "B738", GeoPoint(51.47,-0.45), 2000, 150, false};
        EXPECT_FALSE(tryBuildCandidate("EGLL", GeoPoint(51.47,-0.45), airborne, route, c));
    }

    TEST(runtimeScheduleSupportTest, candidate_at_destination_requires_airborne)
    {
        RouteData route{"CS1", "F1", "AIR", true, true,
            {{"EGLL", GeoPoint(51.47,-0.45)}, {"LFPG", GeoPoint(49.0,2.55)}}};

        LiveScheduleCandidate c;
        LiveAircraft inbound{"", "CS1", "", "B738", GeoPoint(49.05,2.60), 3000, 160, false};
        ASSERT_TRUE(tryBuildCandidate("LFPG", GeoPoint(49.0,2.55), inbound, route, c));
        EXPECT_EQ(c.type, CandidateType::ArrivalOnly);
        EXPECT_EQ(c.originIcao, "EGLL");

        LiveAircraft landed{"", "CS1", "", "B738", GeoPoint(49.0,2.55), 0, 10, true};
        EXPECT_FALSE(tryBuildCandidate("LFPG", GeoPoint(49.0,2.55), landed, route, c));
    }

    TEST(runtimeScheduleSupportTest, candidate_mid_route_selects_turnaround_or_next_leg)
    {
        RouteData route{"CS1", "F1", "AIR", true, true,
            {{"EGLL", GeoPoint(51.47,-0.45)}, {"EDDF", GeoPoint(50.03,8.56)}, {"LFPG", GeoPoint(49.0,2.55)}}};

        LiveScheduleCandidate c;
        LiveAircraft transit{"", "CS1", "", "B738", GeoPoint(50.03,8.56), 4000, 200, false};
        ASSERT_TRUE(tryBuildCandidate("EDDF", GeoPoint(50.03,8.56), transit, route, c));
        EXPECT_EQ(c.type, CandidateType::Turnaround);
        EXPECT_EQ(c.originIcao, "EGLL");
        EXPECT_EQ(c.onwardDestinationIcao, "LFPG");

        LiveAircraft onGround{"", "CS1", "", "B738", GeoPoint(50.03,8.56), 0, 10, true};
        ASSERT_TRUE(tryBuildCandidate("EDDF", GeoPoint(50.03,8.56), onGround, route, c));
        EXPECT_EQ(c.type, CandidateType::DepartureOnly);
        EXPECT_EQ(c.destinationIcao, "LFPG");
    }

    TEST(runtimeScheduleSupportTest, candidate_rejects_airport_not_on_route)
    {
        RouteData route{"CS1", "F1", "AIR", true, true,
            {{"EGLL", GeoPoint(51.47,-0.45)}, {"LFPG", GeoPoint(49.0,2.55)}}};

        LiveScheduleCandidate c;
        LiveAircraft ac{"", "CS1", "", "B738", GeoPoint(48.0,2.0), 5000, 300, false};
        EXPECT_FALSE(tryBuildCandidate("EDDF", GeoPoint(50.03,8.56), ac, route, c));
    }

    TEST(runtimeScheduleSupportTest, airnav_schedule_response_builds_candidates)
    {
        auto candidates = parseAirNavRadarScheduleResponse(
            R"({"candidates":[
                {"kind":"DEPARTURE","counterpart_icao":"LFPG","airline_icao":"BAW","flight_no":"123","call_sign":"BAW123","model_icao":"B738","registration":"G-AB","altitude_ft":0,"ground_speed_kt":0},
                {"kind":"ARRIVAL","counterpart_icao":"EDDF","airline_icao":"DLH","flight_no":"55","call_sign":"","model_icao":"A320"},
                {"kind":"FLYOVER","counterpart_icao":"KJFK"},
                {"kind":"ARRIVAL","counterpart_icao":"EGLL"}
            ]})",
            "EGLL",
            GeoPoint(51.47,-0.45));

        ASSERT_EQ(candidates.size(), 2);
        EXPECT_EQ(candidates[0].type, CandidateType::DepartureOnly);
        EXPECT_EQ(candidates[0].destinationIcao, "LFPG");
        EXPECT_EQ(candidates[1].type, CandidateType::ArrivalOnly);
        EXPECT_EQ(candidates[1].originIcao, "EDDF");
        EXPECT_EQ(candidates[1].aircraft.callSign, "DLH55"); // synthesized from airline+flight number
    }

    TEST(runtimeScheduleSupportTest, csv_row_splitting_handles_quotes)
    {
        auto values = splitCsvRow(R"(a,"b,c",d)");
        ASSERT_EQ(values.size(), 3);
        EXPECT_EQ(values[0], "a");
        EXPECT_EQ(values[1], "b,c");
        EXPECT_EQ(values[2], "d");
    }

    TEST(runtimeScheduleSupportTest, military_airbases_csv_parses_and_finds)
    {
        stringstream csv;
        csv << "icao,name,country,op,m1,m2,m3\n"
            << "# comment\n"
            << "ETAD,Spangdahlem,de,fighter,F16,F15,A10\n"
            << "EGVA,Fairford,uk,bomber,B52,B1,B2\n";

        auto bases = parseMilitaryAirbasesCsv(csv);
        ASSERT_EQ(bases.size(), 2);
        EXPECT_EQ(bases[0].icao, "ETAD");
        EXPECT_EQ(bases[0].primaryModelIcao, "F16");

        EXPECT_NE(findMilitaryAirbase(bases, "egva"), nullptr);
        EXPECT_EQ(findMilitaryAirbase(bases, "XXXX"), nullptr);
    }
}
