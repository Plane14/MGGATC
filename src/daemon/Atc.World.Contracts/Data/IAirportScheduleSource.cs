using System.Threading;
using System.Threading.Tasks;

namespace Atc.World.Contracts.Data;

public interface IAirportScheduleSource
{
    Task<AirportScheduleSnapshot> GetAirportSchedule(string airportIcao, CancellationToken cancellationToken = default);
}
