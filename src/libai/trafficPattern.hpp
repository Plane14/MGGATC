//
// This file is part of AT&C project which simulates virtual world of air traffic and ATC.
// Code licensing terms are available at https://github.com/felix-b/atc/blob/master/LICENSE
//
#pragma once

#include <algorithm>
#include <chrono>

#include "libworld.h"
#include "aircraftPerformanceProfileService.hpp"
#include "aircraftPerformanceModel.hpp"

using namespace std;
using namespace world;

namespace ai
{
    namespace traffic_pattern
    {
        struct Geometry
        {
            GeoPoint downwindEntry;
            GeoPoint baseTurn;
            GeoPoint finalStart;
            float downwindHeading = 0.0f;
            float baseHeading = 0.0f;
            float finalHeading = 0.0f;
            float patternAltitudeFeetAgl = 1000.0f;
            float downwindSpeedKt = 90.0f;
            float baseSpeedKt = 80.0f;
            float finalSpeedKt = 70.0f;
            float finalSinkRateFpm = 500.0f;
            chrono::microseconds downwindDuration = chrono::seconds(45);
            chrono::microseconds baseDuration = chrono::seconds(25);
        };

        inline bool shouldUseAdvisoryPattern(shared_ptr<Airport> airport, shared_ptr<Flight> flight)
        {
            return (
                airport &&
                flight &&
                airport->isAdvisoryOnly() &&
                flight->rules() != Flight::RulesType::IFR);
        }

        inline Geometry buildLeftPattern(
            const AircraftPerformanceProfileService::Profile& profile,
            const Runway::End& runwayEnd,
            Aircraft::Category category)
        {
            const bool helicopter =
                (category & Aircraft::Category::Helicopter) == Aircraft::Category::Helicopter;
            const float patternAltitudeFeetAgl = helicopter ? 500.0f : 1000.0f;
            const float lateralOffsetNm = helicopter ? 0.35f : 0.8f;
            const float finalSinkRateFpm = max(helicopter ? 400.0f : 500.0f, (float)profile.approachRodFpm);
            const float finalSpeedKt = max(
                helicopter ? 55.0f : 70.0f,
                min(
                    helicopter ? 95.0f : 140.0f,
                    (float)performance_model::calcFinalApproachGroundSpeedKt(profile)));
            const float downwindSpeedKt = max(
                helicopter ? 65.0f : 90.0f,
                min(
                    helicopter ? 110.0f : 180.0f,
                    finalSpeedKt + (helicopter ? 10.0f : 20.0f)));
            const float baseSpeedKt = max(
                helicopter ? 60.0f : 80.0f,
                min(
                    downwindSpeedKt,
                    finalSpeedKt + (helicopter ? 5.0f : 10.0f)));
            const float finalDistanceNm = max(
                helicopter ? 0.8f : 2.0f,
                (patternAltitudeFeetAgl + 60.0f) / finalSinkRateFpm * finalSpeedKt / 60.0f);
            const float downwindExtensionNm = max(
                helicopter ? 0.8f : 1.4f,
                finalDistanceNm * 0.75f);
            const float finalDistanceMeters = max(
                helicopter ? 1200.0f : 2500.0f,
                finalDistanceNm * (float)METERS_IN_1_NAUTICAL_MILE -
                    runwayEnd.displacedThresholdMeters() -
                    50.0f);

            const GeoPoint threshold = runwayEnd.centerlinePoint().geo();
            const float finalHeading = runwayEnd.heading();
            const float downwindHeading = GeoMath::flipHeading(finalHeading);
            const float baseHeading = GeoMath::addTurnToHeading(downwindHeading, -90.0f);
            const float leftOffsetHeading = GeoMath::addTurnToHeading(finalHeading, -90.0f);
            const GeoPoint finalStart = GeoMath::getPointAtDistance(
                threshold,
                GeoMath::flipHeading(finalHeading),
                finalDistanceMeters);
            const GeoPoint baseTurn = GeoMath::getPointAtDistance(
                finalStart,
                leftOffsetHeading,
                lateralOffsetNm * (float)METERS_IN_1_NAUTICAL_MILE);
            const GeoPoint downwindEntry = GeoMath::getPointAtDistance(
                baseTurn,
                finalHeading,
                downwindExtensionNm * (float)METERS_IN_1_NAUTICAL_MILE);

            const auto durationForLeg = [](const GeoPoint& from, const GeoPoint& to, float speedKt) {
                const double speedMetersPerSecond = max(1.0, speedKt / KNOT_IN_1_METER_PER_SEC);
                const double seconds = GeoMath::getDistanceMeters(from, to) / speedMetersPerSecond;
                return chrono::microseconds((long long)(seconds * 1000000.0));
            };

            Geometry geometry;
            geometry.downwindEntry = downwindEntry;
            geometry.baseTurn = baseTurn;
            geometry.finalStart = finalStart;
            geometry.downwindHeading = downwindHeading;
            geometry.baseHeading = baseHeading;
            geometry.finalHeading = finalHeading;
            geometry.patternAltitudeFeetAgl = patternAltitudeFeetAgl;
            geometry.downwindSpeedKt = downwindSpeedKt;
            geometry.baseSpeedKt = baseSpeedKt;
            geometry.finalSpeedKt = finalSpeedKt;
            geometry.finalSinkRateFpm = finalSinkRateFpm;
            geometry.downwindDuration = durationForLeg(downwindEntry, baseTurn, downwindSpeedKt);
            geometry.baseDuration = durationForLeg(baseTurn, finalStart, baseSpeedKt);

            return geometry;
        }
    }
}
