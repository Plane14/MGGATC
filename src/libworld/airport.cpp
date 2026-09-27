// 
// This file is part of AT&C project which simulates virtual world of air traffic and ATC.
// Code licensing terms are available at https://github.com/felix-b/atc/blob/master/LICENSE
// 
#include <algorithm>
#include <memory>
#include <sstream>
#include "libworld.h"

namespace world
{
    static bool isValueWithinWrappedRange(int value, int minValue, int maxValue, int wrapValue)
    {
        while (value < 0)
        {
            value += wrapValue;
        }
        while (value >= wrapValue)
        {
            value -= wrapValue;
        }
        while (minValue < 0)
        {
            minValue += wrapValue;
        }
        while (maxValue < 0)
        {
            maxValue += wrapValue;
        }
        while (minValue >= wrapValue)
        {
            minValue -= wrapValue;
        }
        while (maxValue >= wrapValue)
        {
            maxValue -= wrapValue;
        }

        return minValue <= maxValue
            ? value >= minValue && value <= maxValue
            : value >= minValue || value <= maxValue;
    }

    bool AirportTrafficFlow::matches(const AirportFlowConditions& conditions) const
    {
        if (hasWindRule)
        {
            if (conditions.windSpeedKt > maxWindSpeedKt)
            {
                return false;
            }
            if (!isValueWithinWrappedRange(
                (int)conditions.windDirectionTrue,
                minWindDirection,
                maxWindDirection,
                360))
            {
                return false;
            }
        }

        if (hasCeilingRule && conditions.ceilingFeet < minCeilingFeet)
        {
            return false;
        }

        if (hasVisibilityRule && conditions.visibilitySm < minVisibilitySm)
        {
            return false;
        }

        if (hasTimeRule && !isValueWithinWrappedRange(
            conditions.localTimeMinutes,
            startTimeMinutes,
            endTimeMinutes,
            24 * 60))
        {
            return false;
        }

        return true;
    }

    const AirportTrafficFlow* Airport::findMatchingTrafficFlow(const AirportFlowConditions& conditions) const
    {
        for (const auto& flow : m_trafficFlows)
        {
            if (flow.matches(conditions))
            {
                return &flow;
            }
        }

        return nullptr;
    }

    bool Airport::selectRunwaysForFlowConditions(
        const AirportFlowConditions& conditions,
        vector<string>& departureRunways,
        vector<string>& arrivalRunways) const
    {
        const auto* flow = findMatchingTrafficFlow(conditions);
        if (!flow)
        {
            return false;
        }

        departureRunways.clear();
        arrivalRunways.clear();

        const auto appendUnique = [](vector<string>& names, const string& runwayName) {
            if (find(names.begin(), names.end(), runwayName) == names.end())
            {
                names.push_back(runwayName);
            }
        };

        for (const auto& runwayUse : flow->runwayUses)
        {
            if (runwayUse.departures)
            {
                appendUnique(departureRunways, runwayUse.runwayName);
            }
            if (runwayUse.arrivals)
            {
                appendUnique(arrivalRunways, runwayUse.runwayName);
            }
        }

        return !departureRunways.empty() || !arrivalRunways.empty();
    }

    shared_ptr<Runway> Airport::findLongestRunway() const
    {
        const auto compareRunwayLength = [](const shared_ptr<Runway>& r1, const shared_ptr<Runway>& r2) {
            return (r1->lengthMeters() < r2->lengthMeters());
        };

        auto it = max_element(m_runways.begin(), m_runways.end(), compareRunwayLength);
        shared_ptr<Runway> longestRunway = it != m_runways.end()
           ? *it
           : nullptr;

        if (!longestRunway)
        {
            stringstream errorMessage;
            errorMessage
                << "Could not find longest runway at ["
                << m_header.icao()
                << "] are there any runways at this airport??";
            throw runtime_error(errorMessage.str());
        }

        return longestRunway;
    }

    const vector<shared_ptr<Runway>>& Airport::findLongestParallelRunwayGroup() const
    {
        const auto calcAverageRunwayLength = [](const vector<shared_ptr<Runway>>& group)->float {
            float sum = 0;
            for (int i = 0 ; i < group.size() ; i++)
            {
                sum += group[i]->lengthMeters();
            }
            return group.size() > 0 ? sum / group.size() : 0;
        };

        vector<float> averages;
        transform(
            m_parallelRunwayGroups.begin(),
            m_parallelRunwayGroups.end(),
            back_inserter(averages),
            calcAverageRunwayLength);

        int indexOfMaxAverage = distance(averages.begin(), max_element(averages.begin(), averages.end()));
        return m_parallelRunwayGroups.at(indexOfMaxAverage);
    }

    shared_ptr<Runway> Airport::getRunwayOrThrow(const string& name) const
    {
        auto runway = tryFindRunway(name);
        if (!runway)
        {
            stringstream errorMessage;
            errorMessage 
                << "Runway '" << name 
                << "' could not be found at airport '" << m_header.icao() << "'";
            throw runtime_error(errorMessage.str());
        }
        return runway;
    }

    const Runway::End& Airport::getRunwayEndOrThrow(const string& name) const
    {
        auto runway = getRunwayOrThrow(name);
        return runway->getEndOrThrow(name);
    }

    shared_ptr<Runway> Airport::tryFindRunway(const string& name) const
    {
        auto found = m_runwayByName.find(name);
        return found != m_runwayByName.end()
            ? found->second
            : nullptr;
    }

    bool Airport::isRunwayActive(const string& runwayName) const
    {
        auto runwayToCheck = getRunwayOrThrow(runwayName);

        for (const auto& name : m_mutableState->activeArrivalRunways)
        {
            auto arrival = getRunwayOrThrow(name);
            if (arrival == runwayToCheck)
            {
                return true;
            }
        }

        for (const auto& name : m_mutableState->activeDepartureRunways)
        {
            auto departure = getRunwayOrThrow(name);
            if (departure == runwayToCheck)
            {
                return true;
            }
        }

        return false;
    }

    shared_ptr<ParkingStand> Airport::getParkingStandOrThrow(const string& name) const
    {
        auto parking = tryFindParkingStand(name);
        if (!parking)
        {
            throw runtime_error("Specified parking stand could not be found");
        }
        return parking;
    }

    shared_ptr<ParkingStand> Airport::tryFindParkingStand(const string& name) const
    {
        auto found = m_parkingStandByName.find(name);
        return found != m_parkingStandByName.end()
            ? found->second
            : nullptr;
    }

    shared_ptr<ParkingStand> Airport::findClosestParkingStand(const GeoPoint& location)
    {
        ClosestItemFinder<ParkingStand> finder(location);
        for (const auto& gate : m_parkingStands)
        {
            finder.next(gate);
        }
        return finder.getClosest();
    }

    void Airport::selectActiveRunways()
    {
        if (!m_tower)
        {
            return;
        }

        for (const auto& position : m_tower->positions())
        {
            if (position->type() == ControllerPosition::Type::Local)
            {
                position->selectActiveRunways(
                    m_mutableState->activeDepartureRunways,
                    m_mutableState->activeArrivalRunways);
            }
        }

        calculateActiveRunwaysBounds();
    }

    void Airport::selectArrivalAndDepartureTaxiways()
    {
        for (const auto& departureRunwayName : m_mutableState->activeDepartureRunways)
        {
            const auto& runwayEnd = getRunwayOrThrow(departureRunwayName)->getEndOrThrow(departureRunwayName);

            for (const auto &gate : m_parkingStands)
            {
                m_taxiNet->tryFindDepartureTaxiPathToRunway(gate->location().geo(), runwayEnd);
            }
        }
    }

    void Airport::calculateActiveRunwaysBounds()
    {
        for (const auto& runway : m_mutableState->activeArrivalRunways)
        {
            getRunwayOrThrow(runway)->calculateBounds();
        }

        for (const auto& runway : m_mutableState->activeDepartureRunways)
        {
            getRunwayOrThrow(runway)->calculateBounds();
        }
    }
}
