using System.Globalization;
using System.Text.Json;
using System.Text.Json.Serialization;
using Atc.World.Contracts.Data;

namespace Atc.World.Data;

public static class AirNavRadarAirportScheduleParser
{
    private static readonly JsonSerializerOptions SerializerOptions = new()
    {
        PropertyNameCaseInsensitive = true
    };

    public static AirportScheduleSnapshot Parse(string json)
    {
        var payload = JsonSerializer.Deserialize<AirportScheduleSnapshotPayload>(json, SerializerOptions)
            ?? throw new InvalidOperationException("AirNavRadar scraper returned an empty JSON payload.");

        var airportIcao = NormalizeRequiredCode(payload.AirportIcao, nameof(payload.AirportIcao));
        var fetchedAtUtcText = payload.FetchedAtUtc
            ?? throw new InvalidOperationException("AirNavRadar scraper payload is missing required field 'FetchedAtUtc'.");
        var fetchedAtUtc = ParseRequiredUtc(fetchedAtUtcText, nameof(payload.FetchedAtUtc));
        var source = string.IsNullOrWhiteSpace(payload.Source)
            ? "airnavradar-nodriver"
            : payload.Source.Trim();

        return new AirportScheduleSnapshot(
            AirportIcao: airportIcao,
            FetchedAtUtc: fetchedAtUtc,
            Arrivals: MapEntries(payload.Arrivals, AirportScheduleDirection.Arrival),
            Departures: MapEntries(payload.Departures, AirportScheduleDirection.Departure),
            Source: source);
    }

    private static AirportScheduleEntry[] MapEntries(
        AirportScheduleEntryPayload[]? payloads,
        AirportScheduleDirection direction)
    {
        if (payloads == null || payloads.Length == 0)
        {
            return Array.Empty<AirportScheduleEntry>();
        }

        return payloads
            .Select(payload => TryMapEntry(payload, direction))
            .Where(entry => entry != null)
            .Cast<AirportScheduleEntry>()
            .ToArray();
    }

    private static AirportScheduleEntry? TryMapEntry(
        AirportScheduleEntryPayload payload,
        AirportScheduleDirection direction)
    {
        var flightNumber = NormalizeOptionalCode(payload.FlightNumber);
        var originIcao = NormalizeOptionalCode(payload.OriginIcao);
        var destinationIcao = NormalizeOptionalCode(payload.DestinationIcao);

        if (string.IsNullOrEmpty(flightNumber) ||
            string.IsNullOrEmpty(originIcao) ||
            string.IsNullOrEmpty(destinationIcao))
        {
            return null;
        }

        var scheduledTimeUtcText = FirstNonEmpty(payload.ScheduledTimeUtc, payload.EstimatedTimeUtc, payload.ActualTimeUtc);
        if (scheduledTimeUtcText == null)
        {
            return null;
        }

        return new AirportScheduleEntry(
            Direction: direction,
            FlightNumber: flightNumber,
            Callsign: NormalizeOptionalCode(payload.Callsign),
            TailNo: NormalizeOptionalCode(payload.TailNo),
            AircraftTypeIcao: NormalizeOptionalCode(payload.AircraftTypeIcao),
            AirlineIcao: NormalizeOptionalCode(payload.AirlineIcao),
            OriginIcao: originIcao,
            DestinationIcao: destinationIcao,
            ScheduledTimeUtc: ParseRequiredUtc(scheduledTimeUtcText, nameof(payload.ScheduledTimeUtc)),
            EstimatedTimeUtc: ParseOptionalUtc(payload.EstimatedTimeUtc),
            ActualTimeUtc: ParseOptionalUtc(payload.ActualTimeUtc));
    }

    private static string NormalizeRequiredCode(string? value, string parameterName)
    {
        return NormalizeOptionalCode(value)
            ?? throw new InvalidOperationException($"AirNavRadar scraper payload is missing required field '{parameterName}'.");
    }

    private static string? NormalizeOptionalCode(string? value)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            return null;
        }

        return string.Concat(value.Where(c => !char.IsWhiteSpace(c))).ToUpperInvariant();
    }

    private static string? FirstNonEmpty(params string?[] candidates)
    {
        return candidates.FirstOrDefault(candidate => !string.IsNullOrWhiteSpace(candidate));
    }

    private static DateTime ParseRequiredUtc(string value, string parameterName)
    {
        return ParseOptionalUtc(value)
            ?? throw new InvalidOperationException($"AirNavRadar scraper payload field '{parameterName}' is not a valid UTC timestamp.");
    }

    private static DateTime? ParseOptionalUtc(string? value)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            return null;
        }

        if (!DateTime.TryParse(
            value,
            CultureInfo.InvariantCulture,
            DateTimeStyles.AdjustToUniversal | DateTimeStyles.AssumeUniversal,
            out var parsed))
        {
            return null;
        }

        return parsed;
    }

    private sealed record AirportScheduleSnapshotPayload(
        [property: JsonPropertyName("airportIcao")] string? AirportIcao,
        [property: JsonPropertyName("fetchedAtUtc")] string? FetchedAtUtc,
        [property: JsonPropertyName("arrivals")] AirportScheduleEntryPayload[]? Arrivals,
        [property: JsonPropertyName("departures")] AirportScheduleEntryPayload[]? Departures,
        [property: JsonPropertyName("source")] string? Source);

    private sealed record AirportScheduleEntryPayload(
        [property: JsonPropertyName("flightNumber")] string? FlightNumber,
        [property: JsonPropertyName("callsign")] string? Callsign,
        [property: JsonPropertyName("tailNo")] string? TailNo,
        [property: JsonPropertyName("aircraftTypeIcao")] string? AircraftTypeIcao,
        [property: JsonPropertyName("airlineIcao")] string? AirlineIcao,
        [property: JsonPropertyName("originIcao")] string? OriginIcao,
        [property: JsonPropertyName("destinationIcao")] string? DestinationIcao,
        [property: JsonPropertyName("scheduledTimeUtc")] string? ScheduledTimeUtc,
        [property: JsonPropertyName("estimatedTimeUtc")] string? EstimatedTimeUtc,
        [property: JsonPropertyName("actualTimeUtc")] string? ActualTimeUtc);
}
