//
// This file is part of AT&C project which simulates virtual world of air traffic and ATC.
// Code licensing terms are available at https://github.com/felix-b/atc/blob/master/LICENSE
//
#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <istream>

#include "libworld.h"

using namespace std;

namespace world
{
    class AircraftPerformanceProfileService
    {
    public:
        struct Profile
        {
            string icao;
            string doc8643Class;
            string wakeTurbulenceCategory;
            string takeOffRecat;
            string landingApc;
            double wingSpanMeters = 0;
            double lengthMeters = 0;
            double heightMeters = 0;
            int takeOffV2Kt = 140;
            int takeOffDistanceMeters = 1800;
            int takeOffMtowKg = 0;
            int initialClimbIASKt = 160;
            int initialClimbRocFpm = 2500;
            int climb150IASKt = 190;
            int climb150RocFpm = 2000;
            int climb240IASKt = 220;
            int climb240RocFpm = 1500;
            double machClimbMach = 0.70;
            int machClimbRocFpm = 1000;
            int cruiseTasKt = 430;
            double cruiseMach = 0.76;
            int cruiseCeilingFl = 390;
            int cruiseRangeNm = 0;
            double initialDescentMach = 0.72;
            int initialDescentRodFpm = 800;
            int descentIASKt = 250;
            int descentRodFpm = 3000;
            int approachIASKt = 180;
            int approachRodFpm = 1200;
            int approachMcsKt = 0;
            int landingVatKt = 140;
            int landingDistanceMeters = 1400;
            double taxiNormalMetersPerSecond = 6.0;
            double taxiHighSpeedMetersPerSecond = 10.0;
            double taxiPushbackMetersPerSecond = 1.0;
            Aircraft::Category category = Aircraft::Category::Jet;
            bool hasPerformanceData = false;
            bool hasDoc8643Reference = false;
        };

    private:
        unordered_map<string, Profile> m_profiles;
        mutable unordered_map<string, Profile> m_unknownProfiles;
        mutable mutex m_unknownProfilesMutex;

    public:
        explicit AircraftPerformanceProfileService(shared_ptr<HostServices> host);
        explicit AircraftPerformanceProfileService(const unordered_map<string, Profile>& profiles);

        const Profile& resolve(const string& icao) const;

        static unordered_map<string, Profile> parseBundledProfiles(istream& performanceCsv, istream& doc8643);
        static Profile createSafeDefault(
            const string& icao,
            const string& doc8643Class = "",
            const string& wakeTurbulenceCategory = "");
    };
}
