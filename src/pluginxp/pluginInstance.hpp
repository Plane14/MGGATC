// 
// This file is part of AT&C project which simulates virtual world of air traffic and ATC.
// Code licensing terms are available at https://github.com/felix-b/atc/blob/master/LICENSE
// 
#pragma once

#include <iostream>
#include <functional>
#include <utility>
#include <memory>
#include <thread>
#include <future>
#include <atomic>
#include <mutex>

// SDK
#include "XPLMPlugin.h"
#if !XPLM300
#error This plugin requires version 300 of the SDK
#endif

// PPL 
#include "owneddata.h"

// tnc
#include "utils.h"
#include "libworld.h"
#include "libai.hpp"
#if IBM
#include "libserver.hpp"
#endif
#include "pluginHostServices.hpp"
#include "pluginMenu.hpp"
#include "nativeTextToSpeechService.hpp"
#include "airlineReferenceTable.hpp"
#include "simplePhraseologyService.hpp"
#include "xpmp2AircraftObjectService.hpp"
#include "xplaneAirportFlowProvider.hpp"
#include "xplmSpeakStringTtsService.hpp"
#include "pluginWorldLoader.hpp"
#include "demoScheduleLoader.hpp"
#include "configuration.hpp"
#include "userPilotAssistantWorkflow.hpp"
#include "userAircraft.hpp"
#include "userPilot.hpp"
#include "transcriptInterface.hpp"
#include "aircraftPerformanceProfileService.hpp"

using namespace std;
using namespace PPL;

//const char* poc_resource_directory = "/Users/felixb/Desktop/xp/Resources/plugins/tnc/Resources";
//static string resourceDirectory = "/Users/felixb/Desktop/xp/Resources/plugins/tnc/Resources";//PluginPath::prependPluginResourcesPath("");

class PluginInstance
{
private:

    enum class PluginStateId
    {
        Stopped = 0,
        WorldAssembling = 1,
        WorldAssembled = 2,
        SchedulesStarting = 3,
        SchedulesStarted = 4,
        Failed = 5
    };

    class PluginState
    {
    private:
        PluginStateId m_id;
        string m_name;
    protected:
        PluginState(PluginStateId _id, const string& _name) :
            m_id(_id),
            m_name(_name)
        {
        }
    public:
        PluginStateId id() const { return m_id; }
        const string& name() const { return m_name; }
    public:
        virtual void enter()
        {
        }
        virtual void exit()
        {
        }
        virtual void ping()
        {
        }
    };

    class StoppedState : public PluginState
    {
    private:
        shared_ptr<HostServices> m_host;
    public:
        StoppedState(shared_ptr<HostServices> _host) :
            PluginState(PluginStateId::Stopped, "STOPPED"),
            m_host(std::move(_host))
        {
        }
    public:
        void enter() override
        {
            stopServer();
        }
    private:
        void stopServer()
        {
#if IBM
            auto server = m_host->services().get<server::ServerControllerInterface>();

            if (server->running())
            {
                m_host->writeLog("PLUGIN|stopping the server");
                server->beginStop();
                if (server->waitUntilStopped(chrono::seconds(5)))
                {
                    m_host->writeLog("PLUGIN|server successfully stopped");
                }
                else
                {
                    m_host->writeLog("PLUGIN|WARNING: timeout waiting for server to stop");
                }
            }
#endif
        }
    };

    class FailedState : public PluginState
    {
    public:
        FailedState() :
            PluginState(PluginStateId::Failed, "FAILED")
        {
        }
    };

    class WorldAssemblingState : public PluginState
    {
    private:
        shared_ptr<PluginHostServices> m_host;
        future<shared_ptr<World>> m_worldFuture;
        atomic<bool> m_done;
        PluginMenu::Item m_assemblingItem;
        function<void(shared_ptr<World> world)> m_onAssembled;
        function<void()> m_onFailed;
    public:
        WorldAssemblingState(
            shared_ptr<PluginHostServices> _host,
            PluginMenu& _menu,
            function<void(shared_ptr<World> world)> _onAssembled,
            function<void()> _onFailed
        ) : PluginState(PluginStateId::WorldAssembling, "WORLD-ASSEMBLING"),
            m_host(std::move(_host)),
            m_assemblingItem(_menu, "World is being assembled, please wait...", [](){}),
            m_onAssembled(std::move(_onAssembled)),
            m_onFailed(std::move(_onFailed)),
            m_done(false)
        {
        }

    public:

        void enter() override
        {
            auto host = m_host;

            m_worldFuture = std::async(std::launch::async, [host]() -> shared_ptr<World> {
                try
                {
                    PluginWorldLoader loader(host);
                    loader.loadWorld();
                    auto world = loader.getWorld();
                    if (world)
                    {
                        host->useWorld(world);
                        tryStartServer(host);
                    }
                    return world;
                }
                catch (const exception& e)
                {
                    host->writeLog("PLUGIN|WorldAssemblingState background task CRASHED!!! %s", e.what());
                    return shared_ptr<World>();
                }
            });
        }

        void exit() override
        {
            if (!m_done && m_worldFuture.valid() &&
                m_worldFuture.wait_for(chrono::milliseconds(0)) != future_status::ready)
            {
                m_host->writeLog("PLUGIN|WARNING: leaving WORLD-ASSEMBLING state while the assembly is still in progress!");
                parkFuture(m_worldFuture);
            }
        }

        void ping() override
        {
            if (m_worldFuture.wait_for(chrono::milliseconds(0)) != future_status::ready)
            {
                m_host->writeLog("PLUGIN|ping WORLD-ASSEMBLING: in progress");
            }
            else if (m_worldFuture.valid())
            {
                m_host->writeLog("PLUGIN|ping WORLD-ASSEMBLING: done");
                auto world = m_worldFuture.get();
                m_done = true;

                if (world)
                {
                    m_onAssembled(world);
                }
                else
                {
                    m_host->writeLog("PLUGIN|ERROR: world was not assembled - plugin will not function (see previous errors).");
                    m_onFailed();
                }
            }
        }

    private:

        static void tryStartServer(shared_ptr<HostServices> host)
        {
#if IBM
            try
            {
                auto server = host->services().get<server::ServerControllerInterface>();

                if (!server->running())
                {
                    host->writeLog("PLUGIN|starting the server");
                    server->start(9002);
                }
            }
            catch (const exception& e)
            {
                host->writeLog("PLUGIN|startServer CRASHED!!! %s", e.what());
            }
#endif
        }
    };

    class WorldAssembledState : public PluginState
    {
    private:
        shared_ptr<HostServices> m_host;
        PluginMenu::Item m_startSchedules100Item;
        PluginMenu::Item m_startSchedules70Item;
        PluginMenu::Item m_startSchedules50Item;
    public:
        WorldAssembledState(
            shared_ptr<HostServices> _host,
            PluginMenu& _menu,
            function<void(float loadFactor)> _onStartSchedules
        ) : PluginState(PluginStateId::WorldAssembled, "WORLD-ASSEMBLED"),
            m_host(std::move(_host)),
            m_startSchedules100Item(_menu, "Start Schedules - 100% load", [=]{ _onStartSchedules(1.0); }),
            m_startSchedules70Item(_menu, "Start Schedules - 70% load", [=]{ _onStartSchedules(0.7); }),
            m_startSchedules50Item(_menu, "Start Schedules - 50% load", [=]{ _onStartSchedules(0.5); })
        {
        }
    };

    class SchedulesStartingState : public PluginState
    {
    private:
        struct SchedulesLoadResult
        {
            shared_ptr<Airport> airport;
            string error;
        };

        shared_ptr<HostServices> m_host;
        shared_ptr<World> m_world;
        float m_loadFactor;
        PluginMenu::Item m_worldIsStarting;
        PluginMenu::Item m_cancelItem;                             // "Cancel loading schedules"
        function<void(shared_ptr<Airport> userAirport)> m_onStarted;
        function<void(const string& error)> m_onFailed;
        shared_ptr<DemoScheduleLoader> m_loader;
        shared_ptr<SchedulesLoadResult> m_loadResult;
        future<shared_ptr<SchedulesLoadResult>> m_loadFuture;
        bool m_done = false;
        bool m_cancelRequestedByUser = false;
    public:
        // constructor: add a 6th parameter _onFailed (function<void(const string&)>) AFTER _onStarted
        SchedulesStartingState(
            shared_ptr<HostServices> _host,
            shared_ptr<World> _world,
            PluginMenu& _menu,
            float _loadFactor,
            function<void(shared_ptr<Airport> userAirport)> _onStarted,
            function<void(const string& error)> _onFailed
        ) : PluginState(PluginStateId::SchedulesStarting, "SCHEDULES-STARTING"),
            m_host(std::move(_host)),
            m_world(_world),
            m_worldIsStarting(_menu, "Starting schedules, please wait...", [](){}),
            m_cancelItem(_menu, "Cancel loading schedules", [this](){ cancelLoading(); }),
            m_loadFactor(_loadFactor),
            m_onStarted(std::move(_onStarted)),
            m_onFailed(std::move(_onFailed))
        {
        }

        void enter() override
        {
            m_loadResult = make_shared<SchedulesLoadResult>();
            m_loader = make_shared<DemoScheduleLoader>(m_host, m_world);
            auto loader = m_loader;
            auto loadFactor = m_loadFactor;
            auto result = m_loadResult;

            try
            {
                loader->prepare();   // main-thread-only phase (XPLM nav + CSL scan)
            }
            catch (const exception& e)
            {
                result->error = e.what();
                // ping() will report this failure
                return;
            }

            try
            {
                m_loadFuture = std::async(std::launch::async, [loader, loadFactor, result]() {
                    try
                    {
                        loader->loadSchedules(loadFactor);
                    }
                    catch (const exception& e)
                    {
                        result->error = e.what();
                    }
                    result->airport = loader->airport();
                    return result;
                });
            }
            catch (const exception& e)
            {
                result->error = e.what();
                // ping() will report this failure
            }
        }

        void ping() override
        {
            if (m_done)
            {
                return;
            }

            if (m_cancelRequestedByUser)
            {
                // The cancel request comes from a menu handler, so the transition into the
                // next state is deferred to this callback to keep the menu item alive until
                // X-Plane is done dispatching it. The loading checks the cancel flag between
                // its steps, so we simply wait for it to stop here.
                if (!m_loadFuture.valid() ||
                    m_loadFuture.wait_for(chrono::milliseconds(0)) == future_status::ready)
                {
                    if (m_loadFuture.valid())
                    {
                        try { m_loadFuture.get(); } catch (const exception&) {}
                    }

                    finish(nullptr, string("cancelled by the user"));
                }

                return;
            }

            if (!m_loadFuture.valid())
            {
                // prepare() failed on the main thread, nothing is loading
                finish(nullptr, m_loadResult ? m_loadResult->error : string("schedule preparation failed"));
                return;
            }

            if (m_loadFuture.wait_for(chrono::milliseconds(0)) != future_status::ready)
            {
                return;   // still loading, keep the sim responsive
            }

            shared_ptr<SchedulesLoadResult> result;
            try
            {
                result = m_loadFuture.get();
            }
            catch (const exception& e)
            {
                finish(nullptr, e.what());
                return;
            }

            finish(result->airport, result->error);
        }

        void exit() override
        {
            if (m_loader && !m_done)
            {
                m_loader->requestCancel();

                // Give the background task a bounded window to stop cleanly so that it cannot
                // keep touching the world while the user retries the schedule load. Loading
                // checks the cancel flag between flights/route lookups, so the wait normally
                // ends within the currently running fetch step.
                for (int attempt = 0 ; attempt < 50 ; attempt++)
                {
                    if (!m_loadFuture.valid())
                    {
                        return;
                    }

                    if (m_loadFuture.wait_for(chrono::milliseconds(0)) == future_status::ready)
                    {
                        try { m_loadFuture.get(); } catch (const exception&) {}
                        return;
                    }

                    this_thread::sleep_for(chrono::milliseconds(100));
                }

                m_host->writeLog("PLUGIN|WARNING: leaving SCHEDULES-STARTING state while schedules are still loading");
                parkFuture(m_loadFuture);
            }
        }
    private:
        void cancelLoading()
        {
            if (m_loader && !m_done && !m_cancelRequestedByUser)
            {
                m_cancelRequestedByUser = true;
                m_loader->requestCancel();
                m_host->writeLog("PLUGIN|cancel requested: schedules are still being loaded");
            }
        }

        void finish(shared_ptr<Airport> airport, const string& error)
        {
            m_done = true;
            m_loader.reset();

            if (!error.empty())
            {
                m_host->writeLog("PLUGIN|schedule loading finished with errors: %s", error.c_str());
            }

            if (airport)
            {
                try
                {
                    WorldBuilder::tidyAirportElevations(m_host, airport);
                    logUserAirportElevations(airport);
                    m_host->writeLog(
                        "PLUGIN|The world now has [%d] airports, [%d] control facilities, [%d] AI flights",
                        m_world->airports().size(),
                        m_world->controlFacilities().size(),
                        m_world->flights().size());
                }
                catch (const exception& e)
                {
                    m_host->writeLog("PLUGIN|post-load tidy-up CRASHED!!! %s", e.what());
                }

                m_onStarted(airport);   // transitions the state machine; do not touch members after this call
                return;
            }

            m_host->writeLog("PLUGIN|ERROR: schedules were not loaded - see previous errors");
            m_onFailed(error);
        }

        void logUserAirportElevations(shared_ptr<Airport> userAirport) // was: no parameter, used m_userAirport
        {
            const auto logRunwayEndElevation = [this, userAirport](const Runway::End& end) {
                const auto& centerlinePoint = end.centerlinePoint().geo();
                m_host->writeLog(
                    "PLUGIN|User airport elevations [%s/%s] at (%f,%f) elevation [%f]",
                    userAirport->header().icao().c_str(),
                    end.name().c_str(),
                    centerlinePoint.latitude,
                    centerlinePoint.longitude,
                    end.elevationFeet());
            };

            for (const auto& runway : userAirport->runways())
            {
                logRunwayEndElevation(runway->end1());
                logRunwayEndElevation(runway->end2());
            }
        }
    };

    class SchedulesStartedState : public PluginState
    {
    private:
        shared_ptr<HostServices> m_host;
        shared_ptr<World> m_world;
        shared_ptr<AircraftObjectService> m_aircraftObjectService;
        chrono::time_point<chrono::high_resolution_clock, chrono::microseconds> m_lastTickTime;
        //uint64_t m_timeFactor;
        shared_ptr<Airport> m_userAirport;
        shared_ptr<UserPilotAssistantWorkflow> m_userPilotWorkflow;
        shared_ptr<TranscriptInterface> m_transcript;
        shared_ptr<PluginMenu> m_userActionsMenu;
        PluginMenu::Item m_tuneClearanceItem;
        PluginMenu::Item m_tuneGroundItem;
        PluginMenu::Item m_tuneTowerItem;
        PluginMenu::Item m_toggleLabelsItem;
        PluginMenu::Item m_toggleDebugLabelsItem;
        PluginMenu::Item m_time1XItem;
        PluginMenu::Item m_time10XItem;
        PluginMenu::Item m_time20XItem;
        PluginMenu::Item m_stopSchedulesItem;
        PluginMenu::Item m_restartSchedules100Item;
        PluginMenu::Item m_restartSchedules70Item;
        PluginMenu::Item m_restartSchedules50Item;
        DataRef<int> m_com1FrequencyKhz;
        DataRef<int> m_simSpeed;
    public:
        SchedulesStartedState(
            shared_ptr<HostServices> _host,
            shared_ptr<World> _world,
            PluginMenu& _menu,
            shared_ptr<Airport> _userAirport,
            function<void()> _onStopSchedules,
            function<void(float loadFactor)> _onRestartSchedules
        ) : PluginState(PluginStateId::SchedulesStarted, "SCHEDULES-STARTED"),
            m_host(_host),
            m_world(_world),
            m_userAirport(_userAirport),
            m_transcript(_host->services().get<TranscriptInterface>()),
            m_userActionsMenu(make_shared<PluginMenu>("ATC Actions", _menu)),
            m_tuneClearanceItem(_menu, "COM1 " + _userAirport->header().icao() + " CLR/DEL", [this] { tuneToClearance(); }),
            m_tuneGroundItem(_menu, "COM1 " + _userAirport->header().icao() + " GND", [this] { tuneToGround(); }),
            m_tuneTowerItem(_menu, "COM1 " + _userAirport->header().icao() + " TWR", [this] { tuneToTower(); }),
            m_toggleLabelsItem(_menu, "Toggle AI aircraft labels", [this](){ toggleAIAircraftLabels(); }),
            m_toggleDebugLabelsItem(_menu, "Toggle debug info in labels", [this](){ toggleAIAircraftDebugLabels(); }),
            m_time1XItem(_menu, "Time X 1", [=](){ m_simSpeed = 1; }),
            m_time10XItem(_menu, "Time X 10", [=](){ m_simSpeed = 10; }),
            m_time20XItem(_menu, "Time X 20", [=](){ m_simSpeed = 20; }),
            m_stopSchedulesItem(_menu, "Stop schedules", std::move(_onStopSchedules)),
            m_restartSchedules100Item(_menu, "Restart schedules with 100% load", [=]{_onRestartSchedules(1.0f);}),
            m_restartSchedules70Item(_menu, "Restart schedules with 70% load", [=]{_onRestartSchedules(0.7f);}),
            m_restartSchedules50Item(_menu, "Restart schedules with 50% load", [=]{_onRestartSchedules(0.5f);}),
            m_com1FrequencyKhz("sim/cockpit2/radios/actuators/com1_frequency_hz_833", PPL::ReadWrite),
            m_simSpeed("sim/time/sim_speed", PPL::ReadWrite)
        {
            m_aircraftObjectService = m_host->services().get<AircraftObjectService>();
            m_lastTickTime = getNow();
            //m_timeFactor = 1;
        }

        void enter() override
        {
            m_transcript->setUserActionsMenu(m_userActionsMenu);
            initUserFlight();
        }

        void ping() override
        {
            auto now = getNow();
            auto microsecondsSinceLastTick = (now - m_lastTickTime) * m_simSpeed; // m_timeFactor;
            auto newWorldTimestamp = m_world->timestamp() + microsecondsSinceLastTick;
            m_lastTickTime = now;

            if (microsecondsSinceLastTick.count() == 0)
            {
                return;
            }

            m_world->progressTo(newWorldTimestamp);
            auto changeSet = m_world->hasChanges()
                ? m_world->takeChanges()
                : nullptr;

            if (changeSet)
            {
                processWorldChanges(changeSet);
            }
        }

        void exit() override
        {
            m_transcript->setUserActionsMenu(nullptr);
            m_world->clearAllFlights();
            m_userPilotWorkflow.reset();
        }

    private:

        void processWorldChanges(shared_ptr<World::ChangeSet> changeSet)
        {
            m_aircraftObjectService->processEvents(changeSet);
        }

        void tuneToClearance()
        {
            try
            {
                auto clearance = m_userAirport->clearanceDeliveryAt(m_userAirport->header().datum());
                m_com1FrequencyKhz = clearance->frequency()->khz();
            }
            catch (const exception& e)
            {
                m_host->writeLog("PLUGIN|%s CRASHED!!! %s", "tuneToClearance", e.what());
            }
        }

        void tuneToGround()
        {
            try
            {
                auto ground = m_userAirport->groundAt(m_userAirport->header().datum());
                m_com1FrequencyKhz = ground->frequency()->khz();
            }
            catch (const exception& e)
            {
                m_host->writeLog("PLUGIN|%s CRASHED!!! %s", "tuneToGround", e.what());
            }
        }

        void tuneToTower()
        {
            try
            {
                auto tower = m_userAirport->localAt(m_userAirport->header().datum());
                m_com1FrequencyKhz = tower->frequency()->khz();
            }
            catch (const exception& e)
            {
                m_host->writeLog("PLUGIN|%s CRASHED!!! %s", "tuneToTower", e.what());
            }
        }

        void toggleAIAircraftLabels()
        {
            auto config = m_host->services().get<PluginConfiguration>();
            config->showAIAircraftLabels = !config->showAIAircraftLabels;
            m_host->getWorld()->notifyConfigurationChanged();
        }

        void toggleAIAircraftDebugLabels()
        {
            auto config = m_host->services().get<PluginConfiguration>();
            config->showAIAircraftDebugLabels = !config->showAIAircraftDebugLabels;
            m_host->getWorld()->notifyConfigurationChanged();
        }

        void initUserFlight()
        {
            try
            {
                string departureRunwayName;
                const auto& activeDepartureRunways = m_userAirport->activeDepartureRunways();
                if (!activeDepartureRunways.empty())
                {
                    departureRunwayName = activeDepartureRunways.at(0);
                }
                else
                {
                    m_host->writeLog(
                        "UPILOT|INIT WARNING: no active departure runways at [%s], falling back to the longest runway",
                        m_userAirport->header().icao().c_str());
                    departureRunwayName = m_userAirport->findLongestRunway()->end1().name();
                }

                auto world = m_host->getWorld();
                auto departureTime = world->currentTime() + 30 * 60; //P-30
                auto arrivalTime = departureTime + 3 * 60 * 60; //3h
                auto userAircraft = UserAircraft::create(m_host);
                string flightNo = to_string(1);
                string callSign = flightNo;
                AirlineReferenceTable::Entry airline;
                if (!userAircraft->airlineIcao().empty() && AirlineReferenceTable::tryFindByIcao(userAircraft->airlineIcao(), airline))
                {
                    callSign = airline.callsign + " " + flightNo;
                }
                const auto defaultArrivalAirport = [this, world]()->string {
                    for (const auto& airport : world->airports())
                    {
                        if (airport->header().icao() != m_userAirport->header().icao())
                        {
                            return airport->header().icao();
                        }
                    }

                    return m_userAirport->header().icao();
                }();

                auto flightPlan = shared_ptr<FlightPlan>(new FlightPlan(
                    departureTime,
                    arrivalTime,
                    m_userAirport->header().icao(),
                    defaultArrivalAirport));

                auto userFlight = shared_ptr<Flight>(new Flight(
                    m_host,
                    1,
                    Flight::RulesType::IFR,
                    userAircraft->airlineIcao(),
                    flightNo,
                    callSign,
                    flightPlan));

                m_host->writeLog("initUserFlight:2");

                auto departureGate = m_userAirport->findClosestParkingStand(userAircraft->location());
                flightPlan->setDepartureGate(departureGate ? departureGate->name() : "N/A");
                flightPlan->setDepartureRunway(departureRunwayName);

                m_host->writeLog(
                    "UPILOT|INIT departure gate=[%s] runway=[%s]",
                    flightPlan->departureGate().c_str(), departureRunwayName.c_str());

                userFlight->setAircraft(userAircraft);

                m_host->writeLog("initUserFlight:4");

                auto userPilot = UserPilot::create(m_host, "Bob", Actor::Gender::Male, userFlight);

                m_host->writeLog("initUserFlight:5");

                userFlight->setPilot(userPilot);

                m_host->writeLog("initUserFlight:6");

                m_userPilotWorkflow = shared_ptr<UserPilotAssistantWorkflow>(new UserPilotAssistantWorkflow(
                    m_host,
                    userFlight,
                    m_userAirport));

                m_world->addFlight(userFlight);

                m_host->writeLog("initUserFlight:7");
            }
            catch (const exception& e)
            {
                m_host->writeLog("UPILOT|INIT CRASHED!!! %s", e.what());
            }
        }
    };

private:
    PluginMenu m_menu;
    shared_ptr<PluginState> m_currentState;
    shared_ptr<PluginHostServices> m_hostServices;
    shared_ptr<World> m_world;
    float m_schedulesLoadFactor;
    DataRef<double> m_userAircraftLatitude;
    DataRef<double> m_userAircraftLongitude;
public:
    PluginInstance() :
        m_menu("Air Traffic & Control"),
        m_schedulesLoadFactor(0.7f),
        m_userAircraftLatitude("sim/flightmodel/position/latitude", PPL::ReadOnly),
        m_userAircraftLongitude("sim/flightmodel/position/longitude", PPL::ReadOnly)
    {
        PrintDebugString("PLUGIN|initializing PluginInstance");

        m_hostServices = createHostServices();
        transitionToState([this]() { return createWorldAssemblingState(); });
        // Fixed timestep: 20 Hz (50 ms) to avoid consuming CPU every frame.
        XPLMRegisterFlightLoopCallback(&pluginFlightLoopCallback, 0.05, this);
    }

    ~PluginInstance()
    {
        PrintDebugString("PLUGIN|destroying PluginInstance");

        XPLMUnregisterFlightLoopCallback(&pluginFlightLoopCallback, this);
        if (m_currentState->id() != PluginStateId::Stopped)
        {
            transitionToState([this]() { return createStoppedState(); });
        }
    }

public:

    void notifyAirportLoaded()
    {
        try
        {
            switch (m_currentState->id())
            {
            case PluginStateId::WorldAssembling:
                PrintDebugString("PLUGIN|notifyAirportLoaded pinging state [%s]", m_currentState->name().c_str());
                m_currentState->ping();
                break;
            case PluginStateId::SchedulesStarted:
                transitionToState([this] {
                    return createSchedulesStartingState();
                });
                break;
            case PluginStateId::Failed:
                transitionToState([this]() {
                    return m_world
                        ? shared_ptr<PluginState>(createWorldAssembledState())
                        : shared_ptr<PluginState>(createWorldAssemblingState());
                });
                break;
            }
        }
        catch (const exception& e)
        {
            m_hostServices->writeLog("PLUGIN|notifyAirportLoaded CRASHED!!! %s", e.what());
        }
    }

private:

    // Keeps futures of abandoned async loads alive: destroying a std::async future blocks
    // until the task completes, which would stall the main thread.
    template <typename T>
    static void parkFuture(future<T>& futureToPark)
    {
        static mutex parkedFuturesMutex;
        static vector<future<T>> parkedFutures;
        lock_guard<mutex> guard(parkedFuturesMutex);
        parkedFutures.push_back(std::move(futureToPark));
    }

    shared_ptr<PluginHostServices> createHostServices()
    {
#if APL
        XPLMDebugString("------ BEGIN ALTEST ------\n");
        NativeTextToSpeechService::init_sound();
        XPLMDebugString("------ END ALTEST ------\n");
#endif

        auto hostServices = shared_ptr<PluginHostServices>(new PluginHostServices());
        auto configuration = make_shared<PluginConfiguration>();

#if LIN
        auto pluginTts = shared_ptr<XPLMSpeakStringTtsService>(new XPLMSpeakStringTtsService(hostServices));
#else
        auto pluginTts = shared_ptr<NativeTextToSpeechService>(new NativeTextToSpeechService(hostServices));
#endif

        auto phraseologyService = shared_ptr<PhraseologyService>(new SimplePhraseologyService(hostServices));
        auto aircraftObjectService = shared_ptr<Xpmp2AircraftObjectService>(new Xpmp2AircraftObjectService(hostServices));
        auto intentFactory = shared_ptr<IntentFactory>(new IntentFactory(hostServices));
        auto transcriptInterface = shared_ptr<TranscriptInterface>(new MenuBasedTranscriptInterface(hostServices));
        auto aircraftPerformanceProfiles = shared_ptr<AircraftPerformanceProfileService>(new AircraftPerformanceProfileService(hostServices));
        auto airportFlowProvider = make_shared<XPlaneAirportFlowProvider>();

        hostServices->services().use<PluginConfiguration>(configuration);
        hostServices->services().use<AircraftObjectService>(aircraftObjectService);
        hostServices->services().use<TextToSpeechService>(pluginTts);
        hostServices->services().use<IntentFactory>(intentFactory);
        hostServices->services().use<PhraseologyService>(phraseologyService);
        hostServices->services().use<TranscriptInterface>(transcriptInterface);
        hostServices->services().use<AircraftPerformanceProfileService>(aircraftPerformanceProfiles);
        hostServices->services().use<AirportFlowProvider>(airportFlowProvider);

#if IBM
        auto serverController = server::ServerControllerInterface::create(hostServices);
        hostServices->services().use<server::ServerControllerInterface>(serverController);
#endif

        ai::contributeComponents(hostServices);

        hostServices->writeLog("PLUGIN|host services initialized");
        return hostServices;
    }

    void transitionToState(function<shared_ptr<PluginState>()> factory)
    {
        if (m_currentState)
        {
            try
            {
                m_hostServices->writeLog("PLUGIN|exiting state[%s]", m_currentState->name().c_str());
                m_currentState->exit();
                m_currentState.reset();
            }
            catch (const exception &e)
            {
                PrintDebugString("PLUGIN|CRASHED while transitioning state (exit)!!! %s", e.what());
            }
        }

        try
        {
            auto nextState = factory();

            if (nextState)
            {
                nextState->enter();
                m_currentState = nextState;
                m_hostServices->writeLog("PLUGIN|transitioned to state[%s]", m_currentState->name().c_str());
            }
        }
        catch (const exception &e)
        {
            PrintDebugString("PLUGIN|CRASHED while transitioning state (enter)!!! %s", e.what());
            m_currentState = createFailedState();
            try
            {
                m_currentState->enter();
            }
            catch (const exception &e2)
            {
                PrintDebugString("PLUGIN|CRASHED while entering FAILED state!!! %s", e2.what());
            }
        }
    }

    shared_ptr<PluginState> createStoppedState()
    {
        return make_shared<StoppedState>(m_hostServices);
    }

    shared_ptr<PluginState> createWorldAssemblingState()
    {
        const auto onAssembled = [this](shared_ptr<World> world) {
            m_hostServices->writeLog("PLUGIN|createWorldAssemblingState");
            m_world = world;
            transitionToState([this]() {
                return createSchedulesStartingState();
            });
        };

        const auto onFailed = [this] {
            transitionToState([this]() {
                return createFailedState();
            });
        };

        return make_shared<WorldAssemblingState>(m_hostServices, m_menu, onAssembled, onFailed);
    }

    shared_ptr<PluginState> createWorldAssembledState()
    {
        return make_shared<WorldAssembledState>(m_hostServices, m_menu, [this](float loadFactor) {
            m_schedulesLoadFactor = loadFactor;
            transitionToState([this]() {
                return createSchedulesStartingState();
            });
        });
    }

    shared_ptr<PluginState> createSchedulesStartingState()
    {
        return make_shared<SchedulesStartingState>(
            m_hostServices,
            m_world,
            m_menu,
            m_schedulesLoadFactor,
            [this](shared_ptr<Airport> userAirport) {
                transitionToState([this, userAirport]() {
                    return createSchedulesStartedState(userAirport);
                });
            },
            [this](const string& error) {
                m_hostServices->writeLog("PLUGIN|schedules were not loaded (%s), offering to try again", error.c_str());
                transitionToState([this]() {
                    return createWorldAssembledState();
                });
            }
        );
    }

    shared_ptr<PluginState> createSchedulesStartedState(shared_ptr<Airport> userAirport)
    {
        return make_shared<SchedulesStartedState>(
            m_hostServices,
            m_world,
            m_menu,
            userAirport,
            [this]() {
                transitionToState([this]() {
                    return createWorldAssembledState();
                });
            },
            [this](float loadFactor) {
                transitionToState([this, loadFactor]() {
                    m_schedulesLoadFactor = loadFactor;
                    return createSchedulesStartingState();
                });
            }
        );
    }

    shared_ptr<PluginState> createFailedState()
    {
        return make_shared<FailedState>();
    }

    void flightLoopTick()
    {
        if (!m_currentState)
        {
            return;
        }

        try
        {
            m_currentState->ping();
        }
        catch(const exception& e)
        {
            m_hostServices->writeLog(
                "PLUGIN|flightLoopTick ping state[%s] CRASHED!!! %s",
                m_currentState->name().c_str(),
                e.what());
        }
    }

private:

    static chrono::time_point<chrono::high_resolution_clock, chrono::microseconds> getNow()
    {
        return std::chrono::time_point_cast<std::chrono::microseconds>(chrono::high_resolution_clock::now());
    }

    static float pluginFlightLoopCallback(
        float  inElapsedSinceLastCall,
        float  inElapsedTimeSinceLastFlightLoop,
        int    inCounter,
        void * inRefcon)
    {
        if (inRefcon)
        {
            static_cast<PluginInstance*>(inRefcon)->flightLoopTick();
        }

        // Maintain 20 Hz fixed timestep (50 ms between calls).
        return 0.05f;
    }

};
