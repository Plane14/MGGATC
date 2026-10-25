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
        /// Wind component along the runway centerline.
        /// Positive headwindComponent = headwind (reduces takeoff/landing distance).
        /// Negative headwindComponent = tailwind (increases takeoff/landing distance).
        struct WindInfo
        {
            double headwindComponentKt = 0.0; ///< Headwind (+forward) or tailwind (-forward) in kt
        };

        /// Runway surface condition affecting performance calculations.
        enum class SurfaceCondition
        {
            Dry, ///< Dry runway (reference condition, factor 1.0)
            Wet, ///< Wet runway (factor 1.15)
            Icy, ///< Icy/snow-covered runway (factor 1.40)
            Snow, ///< Snow-covered runway (factor 1.30)
        };

        /// Approximate runway surface multipliers for takeoff/landing distance.
        /// (Wet ≈ +15%, Icy ≈ +40%, Snow ≈ +30% per FAA AC 150/5320-6)
        inline double surfaceDistanceFactor(SurfaceCondition cond)
        {
            switch (cond)
            {
                case SurfaceCondition::Wet:  return 1.15;
                case SurfaceCondition::Icy:  return 1.40;
                case SurfaceCondition::Snow: return 1.30;
                case SurfaceCondition::Dry:
                default:                     return 1.0;
            }
        }

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
             double startSpeedKt,
             const WindInfo& wind = {},
             SurfaceCondition surface = SurfaceCondition::Dry)
         {
             // Headwind reduces takeoff distance; tailwind increases it.
             // Approximate: ±5% per 10 kt of headwind/tailwind component.
             double windFactor = 1.0 - (wind.headwindComponentKt / 200.0); // ±5% per 10 kt
             double distanceMeters = profile.takeOffDistanceMeters * windFactor * surfaceDistanceFactor(surface);
             return durationFromDistanceMeters(distanceMeters, startSpeedKt, profile.takeOffV2Kt);
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

         inline chrono::milliseconds calcLandingRollDuration(
             const AircraftPerformanceProfileService::Profile& profile,
             const WindInfo& wind = {},
             SurfaceCondition surface = SurfaceCondition::Dry)
         {
             double touchdownSpeedKt = max(40, profile.landingVatKt - max(5, profile.landingVatKt / 15));
             double rolloutExitSpeedKt = max(15.0, metersPerSecondToKnots(profile.taxiHighSpeedMetersPerSecond));
             // Same wind factor applies; tailwind increases landing distance
             double windFactor = 1.0 - (wind.headwindComponentKt / 200.0);
             double distanceMeters = profile.landingDistanceMeters * windFactor * surfaceDistanceFactor(surface);
             return durationFromDistanceMeters(distanceMeters, touchdownSpeedKt, rolloutExitSpeedKt);
         }

        inline double calcFinalApproachMinutes(const AircraftPerformanceProfileService::Profile& profile)
        {
            return min(4.0, max(1.8, 3000.0 / max(800, profile.approachRodFpm)));
        }

         inline int calcFinalApproachGroundSpeedKt(
             const AircraftPerformanceProfileService::Profile& profile,
             const WindInfo& wind = {})
         {
             // Ground speed = airspeed + wind component (tailwind reduces groundspeed on approach)
             return max(60, profile.landingVatKt - (int)wind.headwindComponentKt);
         }

         inline int calcPreFlareSinkRateFpm(
             const AircraftPerformanceProfileService::Profile& profile,
             SurfaceCondition surface = SurfaceCondition::Dry)
         {
             // Reduce sink rate on slippery surfaces for smoother touchdown
             double factor = 1.0;
             switch (surface)
             {
                 case SurfaceCondition::Icy:
                 case SurfaceCondition::Snow: factor = 0.85; break;
                 case SurfaceCondition::Wet:  factor = 0.90; break;
                 case SurfaceCondition::Dry:
                 default:                     factor = 1.0;  break;
             }
             return max(200, min(800, (int)(profile.approachRodFpm / 2 * factor)));
         }

         inline int calcTouchdownSpeedKt(
             const AircraftPerformanceProfileService::Profile& profile,
             const WindInfo& wind = {})
         {
             // Tailwind increases touchdown speed slightly
             return max(40, profile.landingVatKt - max(5, profile.landingVatKt / 15) + (int)(wind.headwindComponentKt / 10.0));
         }

        inline int calcRolloutExitSpeedKt(const AircraftPerformanceProfileService::Profile& profile)
        {
            return max(15, (int)metersPerSecondToKnots(profile.taxiHighSpeedMetersPerSecond));
        }
    }
}
