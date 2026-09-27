using System;
using System.Collections.Generic;

namespace Atc.World.Contracts.Data;

public record AirportScheduleSnapshot(
    string AirportIcao,
    DateTime FetchedAtUtc,
    IReadOnlyList<AirportScheduleEntry> Arrivals,
    IReadOnlyList<AirportScheduleEntry> Departures,
    string Source
);
