// 
// This file is part of AT&C project which simulates virtual world of air traffic and ATC.
// Code licensing terms are available at https://github.com/felix-b/atc/blob/master/LICENSE
// 
#pragma once

#include <cstring>
#include <cctype>
#include <string>
#include <chrono>
#include <queue>
#include <vector>
#include <random>
#include <set>
#include <sstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <numeric>

#if IBM
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

// SDK
#include "XPLMProcessing.h"
#include "XPLMNavigation.h"

// PPL
#include "owneddata.h"

// tnc
#include "libworld.h"
#include "airlineReferenceTable.hpp"
#include "aircraftPerformanceProfileService.hpp"
#include "runtimeScheduleSupport.hpp"

using namespace std;
using namespace PPL;
using namespace world;

class DemoScheduleLoader
{
private:
    struct AircraftOption
    {
        string modelIcao;
        string airlineIcao;
        string callSignPrefix;
    };

    struct AssignedLiveSchedule
    {
        runtime_schedule::LiveScheduleCandidate candidate;
        shared_ptr<ParkingStand> gate;
        time_t arrivalTime = 0;
        time_t departureTime = 0;
    };

    shared_ptr<HostServices> m_host;
    shared_ptr<World> m_world;
    DataRef<double> m_userAircraftLatitude;
    DataRef<double> m_userAircraftLongitude;
    shared_ptr<Airport> m_airport;
public:
    DemoScheduleLoader(shared_ptr<HostServices> _host, shared_ptr<World> _world) :
        m_host(_host),
        m_world(_world),
        m_userAircraftLatitude("sim/flightmodel/position/latitude", PPL::ReadOnly),
        m_userAircraftLongitude("sim/flightmodel/position/longitude", PPL::ReadOnly)
    {
    }
public:
    void loadSchedules(float loadFactor)
    {
        string userAirportIcao = getUserAirportIcao();
        m_airport = m_world->getAirport(userAirportIcao);
        m_airport->selectActiveRunways();
        m_airport->selectArrivalAndDepartureTaxiways();
        logActiveRunwaysBounds();

        m_host->writeLog("SCHEDL|Loading AI schedules at airport[%s]", m_airport->header().icao().c_str());

        bool loadedLiveSchedules = false;
        try
        {
            loadedLiveSchedules = tryLoadRealSchedules(loadFactor);
        }
        catch (const exception& e)
        {
            m_host->writeLog("SCHEDL|adsb.lol runtime schedule load failed: %s", e.what());
        }

        if (!loadedLiveSchedules)
        {
            m_host->writeLog("SCHEDL|Falling back to bundled demo schedules");
            initDemoSchedules(loadFactor, m_world->currentTime() + 200, m_world->currentTime() + 30);
        }

        m_host->writeLog(
            "SCHEDL|Loaded [%d] AI flights at airport[%s]",
            m_world->flights().size(),
            m_airport->header().icao().c_str());
    }

public:

    shared_ptr<Airport> airport() const { return m_airport; }

private:

    string getUserAirportIcao()
    {
        char airportIcaoId[10] = { 0 };
        float lat = m_userAircraftLatitude;
        float lon = m_userAircraftLongitude;
        m_host->writeLog("SCHEDL|User airport lookup: user aircraft is at (%f,%f)", lat, lon);

        XPLMNavRef navRef = XPLMFindNavAid( nullptr, nullptr, &lat, &lon, nullptr, xplm_Nav_Airport);
        if (navRef != XPLM_NAV_NOT_FOUND)
        {
            XPLMGetNavAidInfo(navRef, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, airportIcaoId, nullptr, nullptr);
        }

        if (strlen(airportIcaoId) > 0)
        {
            m_host->writeLog("SCHEDL|User airport lookup: FOUND [%s]", airportIcaoId);
            return airportIcaoId;
        }

        GeoPoint userAircraftLocation(lat, lon, 0);
        auto closestAirport = findClosestAirport(userAircraftLocation, m_world->airports());
        if (closestAirport)
        {
            m_host->writeLog(
                "SCHEDL|User airport lookup: nav lookup failed, using closest loaded airport [%s]",
                closestAirport->header().icao().c_str());
            return closestAirport->header().icao();
        }

        throw runtime_error("Could not determine a usable airport for schedule loading");
    }

    void selectActiveRunwayNames(string& departureRunway, string& arrivalRunway1, string& arrivalRunway2) const
    {
        const auto& departure = m_airport->activeDepartureRunways();
        const auto& arrival = m_airport->activeArrivalRunways();

        departureRunway = !departure.empty() ? departure.at(0) : "";
        arrivalRunway1 = !arrival.empty() ? arrival.at(0) : "";
        arrivalRunway2 = !arrival.empty() ? arrival.at(arrival.size() - 1) : arrivalRunway1;
    }

    string formatCoordinate(double value) const
    {
        stringstream text;
        text << fixed << setprecision(4) << value;
        return text.str();
    }

    string buildPointLookupUrl() const
    {
        const auto& airportLocation = m_airport->header().datum();
        return
            "https://api.adsb.lol/v2/point/" +
            formatCoordinate(airportLocation.latitude) + "/" +
            formatCoordinate(airportLocation.longitude) + "/80";
    }

    string buildRouteLookupUrl(const runtime_schedule::LiveAircraft& aircraft) const
    {
        return
            "https://api.adsb.lol/api/0/route/" +
            aircraft.callSign + "/" +
            formatCoordinate(aircraft.location.latitude) + "/" +
            formatCoordinate(aircraft.location.longitude);
    }

#if IBM
    string quoteCommandArgument(const string& argument) const
    {
        string quoted = "\"";
        size_t backslashCount = 0;

        for (char c : argument)
        {
            if (c == '\\')
            {
                backslashCount++;
                continue;
            }

            if (c == '"')
            {
                quoted.append(backslashCount * 2 + 1, '\\');
                quoted.push_back('"');
                backslashCount = 0;
                continue;
            }

            if (backslashCount > 0)
            {
                quoted.append(backslashCount, '\\');
                backslashCount = 0;
            }

            quoted.push_back(c);
        }

        if (backslashCount > 0)
        {
            quoted.append(backslashCount * 2, '\\');
        }

        quoted.push_back('"');
        return quoted;
    }
#endif

    string executeCurl(const vector<string>& arguments) const
    {
#if IBM
        SECURITY_ATTRIBUTES securityAttributes;
        ZeroMemory(&securityAttributes, sizeof(securityAttributes));
        securityAttributes.nLength = sizeof(securityAttributes);
        securityAttributes.bInheritHandle = TRUE;

        HANDLE readPipe = nullptr;
        HANDLE writePipe = nullptr;
        if (!CreatePipe(&readPipe, &writePipe, &securityAttributes, 0))
        {
            throw runtime_error("CreatePipe failed");
        }

        if (!SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0))
        {
            CloseHandle(readPipe);
            CloseHandle(writePipe);
            throw runtime_error("SetHandleInformation failed");
        }

        STARTUPINFOA startupInfo;
        ZeroMemory(&startupInfo, sizeof(startupInfo));
        startupInfo.cb = sizeof(startupInfo);
        startupInfo.dwFlags = STARTF_USESTDHANDLES;
        startupInfo.hStdOutput = writePipe;
        startupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

        PROCESS_INFORMATION processInfo;
        ZeroMemory(&processInfo, sizeof(processInfo));

        string commandLine;
        for (size_t i = 0 ; i < arguments.size() ; i++)
        {
            if (i > 0)
            {
                commandLine += " ";
            }
            commandLine += quoteCommandArgument(arguments[i]);
        }

        vector<char> mutableCommandLine(commandLine.begin(), commandLine.end());
        mutableCommandLine.push_back('\0');

        BOOL started = CreateProcessA(
            nullptr,
            mutableCommandLine.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startupInfo,
            &processInfo);

        CloseHandle(writePipe);
        if (!started)
        {
            CloseHandle(readPipe);
            throw runtime_error("CreateProcess failed");
        }

        string output;
        char buffer[4096];
        DWORD bytesRead = 0;
        while (ReadFile(readPipe, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0)
        {
            output.append(buffer, bytesRead);
        }

        CloseHandle(readPipe);
        WaitForSingleObject(processInfo.hProcess, INFINITE);

        DWORD exitCode = 0;
        GetExitCodeProcess(processInfo.hProcess, &exitCode);
        CloseHandle(processInfo.hProcess);
        CloseHandle(processInfo.hThread);

        if (exitCode != 0)
        {
            throw runtime_error("curl exited with a non-zero status");
        }

        return output;
#else
        int outputPipe[2];
        if (pipe(outputPipe) != 0)
        {
            throw runtime_error("pipe failed");
        }

        pid_t processId = fork();
        if (processId < 0)
        {
            close(outputPipe[0]);
            close(outputPipe[1]);
            throw runtime_error("fork failed");
        }

        if (processId == 0)
        {
            dup2(outputPipe[1], STDOUT_FILENO);
            close(outputPipe[0]);
            close(outputPipe[1]);

            vector<char*> argv;
            argv.reserve(arguments.size() + 1);
            for (const auto& argument : arguments)
            {
                argv.push_back(const_cast<char*>(argument.c_str()));
            }
            argv.push_back(nullptr);

            execvp(argv[0], argv.data());
            _exit(127);
        }

        close(outputPipe[1]);
        string output;
        char buffer[4096];
        ssize_t bytesRead = 0;
        while ((bytesRead = read(outputPipe[0], buffer, sizeof(buffer))) > 0)
        {
            output.append(buffer, bytesRead);
        }

        close(outputPipe[0]);
        int exitStatus = 0;
        waitpid(processId, &exitStatus, 0);
        if (!WIFEXITED(exitStatus) || WEXITSTATUS(exitStatus) != 0)
        {
            throw runtime_error("curl exited with a non-zero status");
        }

        return output;
#endif
    }

    string downloadTextFromUrl(const string& url) const
    {
        return executeCurl({
            "curl",
            "-LfsS",
            "--connect-timeout", "5",
            "--max-time", "15",
            url
        });
    }

    string resolveCallSignPrefix(const AircraftOption& aircraftOption, const string& airlineIcao) const
    {
        AirlineReferenceTable::Entry airline;
        string normalizedAirlineIcao = runtime_schedule::upper(airlineIcao);
        if (!normalizedAirlineIcao.empty() && AirlineReferenceTable::tryFindByIcao(normalizedAirlineIcao, airline))
        {
            if (!airline.callsign.empty())
            {
                return airline.callsign;
            }
        }

        return aircraftOption.callSignPrefix;
    }

    string buildFallbackCallsign(
        const AircraftOption& aircraftOption,
        int flightId,
        const string& airlineIcao,
        const string& flightNo) const
    {
        string effectiveFlightNo = flightNo.empty()
            ? to_string(flightId)
            : runtime_schedule::upper(flightNo);
        string effectiveAirlineIcao = airlineIcao.empty()
            ? aircraftOption.airlineIcao
            : runtime_schedule::upper(airlineIcao);
        string callSignPrefix = resolveCallSignPrefix(aircraftOption, effectiveAirlineIcao);

        if (callSignPrefix.empty())
        {
            return effectiveFlightNo;
        }

        string spokenFlightNo = effectiveFlightNo;
        if (
            effectiveAirlineIcao.length() == 3 &&
            effectiveFlightNo.length() > 3 &&
            runtime_schedule::upper(effectiveFlightNo.substr(0, 3)) == effectiveAirlineIcao)
        {
            spokenFlightNo = effectiveFlightNo.substr(3);
        }

        return spokenFlightNo.empty()
            ? callSignPrefix
            : callSignPrefix + " " + spokenFlightNo;
    }

    shared_ptr<Flight> createOutboundFlight(
        const AircraftOption& aircraftOption,
        int flightId,
        const string& destination,
        time_t departureTime,
        shared_ptr<ParkingStand> gate,
        const string& departureRunway,
        const string& airlineIcao = "",
        const string& flightNo = "",
        const string& callSign = "",
        const string& tailNo = "")
    {
        string effectiveAirlineIcao = airlineIcao.empty()
            ? aircraftOption.airlineIcao
            : runtime_schedule::upper(airlineIcao);
        string effectiveFlightNo = flightNo.empty()
            ? (!callSign.empty() ? runtime_schedule::sanitizeCallsign(callSign) : to_string(flightId))
            : runtime_schedule::upper(flightNo);
        string effectiveCallSign = callSign.empty()
            ? buildFallbackCallsign(aircraftOption, flightId, effectiveAirlineIcao, effectiveFlightNo)
            : callSign;
        string effectiveTailNo = tailNo.empty()
            ? effectiveFlightNo
            : tailNo;

        auto flightPlan = shared_ptr<FlightPlan>(new FlightPlan(
            departureTime,
            departureTime + 60 * 60 * 3,
            m_airport->header().icao(),
            destination));
        flightPlan->setDepartureGate(gate->name());
        flightPlan->setDepartureRunway(departureRunway);
        flightPlan->setAirlineIcao(effectiveAirlineIcao);
        flightPlan->setFlightNo(effectiveFlightNo);
        flightPlan->setCallsign(effectiveCallSign);

        auto destinationAirport = m_host->getWorld()->getAirport(destination);
        flightPlan->setArrivalRunway(destinationAirport->findLongestRunway()->end1().name());

        auto flight = shared_ptr<Flight>(new Flight(
            m_host,
            flightId,
            Flight::RulesType::IFR,
            effectiveAirlineIcao,
            effectiveFlightNo,
            effectiveCallSign,
            flightPlan));

        auto aircraft = m_host->createAIAircraft(
            aircraftOption.modelIcao,
            effectiveAirlineIcao,
            effectiveTailNo,
            m_host->services().get<AircraftPerformanceProfileService>()->resolve(aircraftOption.modelIcao).category);
        flight->setAircraft(aircraft);

        auto pilot = m_host->createAIPilot(flight);
        flight->setPilot(pilot);
        flight->setPhase(Flight::Phase::TurnAround);

        return flight;
    }

    shared_ptr<Flight> createInboundFlight(
        const AircraftOption& aircraftOption,
        int flightId,
        const string& origin,
        time_t arrivalTime,
        shared_ptr<ParkingStand> gate,
        const string& arrivalRunway,
        const string& airlineIcao = "",
        const string& flightNo = "",
        const string& callSign = "",
        const string& tailNo = "")
    {
        string effectiveAirlineIcao = airlineIcao.empty()
            ? aircraftOption.airlineIcao
            : runtime_schedule::upper(airlineIcao);
        string effectiveFlightNo = flightNo.empty()
            ? (!callSign.empty() ? runtime_schedule::sanitizeCallsign(callSign) : to_string(flightId))
            : runtime_schedule::upper(flightNo);
        string effectiveCallSign = callSign.empty()
            ? buildFallbackCallsign(aircraftOption, flightId, effectiveAirlineIcao, effectiveFlightNo)
            : callSign;
        string effectiveTailNo = tailNo.empty()
            ? effectiveFlightNo
            : tailNo;

        auto flightPlan = shared_ptr<FlightPlan>(new FlightPlan(
            arrivalTime - 60 * 60 * 3,
            arrivalTime,
            origin,
            m_airport->header().icao()));
        flightPlan->setArrivalGate(gate->name());
        flightPlan->setArrivalRunway(arrivalRunway);
        flightPlan->setAirlineIcao(effectiveAirlineIcao);
        flightPlan->setFlightNo(effectiveFlightNo);
        flightPlan->setCallsign(effectiveCallSign);

        auto flight = shared_ptr<Flight>(new Flight(
            m_host,
            flightId,
            Flight::RulesType::IFR,
            effectiveAirlineIcao,
            effectiveFlightNo,
            effectiveCallSign,
            flightPlan));

        auto aircraft = m_host->createAIAircraft(
            aircraftOption.modelIcao,
            effectiveAirlineIcao,
            effectiveTailNo,
            m_host->services().get<AircraftPerformanceProfileService>()->resolve(aircraftOption.modelIcao).category);
        flight->setAircraft(aircraft);

        auto pilot = m_host->createAIPilot(flight);
        flight->setPilot(pilot);
        flight->setPhase(Flight::Phase::Arrival);

        return flight;
    }

    void scheduleInboundFlight(shared_ptr<Flight> flight, const string& arrivalRunway, time_t arrivalTime)
    {
        auto copyOfWorld = m_world;
        auto copyOfAirport = m_airport;
        m_world->deferUntil(
            "addInboundFlight/" + flight->callSign(),
            arrivalTime,
            [flight, copyOfWorld, copyOfAirport, arrivalRunway](){
                const auto& landingRunwayEnd = copyOfWorld->getRunwayEnd(copyOfAirport->header().icao(), arrivalRunway);
                copyOfWorld->addFlight(flight);
                flight->aircraft()->setOnFinal(landingRunwayEnd);
            }
        );
    }

    void scheduleTurnaroundDeparture(shared_ptr<Flight> flight, time_t departureTime)
    {
        auto copyOfWorld = m_world;
        m_world->deferUntil(
            "addOutboundFlight/" + flight->callSign(),
            departureTime,
            [copyOfWorld, flight](){
                copyOfWorld->addFlightColdAndDark(flight);
            }
        );
    }

    runtime_schedule::RouteData fetchLiveRoute(const runtime_schedule::LiveAircraft& aircraft) const
    {
        return runtime_schedule::parseRouteResponse(downloadTextFromUrl(buildRouteLookupUrl(aircraft)));
    }

    vector<runtime_schedule::LiveAircraft> fetchLiveAircraft() const
    {
        return runtime_schedule::parsePointResponse(downloadTextFromUrl(buildPointLookupUrl()));
    }

    AircraftOption selectAircraftOption(
        const vector<AircraftOption>& aircraftOptions,
        shared_ptr<ParkingStand> gate,
        int index,
        const string& preferredModelIcao = "",
        const string& preferredAirlineIcao = "") const
    {
        vector<AircraftOption> exactMatches;
        vector<AircraftOption> modelMatches;
        vector<AircraftOption> airlineMatches;
        vector<AircraftOption> gateMatches;
        vector<AircraftOption> genericMatches;
        const string normalizedModelIcao = runtime_schedule::upper(preferredModelIcao);
        const string normalizedAirlineIcao = runtime_schedule::upper(preferredAirlineIcao);
        const auto& gateAirlines = gate->airlines();

        for (const auto& option : aircraftOptions)
        {
            if (
                !normalizedModelIcao.empty() &&
                !normalizedAirlineIcao.empty() &&
                option.modelIcao == normalizedModelIcao &&
                option.airlineIcao == normalizedAirlineIcao)
            {
                exactMatches.push_back(option);
            }

            if (!normalizedModelIcao.empty() && option.modelIcao == normalizedModelIcao)
            {
                modelMatches.push_back(option);
            }

            if (!normalizedAirlineIcao.empty() && option.airlineIcao == normalizedAirlineIcao)
            {
                airlineMatches.push_back(option);
            }

            if (
                !gateAirlines.empty() &&
                !option.airlineIcao.empty() &&
                find(gateAirlines.begin(), gateAirlines.end(), option.airlineIcao) != gateAirlines.end())
            {
                gateMatches.push_back(option);
            }

            if (option.airlineIcao.empty())
            {
                genericMatches.push_back(option);
            }
        }

        const vector<AircraftOption> *optionsToUse = &aircraftOptions;
        if (!exactMatches.empty())
        {
            optionsToUse = &exactMatches;
        }
        else if (!modelMatches.empty())
        {
            optionsToUse = &modelMatches;
        }
        else if (!airlineMatches.empty())
        {
            optionsToUse = &airlineMatches;
        }
        else if (!gateMatches.empty())
        {
            optionsToUse = &gateMatches;
        }
        else if (!genericMatches.empty())
        {
            optionsToUse = &genericMatches;
        }

        return optionsToUse->at((index - 1) % optionsToUse->size());
    }

    shared_ptr<ParkingStand> assignGateForCandidate(
        const runtime_schedule::LiveScheduleCandidate& candidate,
        vector<shared_ptr<ParkingStand>>& availableGates) const
    {
        if (availableGates.empty())
        {
            return nullptr;
        }

        const string airlineIcao = runtime_schedule::upper(candidate.route.airlineIcao);
        auto it = find_if(availableGates.begin(), availableGates.end(), [&airlineIcao](const shared_ptr<ParkingStand>& gate) {
            return (
                airlineIcao.empty() ||
                gate->airlines().empty() ||
                find(gate->airlines().begin(), gate->airlines().end(), airlineIcao) != gate->airlines().end());
        });

        if (it == availableGates.end())
        {
            it = availableGates.begin();
        }

        shared_ptr<ParkingStand> gate = *it;
        availableGates.erase(it);
        return gate;
    }

    bool tryLoadRealSchedules(float loadFactor)
    {
        vector<AircraftOption> aircraftOptions = findAvailableAircraftOptions();
        if (aircraftOptions.empty())
        {
            m_host->writeLog("SCHEDL|No available CSL aircraft options found; cannot load real schedules");
            return false;
        }

        vector<shared_ptr<ParkingStand>> usableGates = findUsableGatesForAIFlights();
        if (usableGates.empty())
        {
            m_host->writeLog("SCHEDL|No usable passenger gates found; cannot load real schedules");
            return false;
        }

        int requestedCount = max(1, (int)(usableGates.size() * loadFactor));
        vector<runtime_schedule::LiveAircraft> liveAircraft = fetchLiveAircraft();
        if (liveAircraft.empty())
        {
            m_host->writeLog("SCHEDL|adsb.lol returned no live aircraft for airport[%s]", m_airport->header().icao().c_str());
            return false;
        }

        const auto& airportLocation = m_airport->header().datum();
        stable_sort(liveAircraft.begin(), liveAircraft.end(), [&airportLocation](const runtime_schedule::LiveAircraft& left, const runtime_schedule::LiveAircraft& right) {
            bool leftGround = runtime_schedule::isGroundState(left) && runtime_schedule::isAtAirport(airportLocation, left);
            bool rightGround = runtime_schedule::isGroundState(right) && runtime_schedule::isAtAirport(airportLocation, right);
            if (leftGround != rightGround)
            {
                return leftGround > rightGround;
            }

            return GeoMath::getDistanceMeters(airportLocation, left.location) < GeoMath::getDistanceMeters(airportLocation, right.location);
        });

        vector<runtime_schedule::LiveAircraft> routeLookupAircraft;
        set<string> seenCallSigns;
        int routeLookupLimit = min((int)liveAircraft.size(), max(8, requestedCount * 3));
        for (const auto& aircraft : liveAircraft)
        {
            if (seenCallSigns.find(aircraft.callSign) != seenCallSigns.end())
            {
                continue;
            }

            seenCallSigns.insert(aircraft.callSign);
            routeLookupAircraft.push_back(aircraft);
            if ((int)routeLookupAircraft.size() >= routeLookupLimit)
            {
                break;
            }
        }

        vector<runtime_schedule::LiveScheduleCandidate> candidates;
        for (const auto& aircraft : routeLookupAircraft)
        {
            try
            {
                runtime_schedule::RouteData route = fetchLiveRoute(aircraft);
                runtime_schedule::LiveScheduleCandidate candidate;
                if (runtime_schedule::tryBuildCandidate(m_airport->header().icao(), airportLocation, aircraft, route, candidate))
                {
                    candidates.push_back(candidate);
                }
            }
            catch (const exception& e)
            {
                m_host->writeLog("SCHEDL|Skipping live route [%s]: %s", aircraft.callSign.c_str(), e.what());
            }
        }

        if (candidates.empty())
        {
            m_host->writeLog("SCHEDL|No usable adsb.lol schedule candidates found at airport[%s]", m_airport->header().icao().c_str());
            return false;
        }

        const auto candidateTypeRank = [](runtime_schedule::CandidateType type)->int {
            switch (type)
            {
            case runtime_schedule::CandidateType::Turnaround: return 0;
            case runtime_schedule::CandidateType::DepartureOnly: return 1;
            case runtime_schedule::CandidateType::ArrivalOnly: return 2;
            default: return 3;
            }
        };

        stable_sort(candidates.begin(), candidates.end(), [candidateTypeRank](const runtime_schedule::LiveScheduleCandidate& left, const runtime_schedule::LiveScheduleCandidate& right) {
            if (candidateTypeRank(left.type) != candidateTypeRank(right.type))
            {
                return candidateTypeRank(left.type) < candidateTypeRank(right.type);
            }

            if (left.type == runtime_schedule::CandidateType::DepartureOnly && right.type == runtime_schedule::CandidateType::DepartureOnly)
            {
                return left.aircraft.groundSpeedKt > right.aircraft.groundSpeedKt;
            }

            return left.distanceMeters < right.distanceMeters;
        });

        vector<AssignedLiveSchedule> acceptedSchedules;
        vector<shared_ptr<ParkingStand>> availableGates = usableGates;
        for (const auto& candidate : candidates)
        {
            if ((int)acceptedSchedules.size() >= requestedCount || availableGates.empty())
            {
                break;
            }

            shared_ptr<ParkingStand> gate = assignGateForCandidate(candidate, availableGates);
            if (gate)
            {
                AssignedLiveSchedule nextSchedule;
                nextSchedule.candidate = candidate;
                nextSchedule.gate = gate;
                acceptedSchedules.push_back(nextSchedule);
            }
        }

        if (acceptedSchedules.empty())
        {
            return false;
        }

        const float normalSecondsBetweenDepartures = 210;
        const float normalSecondsBetweenArrivals = 210;
        const float normalLoadFactor = 0.7f;
        int secondsBetweenDepartures = normalSecondsBetweenDepartures * normalLoadFactor / loadFactor;
        int secondsBetweenArrivals = normalSecondsBetweenArrivals * normalLoadFactor / loadFactor;
        time_t nextDepartureTime = m_world->currentTime() + 180;
        time_t nextArrivalTime = m_world->currentTime() + 60;
        const time_t turnaroundSeconds = 30 * 60;

        vector<AssignedLiveSchedule*> arrivalSchedules;
        vector<AssignedLiveSchedule*> departureSchedules;
        for (auto& schedule : acceptedSchedules)
        {
            if (schedule.candidate.type == runtime_schedule::CandidateType::DepartureOnly)
            {
                departureSchedules.push_back(&schedule);
            }
            else
            {
                arrivalSchedules.push_back(&schedule);
            }
        }

        stable_sort(arrivalSchedules.begin(), arrivalSchedules.end(), [](AssignedLiveSchedule* left, AssignedLiveSchedule* right) {
            return left->candidate.distanceMeters < right->candidate.distanceMeters;
        });
        stable_sort(departureSchedules.begin(), departureSchedules.end(), [](AssignedLiveSchedule* left, AssignedLiveSchedule* right) {
            return left->candidate.aircraft.groundSpeedKt > right->candidate.aircraft.groundSpeedKt;
        });

        for (auto* schedule : arrivalSchedules)
        {
            schedule->arrivalTime = nextArrivalTime;
            nextArrivalTime += secondsBetweenArrivals;

            if (schedule->candidate.type == runtime_schedule::CandidateType::Turnaround)
            {
                schedule->departureTime = max(schedule->arrivalTime + turnaroundSeconds, nextDepartureTime);
                nextDepartureTime = schedule->departureTime + secondsBetweenDepartures;
            }
        }

        for (auto* schedule : departureSchedules)
        {
            schedule->departureTime = nextDepartureTime;
            nextDepartureTime += secondsBetweenDepartures;
        }

        string departureRunway;
        string arrivalRunway1;
        string arrivalRunway2;
        selectActiveRunwayNames(departureRunway, arrivalRunway1, arrivalRunway2);
        int arrivalIndex = 0;
        int nextFlightId = 1000;

        for (size_t i = 0 ; i < acceptedSchedules.size() ; i++)
        {
            auto& schedule = acceptedSchedules[i];
            const auto& liveAircraft = schedule.candidate.aircraft;
            const auto& route = schedule.candidate.route;
            const AircraftOption aircraftOption = selectAircraftOption(
                aircraftOptions,
                schedule.gate,
                i + 1,
                liveAircraft.modelIcao,
                route.airlineIcao);
            const string tailNo = runtime_schedule::chooseTailNumber(liveAircraft, to_string(nextFlightId));

            try
            {
                if (schedule.candidate.type == runtime_schedule::CandidateType::DepartureOnly)
                {
                    auto flight = createOutboundFlight(
                        aircraftOption,
                        nextFlightId++,
                        schedule.candidate.destinationIcao,
                        schedule.departureTime,
                        schedule.gate,
                        departureRunway,
                        route.airlineIcao,
                        route.flightNo,
                        liveAircraft.callSign,
                        tailNo);
                    m_world->addFlightColdAndDark(flight);
                }
                else
                {
                    string arrivalRunway = ((arrivalIndex++) % 2) == 0 ? arrivalRunway1 : arrivalRunway2;
                    const string arrivalCallSign = schedule.candidate.type == runtime_schedule::CandidateType::Turnaround
                        ? runtime_schedule::buildArrivalCallsign(liveAircraft.callSign)
                        : liveAircraft.callSign;
                    const string arrivalFlightNo = schedule.candidate.type == runtime_schedule::CandidateType::Turnaround
                        ? (route.flightNo.empty() ? arrivalCallSign : route.flightNo + "A")
                        : route.flightNo;
                    auto inboundFlight = createInboundFlight(
                        aircraftOption,
                        nextFlightId++,
                        schedule.candidate.originIcao,
                        schedule.arrivalTime,
                        schedule.gate,
                        arrivalRunway,
                        route.airlineIcao,
                        arrivalFlightNo,
                        arrivalCallSign,
                        tailNo);
                    scheduleInboundFlight(inboundFlight, arrivalRunway, schedule.arrivalTime);

                    if (schedule.candidate.type == runtime_schedule::CandidateType::Turnaround)
                    {
                        auto outboundFlight = createOutboundFlight(
                            aircraftOption,
                            nextFlightId++,
                            schedule.candidate.onwardDestinationIcao,
                            schedule.departureTime,
                            schedule.gate,
                            departureRunway,
                            route.airlineIcao,
                            route.flightNo,
                            liveAircraft.callSign,
                            tailNo);
                        scheduleTurnaroundDeparture(outboundFlight, schedule.departureTime);
                    }
                }
            }
            catch (const exception& e)
            {
                m_host->writeLog("SCHEDL|Failed to add adsb.lol schedule for [%s]: %s", liveAircraft.callSign.c_str(), e.what());
            }
        }

        m_host->writeLog(
            "SCHEDL|Loaded [%d] adsb.lol schedule slots from [%d] live aircraft candidates",
            acceptedSchedules.size(),
            liveAircraft.size());
        return true;
    }

    void initDemoSchedules(float loadFactor, time_t firstDepartureTime, time_t firstArrivalTime)
    {
        string activeDepartureRunway;
        string activeArrivalRunway1;
        string activeArrivalRunway2;
        int arrivalIndex = 0;
        vector<AircraftOption> aircraftOptions = findAvailableAircraftOptions();
        vector<shared_ptr<Airport>> routeAirportOptions = findRouteAirportOptions();

        if (aircraftOptions.empty())
        {
            m_host->writeLog("SCHEDL|No available CSL aircraft options found; skipping demo schedule generation");
            return;
        }

        if (routeAirportOptions.empty())
        {
            m_host->writeLog("SCHEDL|No route airport options found; skipping demo schedule generation");
            return;
        }

        const float normalSecondsBetweenDepartures = 210;
        const float normalSecondsBetweenArrivals = 210;
        const float normalLoadFactor = 0.7f;
        int secondsBetweenDepartures = normalSecondsBetweenDepartures * normalLoadFactor / loadFactor;
        int secondsBetweenArrivals = normalSecondsBetweenArrivals * normalLoadFactor / loadFactor;

        m_host->writeLog(
            "SCHEDL|LOADFACTOR [%f] secondsBetweenArrivals=[%d] secondsBetweenDepartures=[%d]",
            loadFactor, secondsBetweenArrivals, secondsBetweenDepartures);

        vector<shared_ptr<ParkingStand>> gates;
        findGatesForFlights(gates, loadFactor);
        selectActiveRunwayNames(activeDepartureRunway, activeArrivalRunway1, activeArrivalRunway2);

        int index = 0;
        time_t nextDepartureTime = firstDepartureTime;
        time_t nextArrivalTime = firstArrivalTime;

        for (const auto& gate : gates)
        {
            index++;
            int flightId = 100 + index;
            const AircraftOption aircraftOption = selectAircraftOption(aircraftOptions, gate, index);
            const string& routeAirport = routeAirportOptions.at((index - 1) % routeAirportOptions.size())->header().icao();
            
            try
            {
                if ((index % 2) == 1)
                {
                    time_t departureTime = nextDepartureTime;
                    nextDepartureTime += secondsBetweenDepartures;
                    auto flight = createOutboundFlight(
                        aircraftOption,
                        flightId,
                        routeAirport,
                        departureTime,
                        gate,
                        activeDepartureRunway);
                    m_world->addFlightColdAndDark(flight);
                }
                else
                {
                    time_t arrivalTime = nextArrivalTime;
                    nextArrivalTime += secondsBetweenArrivals;
                    string arrivalRunway = ((arrivalIndex++) % 2) == 0 ? activeArrivalRunway1 : activeArrivalRunway2;
                    auto flight = createInboundFlight(
                        aircraftOption,
                        flightId,
                        routeAirport,
                        arrivalTime,
                        gate,
                        arrivalRunway);
                    scheduleInboundFlight(flight, arrivalRunway, arrivalTime);
                }
            }
            catch(const std::exception& e)
            {
                m_host->writeLog("SCHEDL|CRASHED while adding AI flight!!! %s", e.what());
            }
        }
    }

    vector<shared_ptr<ParkingStand>> findUsableGatesForAIFlights() const
    {
        GeoPoint userAircraftLocation((float)m_userAircraftLatitude, (float)m_userAircraftLongitude);

        const int nameCheckBufferSize = 32;
        char nameCheckBuffer[nameCheckBufferSize + 1] = { 0 };

        const auto isUserAircraftParkedAtGate = [&](const shared_ptr<ParkingStand>& gate)->bool {
            auto distanceToUserAircraft = GeoMath::getDistanceMeters(userAircraftLocation, gate->location().geo());
            return (distanceToUserAircraft < 50);
        };

        const auto isPassengerGateName = [&](const string& name)->bool {
            strncpy(nameCheckBuffer, name.c_str(), nameCheckBufferSize);
            for (int i = 0 ; i < name.length() && i < nameCheckBufferSize ; i++)
            {
                nameCheckBuffer[i] = toupper(nameCheckBuffer[i]);
            }
            return (
                !strstr(nameCheckBuffer, "HEL") &&
                !strstr(nameCheckBuffer, "MILI") &&
                !strstr(nameCheckBuffer, "RAMP") &&
                (!strstr(nameCheckBuffer, "GA") || strstr(nameCheckBuffer, "GATE")) &&
                !strstr(nameCheckBuffer, "G.A") &&
                !strstr(nameCheckBuffer, "GENERAL") &&
                !strstr(nameCheckBuffer, "GRASS") &&
                !strstr(nameCheckBuffer, "DIRT") &&
                !strstr(nameCheckBuffer, "FUEL") &&
                !strstr(nameCheckBuffer, "CARGO") &&
                !strstr(nameCheckBuffer, "HANG") &&
                !strstr(nameCheckBuffer, "TIE") &&
                !strstr(nameCheckBuffer, "MAINT") &&
                !strstr(nameCheckBuffer, "DOCK"));
        };

        const auto canUseGateForAIFlights = [&](const shared_ptr<ParkingStand>& gate)->bool {
            if (isUserAircraftParkedAtGate(gate))
            {
                m_host->writeLog(
                    "SCHEDL|Skipping gate[%s] looks like the user aircraft is parked here!",
                    gate->name().c_str());
                return false;
            }

            bool canUse = (
                gate->type() == ParkingStand::Type::Gate &&
                gate->hasOperationType(world::Aircraft::OperationType::Airline) &&
                !gate->hasOperationType(world::Aircraft::OperationType::Cargo) &&
                (gate->name().length() < 10 || isPassengerGateName(gate->name())));

            if (!canUse)
            {
                m_host->writeLog("SCHEDL|Won't use gate [%s]", gate->name().c_str());
            }

            return canUse;
        };

        const vector<shared_ptr<ParkingStand>>& allGates = m_airport->parkingStands();
        vector<shared_ptr<ParkingStand>> usableGates;
        copy_if(allGates.begin(), allGates.end(), back_inserter(usableGates), canUseGateForAIFlights);
        m_host->writeLog(
            "SCHEDL|Found [%d/%d] gates for AI flights, skipped [%d]",
            usableGates.size(), allGates.size(), allGates.size() - usableGates.size());

        return usableGates;
    }

    void findGatesForFlights(vector<shared_ptr<ParkingStand>>& found, float loadFactor)
    {
        vector<shared_ptr<ParkingStand>> usableGates = findUsableGatesForAIFlights();

        vector<unsigned int> indices(usableGates.size());
        iota(indices.begin(), indices.end(), 0);
        shuffle(indices.begin(), indices.end(), std::default_random_engine());
        int requestedCount = (int)(usableGates.size() * loadFactor);

        for (int i = 0 ; i < indices.size() && i < requestedCount ; i++)
        {
            const shared_ptr<ParkingStand>& gate = usableGates.at(indices.at(i));
            found.push_back(gate);
        }

        m_host->writeLog(
            "SCHEDL|Picked [%d/%d] gates for AI flights at load factor[%f]",
            found.size(),
            requestedCount,
            loadFactor);
    }

    shared_ptr<Airport> findClosestAirport(const GeoPoint& location, const vector<shared_ptr<Airport>>& airports) const
    {
        shared_ptr<Airport> closestAirport;
        float closestDistanceMeters = numeric_limits<float>::max();

        for (const auto& airport : airports)
        {
            float nextDistanceMeters = GeoMath::getDistanceMeters(location, airport->header().datum());
            if (nextDistanceMeters < closestDistanceMeters)
            {
                closestAirport = airport;
                closestDistanceMeters = nextDistanceMeters;
            }
        }

        return closestAirport;
    }

    vector<shared_ptr<Airport>> findRouteAirportOptions() const
    {
        vector<shared_ptr<Airport>> routeAirports;

        for (const auto& airport : m_world->airports())
        {
            if (airport->header().icao() == m_airport->header().icao())
            {
                continue;
            }

            try
            {
                airport->findLongestRunway();
                routeAirports.push_back(airport);
            }
            catch (const exception& e)
            {
                m_host->writeLog("SCHEDL|Skipping route airport [%s]: %s", airport->header().icao().c_str(), e.what());
            }
        }

        return routeAirports;
    }

    vector<AircraftOption> findAvailableAircraftOptions() const
    {
        vector<AircraftOption> options;
        set<string> seenKeys;
        vector<string> packageNames = m_host->findFilesInResourceDirectory({ "Resources", "CSL" });

        for (const auto& packageName : packageNames)
        {
            string filePath = m_host->getResourceFilePath({ "Resources", "CSL", packageName, "xsb_aircraft.txt" });

            try
            {
                shared_ptr<istream> file = m_host->openFileForRead(filePath);
                parseAircraftOptions(*file, options, seenKeys);
            }
            catch (const exception& e)
            {
                m_host->writeLog("SCHEDL|Skipping CSL package metadata [%s]: %s", filePath.c_str(), e.what());
            }
        }

        m_host->writeLog("SCHEDL|Found [%d] available aircraft/livery options", options.size());
        return options;
    }

    void parseAircraftOptions(istream& input, vector<AircraftOption>& options, set<string>& seenKeys) const
    {
        string line;
        set<string> modelsWithAirlineEntries;
        set<string> genericModels;

        const auto addOption = [&options, &seenKeys](const string& modelIcao, const string& airlineIcao, const string& callSignPrefix) {
            if (modelIcao.empty())
            {
                return;
            }

            string key = modelIcao + "|" + airlineIcao;
            if (seenKeys.find(key) != seenKeys.end())
            {
                return;
            }

            seenKeys.insert(key);
            options.push_back({ modelIcao, airlineIcao, callSignPrefix });
        };

        while (getline(input, line))
        {
            if (line.empty() || line[0] == ';')
            {
                continue;
            }

            string directive;
            string modelIcao;
            string airlineIcao;
            stringstream row(line);
            row >> directive;

            if (directive == "AIRLINE")
            {
                row >> modelIcao >> airlineIcao;
                AirlineReferenceTable::Entry airline;
                string callSignPrefix;
                if (AirlineReferenceTable::tryFindByIcao(airlineIcao, airline))
                {
                    callSignPrefix = airline.callsign;
                }

                addOption(modelIcao, airlineIcao, callSignPrefix);
                modelsWithAirlineEntries.insert(modelIcao);
            }
            else if (directive == "ICAO")
            {
                row >> modelIcao;
                genericModels.insert(modelIcao);
            }
        }

        for (const auto& modelIcao : genericModels)
        {
            if (modelsWithAirlineEntries.find(modelIcao) == modelsWithAirlineEntries.end())
            {
                addOption(modelIcao, "", "");
            }
        }
    }

    void logActiveRunwaysBounds()
    {
        const auto logBounds = [this](shared_ptr<Runway> runway) {
            const auto& bounds = runway->bounds();
            m_host->writeLog(
                "LSCHED|RWY-BOUNDS[%s]: A[%f,%f] B[%f,%f] C[%f,%f] D[%f,%f] minLat[%f] maxLat[%f] minLon[%f] maxLon[%f]",
                runway->name().c_str(),
                bounds.A.latitude, bounds.A.longitude,
                bounds.B.latitude, bounds.B.longitude,
                bounds.C.latitude, bounds.C.longitude,
                bounds.D.latitude, bounds.D.longitude,
                bounds.minLatitude, bounds.maxLatitude,
                bounds.minLongitude, bounds.maxLongitude);
        };

        for (const auto& name : m_airport->activeArrivalRunways())
        {
            logBounds(m_airport->getRunwayOrThrow(name));
        }

        for (const auto& name : m_airport->activeDepartureRunways())
        {
            logBounds(m_airport->getRunwayOrThrow(name));
        }
    }
};
