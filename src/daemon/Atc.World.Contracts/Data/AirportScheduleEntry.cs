using System;

namespace Atc.World.Contracts.Data;

public enum AirportScheduleDirection
{
    Arrival,
    Departure
}

public record AirportScheduleEntry(
    AirportScheduleDirection Direction,
    string FlightNumber,
    string? Callsign,
    string? TailNo,
    string? AircraftTypeIcao,
    string? AirlineIcao,
    string OriginIcao,
    string DestinationIcao,
    DateTime ScheduledTimeUtc,
    DateTime? EstimatedTimeUtc,
    DateTime? ActualTimeUtc
);
