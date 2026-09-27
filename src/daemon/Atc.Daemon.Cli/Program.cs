// See https://aka.ms/new-console-template for more information

using System.CommandLine;
using System.Text.Json;
using System.Text.Json.Serialization;
using Atc.Server;
using Atc.World.Data;
using AtcdProto;

int cliExitCode = 0;

Console.WriteLine("Welcome to atcli, a CLI for Air Traffic & Control simulation daemon (atcd)");

var rootCommand = new RootCommand();

var scheduleCommand = new Command("schedule", "Fetch airport schedules");
rootCommand.Add(scheduleCommand);
var scheduleFetchCommand = new Command("fetch", "Fetch live airport schedules from AirNavRadar using nodriver");
scheduleCommand.Add(scheduleFetchCommand);

var airportIcaoOption = new Option<string>(name: "--airport", description: "ICAO airport code to query");
airportIcaoOption.IsRequired = true;

var pythonExecutableOption = new Option<string>(
    name: "--python",
    getDefaultValue: () => "python",
    description: "Python executable used to run the bundled nodriver scraper");

var timeoutSecondsOption = new Option<int>(
    name: "--timeout-seconds",
    getDefaultValue: () => 45,
    description: "Maximum time to wait for the live schedule fetch");

var headedOption = new Option<bool>(
    name: "--headed",
    description: "Run the scraper with a visible browser window");

var compactJsonOption = new Option<bool>(
    name: "--compact",
    description: "Write compact JSON instead of indented JSON");

scheduleFetchCommand.Add(airportIcaoOption);
scheduleFetchCommand.Add(pythonExecutableOption);
scheduleFetchCommand.Add(timeoutSecondsOption);
scheduleFetchCommand.Add(headedOption);
scheduleFetchCommand.Add(compactJsonOption);

scheduleFetchCommand.SetHandler(
    async (airport, python, timeoutSeconds, headed, compactJson) =>
    {
        try
        {
            var source = new AirNavRadarAirportScheduleSource(
                pythonExecutablePath: python,
                scriptPath: GetBundledAirNavRadarScriptPath(),
                timeout: TimeSpan.FromSeconds(timeoutSeconds),
                headless: !headed);

            var snapshot = await source.GetAirportSchedule(airport);
            Console.WriteLine(JsonSerializer.Serialize(snapshot, CreateJsonOptions(compactJson)));
        }
        catch (Exception e)
        {
            cliExitCode = 210;
            Console.Error.WriteLine("ERROR! " + e.Message);
        }
    },
    airportIcaoOption,
    pythonExecutableOption,
    timeoutSecondsOption,
    headedOption,
    compactJsonOption);

var monitorCommand = new Command("monitor", "Monitor radio stations");
rootCommand.Add(monitorCommand);
var monitorStartCommand = new Command("start", "Start monitoring radio station");
monitorCommand.Add(monitorStartCommand);

var latitudeOption = new Option<double>(name: "--lat", description: "Latitude of the radio receiver");
latitudeOption.IsRequired = true;

var longitudeOption = new Option<double>(name: "--lon", description: "Longitude of the radio receiver");
longitudeOption.IsRequired = true;

var frequencyKhzOption = new Option<int>(name: "--khz", description: "Frequency to tune in KHz");
frequencyKhzOption.IsRequired = true;

monitorStartCommand.Add(latitudeOption);
monitorStartCommand.Add(longitudeOption);
monitorStartCommand.Add(frequencyKhzOption);

monitorStartCommand.SetHandler(
    async (lat, lon, khz) => {
        Console.WriteLine($"Starting radio monitor: lat[{lat}] lon[{lon}] khz[{khz}]");
        try
        {
            await using (var client = CreateServiceClient())
            {
                await client.SendEnvelope(new AtcdClientToServer() {
                    start_radio_monitor_request = new AtcdClientToServer.StartRadioMonitorRequest() {
                        LocationLat = (float)lat,
                        LocationLon = (float)lon,
                        FrequencyKhz = khz
                    }
                });

                var replyEnvelope = await client.WaitForIncomingEnvelope(e => e.start_radio_monitor_reply != null, 10000);
                if (replyEnvelope == null)
                {
                    cliExitCode = 100;
                    Console.Error.WriteLine("Send request to daemon, but timed out waiting for reply");
                }
                else if (!replyEnvelope.start_radio_monitor_reply.Success)
                {
                    cliExitCode = 10;
                    Console.WriteLine($"Could not start radio monitor: error={replyEnvelope.start_radio_monitor_reply.Error}");
                }
                else
                {
                    Console.WriteLine($"Radio monitor started.");
                }
            }
        }
        catch (Exception e)
        {
            cliExitCode = 200;
            Console.Error.WriteLine("ERROR! " + e.ToString());
        }
    },
    latitudeOption, 
    longitudeOption, 
    frequencyKhzOption
);

await rootCommand.InvokeAsync(args);
return cliExitCode;


static WebSocketServiceClient<AtcdClientToServer, AtcdServerToClient> CreateServiceClient()
{
    return new WebSocketServiceClient<AtcdClientToServer, AtcdServerToClient>("ws://localhost:3001/atc");
}

static string GetBundledAirNavRadarScriptPath()
{
    return Path.Combine(AppContext.BaseDirectory, "airnavradar", "airnavradar_airport_schedules.py");
}

static JsonSerializerOptions CreateJsonOptions(bool compactJson)
{
    var options = new JsonSerializerOptions
    {
        WriteIndented = !compactJson,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull
    };
    options.Converters.Add(new JsonStringEnumConverter());
    return options;
}
