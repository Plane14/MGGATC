//
// This file is part of AT&C project which simulates virtual world of air traffic and ATC.
// Code licensing terms are available at https://github.com/felix-b/atc/blob/master/LICENSE
//
#pragma once

#include <algorithm>

#include "XPLMDataAccess.h"
#include "libworld.h"

using namespace world;

class XPlaneAirportFlowProvider : public AirportFlowProvider
{
public:
    AirportFlowConditions getAirportFlowConditions(const Airport& airport) override
    {
        AirportFlowConditions conditions;
        conditions.windDirectionTrue = readArrayElement("sim/weather/wind_direction_degt", 0, 0.0f);
        conditions.windSpeedKt = readArrayElement("sim/weather/wind_speed_kt", 0, 0.0f);
        conditions.visibilitySm = std::max(0.0f, readScalar("sim/weather/visibility_reported_sm", 99.0f));
        conditions.ceilingFeet = queryCeilingFeet(airport);
        conditions.localTimeMinutes = ((int)readScalar("sim/time/local_time_sec", 0.0f) / 60) % (24 * 60);
        return conditions;
    }
private:
    static float readScalar(const char* dataRefName, float fallbackValue)
    {
        auto dataRef = XPLMFindDataRef(dataRefName);
        return dataRef
            ? XPLMGetDataf(dataRef)
            : fallbackValue;
    }

    static float readArrayElement(const char* dataRefName, int index, float fallbackValue)
    {
        auto dataRef = XPLMFindDataRef(dataRefName);
        if (!dataRef)
        {
            return fallbackValue;
        }

        float value = fallbackValue;
        return XPLMGetDatavf(dataRef, &value, index, 1) == 1
            ? value
            : fallbackValue;
    }

    static float queryCeilingFeet(const Airport& airport)
    {
        float lowestCeilingFeet = 99999.0f;
        bool foundCeiling = false;

        for (int layerIndex = 0 ; layerIndex < 3 ; layerIndex++)
        {
            float coverage = readArrayElement("sim/weather/cloud_coverage", layerIndex, 0.0f);
            if (coverage < 0.5f)
            {
                continue;
            }

            float baseMetersMsl = readArrayElement("sim/weather/cloud_base_msl_m", layerIndex, -1.0f);
            if (baseMetersMsl < 0.0f)
            {
                continue;
            }

            float baseFeetAgl = std::max(0.0f, baseMetersMsl * FEET_IN_1_METER - airport.header().elevation());
            if (!foundCeiling || baseFeetAgl < lowestCeilingFeet)
            {
                lowestCeilingFeet = baseFeetAgl;
                foundCeiling = true;
            }
        }

        return foundCeiling ? lowestCeilingFeet : 99999.0f;
    }
};
