//
// This file is part of AT&C project which simulates virtual world of air traffic and ATC.
// Code licensing terms are available at https://github.com/felix-b/atc/blob/master/LICENSE
//
#pragma once

#include <algorithm>
#include <chrono>

#include "aircraftPerformanceProfileService.hpp"

using namespace std;
using namespace world;

namespace ai
{
    namespace performance_model
    {
        inline double knotsToMetersPerSecond(double value)
        {
            return value * 0.514444;
        }

        inline double metersPerSecondToKnots(double value)
        {
            return value * 1.943844;
        }

        inline chrono::milliseconds durationFromDistanceMeters(double distanceMeters, double fromKt, double toKt)
        {
            double averageKt = max(5.0, (fromKt + toKt) / 2.0);
            double averageMetersPerSecond = knotsToMetersPerSecond(averageKt);
            return chrono::milliseconds(max(1000, (int)(1000.0 * distanceMeters / averageMetersPerSecond)));
        }

        inline chrono::milliseconds calcTakeoffRollDuration(
            const AircraftPerformanceProfileService::Profile& profile,
            double startSpeedKt)
        {
            return durationFromDistanceMeters(profile.takeOffDistanceMeters, startSpeedKt, profile.takeOffV2Kt);
        }

        inline chrono::milliseconds calcLiftOffDuration(const AircraftPerformanceProfileService::Profile& profile)
        {
            double seconds = 24000.0 / max(600, profile.initialClimbRocFpm);
            return chrono::milliseconds((int)(1000.0 * min(20.0, max(8.0, seconds))));
        }

        inline chrono::milliseconds calcAirborneAccelerationDuration(const AircraftPerformanceProfileService::Profile& profile)
        {
            double seconds = 1500.0 * 60.0 / max(600, profile.initialClimbRocFpm);
            return chrono::milliseconds((int)(1000.0 * min(30.0, max(8.0, seconds))));
        }

        inline chrono::milliseconds calcPostTakeoffTransitionDuration(const AircraftPerformanceProfileService::Profile& profile)
        {
            double seconds = 900.0 * 60.0 / max(500, profile.climb150RocFpm);
            return chrono::milliseconds((int)(1000.0 * min(25.0, max(6.0, seconds))));
        }

        inline chrono::milliseconds calcLandingRollDuration(const AircraftPerformanceProfileService::Profile& profile)
        {
            double touchdownSpeedKt = max(40, profile.landingVatKt - max(5, profile.landingVatKt / 15));
            double rolloutExitSpeedKt = max(15.0, metersPerSecondToKnots(profile.taxiHighSpeedMetersPerSecond));
            return durationFromDistanceMeters(profile.landingDistanceMeters, touchdownSpeedKt, rolloutExitSpeedKt);
        }

        inline double calcFinalApproachMinutes(const AircraftPerformanceProfileService::Profile& profile)
        {
            return min(4.0, max(1.8, 3000.0 / max(800, profile.approachRodFpm)));
        }

        inline int calcFinalApproachGroundSpeedKt(const AircraftPerformanceProfileService::Profile& profile)
        {
            return max(60, profile.landingVatKt);
        }

        inline int calcPreFlareSinkRateFpm(const AircraftPerformanceProfileService::Profile& profile)
        {
            return max(200, min(800, profile.approachRodFpm / 2));
        }

        inline int calcTouchdownSpeedKt(const AircraftPerformanceProfileService::Profile& profile)
        {
            return max(40, profile.landingVatKt - max(5, profile.landingVatKt / 15));
        }

        inline int calcRolloutExitSpeedKt(const AircraftPerformanceProfileService::Profile& profile)
        {
            return max(15, (int)metersPerSecondToKnots(profile.taxiHighSpeedMetersPerSecond));
        }
    }
}
