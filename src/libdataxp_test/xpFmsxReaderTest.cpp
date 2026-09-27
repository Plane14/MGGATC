//
// This file is part of AT&C project which simulates virtual world of air traffic and ATC.
// Code licensing terms are available at https://github.com/felix-b/atc/blob/master/LICENSE
//
#include <fstream>
#include <sstream>
#include <vector>
#include <unordered_set>
#include "gtest/gtest.h"
#include "libworld.h"
#include "libdataxp.h"
#include "libworld_test.h"
#include "libdataxp_test.h"

using namespace world;
using namespace std;

static shared_ptr<Airport> createAirportKordForCifp(shared_ptr<TestHostServices> host)
{
    XPAirportReader reader(host, -1, WorldBuilder::assembleSampleAirportControlZone);
    stringstream aptDat = makeAptDat({
        "1 680 0 0 KORD Chicago O Hare Intl",
        "1302 datum_lat 41.9773",
        "1302 datum_lon -87.9080",
        "100 60.00 1 0 0.00 1 3 0 09L 41.9745 -87.9066 0 0 0 0 0 0 27R 41.9780 -87.9300 0 0 0 0 0 0"
    });
    reader.readAirport(aptDat);
    return reader.getAirport();
}

static string makeCifpLine(
    const string& recordType,
    const string& procedureName,
    const string& fixIdent,
    const string& descCode,
    const string& pathTerm,
    const string& magCourse,
    const string& altitude1,
    const string& altitude2,
    const string& routeQualifier2)
{
    vector<string> fields(38);
    fields[0] = "000010";
    fields[1] = "2";
    fields[2] = procedureName;
    fields[3] = "RW09L";
    fields[4] = fixIdent;
    fields[5] = "P";
    fields[6] = "F";
    fields[7] = " ";
    fields[8] = descCode;
    fields[11] = pathTerm;
    fields[20] = magCourse;
    fields[23] = altitude1;
    fields[24] = altitude2;
    fields[37] = routeQualifier2;

    stringstream line;
    line << recordType << ":";
    for (int i = 0 ; i < (int)fields.size() ; i++)
    {
        if (i > 0)
        {
            line << ',';
        }
        line << fields[i];
    }
    return line.str();
}


TEST(XPFmsxReaderTest, readFmx) {
    auto host = TestHostServices::createWithWorld();
    XPFmsxReader reader(host);
    ifstream fmx;
    openTestInputStream("kjfk_kord.fmx", fmx);

    auto flightPlan = reader.readFrom(fmx);

    EXPECT_EQ(flightPlan->departureAirportIcao(), "KJFK");
    EXPECT_EQ(flightPlan->departureRunway(), "04L");
    EXPECT_EQ(flightPlan->sidName(), "GREKI6");
    EXPECT_EQ(flightPlan->sidTransition(), "JUDDS");
    EXPECT_EQ(flightPlan->starName(), "WYNDE1");
    EXPECT_EQ(flightPlan->starTransition(), "EMMMA");
    EXPECT_EQ(flightPlan->approachName(), "R-09LY");
    EXPECT_EQ(flightPlan->arrivalRunway(), "09L");
    EXPECT_EQ(flightPlan->flightNo(), "UAL738");
    ASSERT_EQ(flightPlan->legs().size(), 5);
    auto firstLeg = flightPlan->firstRouteLeg();
    ASSERT_TRUE(firstLeg);
    EXPECT_EQ(firstLeg->fromNavaid(), "KJFK");
    EXPECT_EQ(firstLeg->toNavaid(), "LAAYK");
    EXPECT_TRUE(firstLeg->hasTargetPoint());
}

TEST(XPFmsxReaderTest, readFms) {
    auto host = TestHostServices::createWithWorld();
    XPFmsxReader reader(host);
    ifstream fms;
    openTestInputStream("kjfk_kord.fms", fms);

    auto flightPlan = reader.readFrom(fms);

    EXPECT_EQ(flightPlan->departureAirportIcao(), "KJFK");
    EXPECT_EQ(flightPlan->departureRunway(), "04L");
    EXPECT_EQ(flightPlan->sidName(), "DEEZZ5");
    EXPECT_EQ(flightPlan->sidTransition(), "HEERO");
    EXPECT_EQ(flightPlan->starName(), "WYNDE1");
    EXPECT_EQ(flightPlan->starTransition(), "EMMMA");
    EXPECT_EQ(flightPlan->approachName(), "I09L");
    EXPECT_EQ(flightPlan->arrivalRunway(), "09L");
    ASSERT_EQ(flightPlan->legs().size(), 8);
    auto firstLeg = flightPlan->firstRouteLeg();
    ASSERT_TRUE(firstLeg);
    EXPECT_EQ(firstLeg->fromNavaid(), "KJFK");
    EXPECT_EQ(firstLeg->toNavaid(), "HEERO");
    EXPECT_TRUE(firstLeg->hasCourseHeading());
}

TEST(XPFmsxReaderTest, readFms_loadsMissedApproachLegsFromXPlaneNavdata) {
    auto host = TestHostServices::createWithWorldAirports({ createAirportKordForCifp });
    host->registerFileContents(
        host->getHostFilePath({ "Resources", "default data", "earth_fix.dat" }),
        "42.100000 -87.900000 MAFIX\n");
    host->registerFileContents(
        host->getHostFilePath({ "Resources", "default data", "CIFP", "KORD.dat" }),
        makeCifpLine("APPCH", "I09L", "RW09L", "GE M", "CF", "0900", "", "", "") + "\n" +
        makeCifpLine("APPCH", "I09L", "RW09L", "GEM ", "CA", "0900", "3000", "", "A;") + "\n" +
        makeCifpLine("PRDAT", "I09L", "MAFIX", "EE  ", "DF", "", "4000", "", "A;") + "\n");

    XPFmsxReader reader(host);
    ifstream fms;
    openTestInputStream("kjfk_kord.fms", fms);

    auto flightPlan = reader.readFrom(fms);
    auto goAroundLegs = flightPlan->legsOfType(FlightPlan::LegType::GoAround);

    ASSERT_EQ(goAroundLegs.size(), 2);
    EXPECT_EQ(goAroundLegs[0]->pathTerm(), "CA");
    EXPECT_EQ(goAroundLegs[0]->toNavaid(), "RW09L");
    EXPECT_TRUE(goAroundLegs[0]->hasCourseHeading());
    EXPECT_NEAR(goAroundLegs[0]->courseHeading(), 90.0f, 0.1f);
    EXPECT_FLOAT_EQ(goAroundLegs[0]->targetAltitude(), 3000.0f);
    EXPECT_TRUE(goAroundLegs[0]->hasTargetPoint());

    EXPECT_EQ(goAroundLegs[1]->pathTerm(), "DF");
    EXPECT_EQ(goAroundLegs[1]->toNavaid(), "MAFIX");
    EXPECT_TRUE(goAroundLegs[1]->hasTargetPoint());
    EXPECT_NEAR(goAroundLegs[1]->targetPoint().latitude, 42.1, 0.0001);
    EXPECT_NEAR(goAroundLegs[1]->targetPoint().longitude, -87.9, 0.0001);
    EXPECT_FLOAT_EQ(goAroundLegs[1]->targetAltitude(), 4000.0f);
}
