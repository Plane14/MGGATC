//
// This file is part of AT&C project which simulates virtual world of air traffic and ATC.
// Code licensing terms are available at https://github.com/felix-b/atc/blob/master/LICENSE
//
#include <chrono>
#include <memory>
#include "gtest/gtest.h"
#include "libworld.h"
#include "libworld_test.h"
#include "aiAircraft.hpp"

using namespace std;
using namespace world;
using namespace ai;

shared_ptr<Airport> createAirportEFGH(shared_ptr<TestHostServices> host);

namespace ai_test
{
    static shared_ptr<ai::AIAircraft> createFlyingAircraft(
        const shared_ptr<TestHostServices>& host,
        shared_ptr<Flight>& flightOut)
    {
        auto plan = make_shared<FlightPlan>(0, 3600, "EFGH", "EFGH");
        plan->setDepartureRunway("09");
        plan->setArrivalRunway("09");

        auto flight = make_shared<Flight>(
            host, 101, Flight::RulesType::IFR, "TST", "101", "TEST 101", plan);

        auto aircraft = make_shared<ai::AIAircraft>(
            host, 101, "B738", "TST", "N101", Aircraft::Category::Heavy);

        flight->setAircraft(aircraft);
        host->getWorld()->addFlight(flight);

        flightOut = flight;
        return aircraft;
    }

    TEST(aiAircraftTest, zero_speed_and_vs_keeps_position)
    {
        auto host = TestHostServices::createWithWorldAirports({ createAirportEFGH });
        shared_ptr<Flight> flight;
        auto aircraft = createFlyingAircraft(host, flight);

        aircraft->setLocation(GeoPoint(30.10, 45.30));
        aircraft->setAltitude(Altitude::msl(5000));
        aircraft->setGroundSpeedKt(0);
        aircraft->setVerticalSpeedFpm(0);

        const auto t0 = chrono::seconds(100);
        aircraft->progressTo(t0);
        aircraft->progressTo(t0 + chrono::minutes(5));

        EXPECT_DOUBLE_EQ(30.10, aircraft->location().latitude);
        EXPECT_DOUBLE_EQ(45.30, aircraft->location().longitude);
        EXPECT_DOUBLE_EQ(5000, aircraft->altitude().feet());
    }

    TEST(aiAircraftTest, ground_speed_advances_along_track)
    {
        auto host = TestHostServices::createWithWorldAirports({ createAirportEFGH });
        shared_ptr<Flight> flight;
        auto aircraft = createFlyingAircraft(host, flight);

        aircraft->setLocation(GeoPoint(30.10, 45.30));
        aircraft->setAltitude(Altitude::msl(5000));
        aircraft->setAttitude(AircraftAttitude(90.0f, 0, 0)); // heading 090 -> track 090
        ASSERT_DOUBLE_EQ(90.0, aircraft->track());

        const auto t0 = chrono::seconds(100);
        aircraft->progressTo(t0); // prime m_locationTimestamp while stationary
        aircraft->setGroundSpeedKt(120);
        aircraft->progressTo(t0 + chrono::minutes(10));

        // 120kt for 10 minutes = 20NM = 37040m due east
        const float movedMeters = GeoMath::getDistanceMeters(
            GeoPoint(30.10, 45.30), aircraft->location());
        EXPECT_NEAR(37040.0f, movedMeters, 500.0f);
        EXPECT_NEAR(30.10, aircraft->location().latitude, 0.01);
        EXPECT_GT(aircraft->location().longitude, 45.30);
    }

    TEST(aiAircraftTest, track_follows_attitude_heading)
    {
        auto host = TestHostServices::createWithWorldAirports({ createAirportEFGH });
        shared_ptr<Flight> flight;
        auto aircraft = createFlyingAircraft(host, flight);

        aircraft->setAttitude(AircraftAttitude(270.0f, 0, 0));
        EXPECT_DOUBLE_EQ(270.0, aircraft->track());
    }

    TEST(aiAircraftTest, descent_below_zero_agl_reports_ground_and_touchdown_once)
    {
        auto host = TestHostServices::createWithWorldAirports({ createAirportEFGH });
        shared_ptr<Flight> flight;
        auto aircraft = createFlyingAircraft(host, flight);

        aircraft->setLocation(GeoPoint(30.10, 45.30));
        aircraft->setAltitude(Altitude::agl(100));

        const auto t0 = chrono::seconds(100);
        aircraft->progressTo(t0);
        aircraft->setVerticalSpeedFpm(-1000);
        const auto t1 = t0 + chrono::seconds(30); // -500fpm for 0.5min => 100-500 < 0 => ground
        aircraft->progressTo(t1);

        EXPECT_EQ(Altitude::Type::Ground, aircraft->altitude().type());
        EXPECT_TRUE(aircraft->justTouchedDown(t1));
        EXPECT_FALSE(aircraft->justTouchedDown(t1));
    }

    TEST(aiAircraftTest, msl_descent_near_landing_elevation_converts_to_agl)
    {
        auto host = TestHostServices::createWithWorldAirports({ createAirportEFGH });
        shared_ptr<Flight> flight;
        auto aircraft = createFlyingAircraft(host, flight);

        // landing runway elevation = 123 (queryTerrainElevationAt stub); MaxAltitudeAGL = 300
        aircraft->setLocation(GeoPoint(30.10, 45.30));
        aircraft->setAltitude(Altitude::msl(523)); // elev 123 + 400

        const auto t0 = chrono::seconds(100);
        aircraft->progressTo(t0);
        aircraft->setVerticalSpeedFpm(-1200);
        aircraft->progressTo(t0 + chrono::seconds(10)); // nextFeet = 523 - 200 = 323 <= 423 => AGL(323-123=200)

        EXPECT_EQ(Altitude::Type::AGL, aircraft->altitude().type());
        EXPECT_NEAR(200.0f, aircraft->altitude().feet(), 1.0f);
    }

    TEST(aiAircraftTest, msl_descent_high_above_landing_elevation_stays_msl)
    {
        auto host = TestHostServices::createWithWorldAirports({ createAirportEFGH });
        shared_ptr<Flight> flight;
        auto aircraft = createFlyingAircraft(host, flight);

        aircraft->setLocation(GeoPoint(30.10, 45.30));
        aircraft->setAltitude(Altitude::msl(5000));

        const auto t0 = chrono::seconds(100);
        aircraft->progressTo(t0);
        aircraft->setVerticalSpeedFpm(-1000);
        aircraft->progressTo(t0 + chrono::minutes(1)); // 5000 - 1000 = 4000 > 423 => stays MSL

        EXPECT_EQ(Altitude::Type::MSL, aircraft->altitude().type());
        EXPECT_NEAR(4000.0f, aircraft->altitude().feet(), 1.0f);
    }

    TEST(aiAircraftTest, climb_from_agl_above_max_converts_to_msl)
    {
        auto host = TestHostServices::createWithWorldAirports({ createAirportEFGH });
        shared_ptr<Flight> flight;
        auto aircraft = createFlyingAircraft(host, flight);

        aircraft->setLocation(GeoPoint(30.10, 45.30));
        aircraft->setAltitude(Altitude::agl(200));

        const auto t0 = chrono::seconds(100);
        aircraft->progressTo(t0);
        aircraft->setVerticalSpeedFpm(3000);
        aircraft->progressTo(t0 + chrono::seconds(10)); // nextFeet = 200 + 500 = 700 > 300 => MSL(700+123)

        EXPECT_EQ(Altitude::Type::MSL, aircraft->altitude().type());
        EXPECT_NEAR(823.0f, aircraft->altitude().feet(), 1.0f);
    }

    TEST(aiAircraftTest, ground_aircraft_with_negative_vs_stays_ground)
    {
        auto host = TestHostServices::createWithWorldAirports({ createAirportEFGH });
        shared_ptr<Flight> flight;
        auto aircraft = createFlyingAircraft(host, flight);

        aircraft->setLocation(GeoPoint(30.10, 45.30));
        aircraft->setAltitude(Altitude::ground());

        const auto t0 = chrono::seconds(100);
        aircraft->progressTo(t0);
        aircraft->setVerticalSpeedFpm(-500);
        const auto t1 = t0 + chrono::seconds(10);
        aircraft->progressTo(t1);

        EXPECT_EQ(Altitude::Type::Ground, aircraft->altitude().type());
        EXPECT_FALSE(aircraft->justTouchedDown(t1));
    }

    TEST(aiAircraftTest, climb_from_ground_enters_agl)
    {
        auto host = TestHostServices::createWithWorldAirports({ createAirportEFGH });
        shared_ptr<Flight> flight;
        auto aircraft = createFlyingAircraft(host, flight);

        aircraft->setLocation(GeoPoint(30.10, 45.30));
        aircraft->setAltitude(Altitude::ground());

        const auto t0 = chrono::seconds(100);
        aircraft->progressTo(t0);
        aircraft->setVerticalSpeedFpm(1500);
        aircraft->progressTo(t0 + chrono::seconds(10)); // nextFeet = 0 + 250 => AGL(250)

        EXPECT_EQ(Altitude::Type::AGL, aircraft->altitude().type());
        EXPECT_NEAR(250.0f, aircraft->altitude().feet(), 1.0f);
    }
}
