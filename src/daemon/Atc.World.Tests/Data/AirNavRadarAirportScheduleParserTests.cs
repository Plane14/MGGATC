using Atc.World.Contracts.Data;
using Atc.World.Data;
using FluentAssertions;
using NUnit.Framework;

namespace Atc.World.Tests.Data;

[TestFixture]
public class AirNavRadarAirportScheduleParserTests
{
    [Test]
    public void Parse_ShouldMapNormalizedScheduleSnapshot()
    {
        const string json = @"{
  ""airportIcao"": ""lemd"",
  ""fetchedAtUtc"": ""2026-09-27T19:00:00Z"",
  ""source"": ""airnavradar-nodriver"",
  ""arrivals"": [
    {
      ""flightNumber"": ""ibe1234"",
      ""callsign"": ""ibe1234"",
      ""tailNo"": ""ecmxy"",
      ""aircraftTypeIcao"": ""a320"",
      ""airlineIcao"": ""ibe"",
      ""originIcao"": ""lebl"",
      ""destinationIcao"": ""lemd"",
      ""scheduledTimeUtc"": ""2026-09-27T19:20:00Z"",
      ""estimatedTimeUtc"": ""2026-09-27T19:18:00Z""
    }
  ],
  ""departures"": [
    {
      ""flightNumber"": ""ryr4321"",
      ""callsign"": ""ryr4321"",
      ""tailNo"": ""eirua"",
      ""aircraftTypeIcao"": ""b738"",
      ""airlineIcao"": ""ryr"",
      ""originIcao"": ""lemd"",
      ""destinationIcao"": ""egkk"",
      ""scheduledTimeUtc"": ""2026-09-27T20:00:00Z"",
      ""actualTimeUtc"": ""2026-09-27T20:03:00Z""
    }
  ]
}";

        var snapshot = AirNavRadarAirportScheduleParser.Parse(json);

        snapshot.AirportIcao.Should().Be("LEMD");
        snapshot.Source.Should().Be("airnavradar-nodriver");
        snapshot.Arrivals.Should().ContainSingle();
        snapshot.Departures.Should().ContainSingle();

        var arrival = snapshot.Arrivals.Single();
        arrival.Direction.Should().Be(AirportScheduleDirection.Arrival);
        arrival.FlightNumber.Should().Be("IBE1234");
        arrival.Callsign.Should().Be("IBE1234");
        arrival.TailNo.Should().Be("ECMXY");
        arrival.AircraftTypeIcao.Should().Be("A320");
        arrival.AirlineIcao.Should().Be("IBE");
        arrival.OriginIcao.Should().Be("LEBL");
        arrival.DestinationIcao.Should().Be("LEMD");
        arrival.ScheduledTimeUtc.Should().Be(DateTime.Parse("2026-09-27T19:20:00Z").ToUniversalTime());
        arrival.EstimatedTimeUtc.Should().Be(DateTime.Parse("2026-09-27T19:18:00Z").ToUniversalTime());

        var departure = snapshot.Departures.Single();
        departure.Direction.Should().Be(AirportScheduleDirection.Departure);
        departure.FlightNumber.Should().Be("RYR4321");
        departure.ActualTimeUtc.Should().Be(DateTime.Parse("2026-09-27T20:03:00Z").ToUniversalTime());
    }

    [Test]
    public void Parse_ShouldSkipIncompleteEntriesAndFallbackScheduledTime()
    {
        const string json = @"{
  ""airportIcao"": ""LEMD"",
  ""fetchedAtUtc"": ""2026-09-27T19:00:00Z"",
  ""source"": """",
  ""arrivals"": [
    {
      ""flightNumber"": """",
      ""originIcao"": ""LEPA"",
      ""destinationIcao"": ""LEMD"",
      ""scheduledTimeUtc"": ""2026-09-27T19:20:00Z""
    }
  ],
  ""departures"": [
    {
      ""flightNumber"": ""AEA987"",
      ""callsign"": ""aea987"",
      ""originIcao"": ""LEMD"",
      ""destinationIcao"": ""LEVC"",
      ""estimatedTimeUtc"": ""2026-09-27T20:25:00Z""
    }
  ]
}";

        var snapshot = AirNavRadarAirportScheduleParser.Parse(json);

        snapshot.Source.Should().Be("airnavradar-nodriver");
        snapshot.Arrivals.Should().BeEmpty();
        snapshot.Departures.Should().ContainSingle();
        snapshot.Departures.Single().ScheduledTimeUtc.Should().Be(DateTime.Parse("2026-09-27T20:25:00Z").ToUniversalTime());
        snapshot.Departures.Single().Callsign.Should().Be("AEA987");
    }
}
