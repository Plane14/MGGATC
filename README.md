# Welcome to virtual world of air traffic and control simulation [early development]

Our goal is to create a comprehensive and accurate ATC (air traffic control) simulation. We simulate both air traffic and the ATC units as two halves of a whole. The platform for user interaction is the [X-Plane flight simulator](https://www.x-plane.com/), where AT&C loads as a plugin and lets the user choose between 1st-person pilot or 1st-person controller experience. The project aims at adding both educational and entertainment value for X-Plane users. **[See demo video](https://youtu.be/VeK6mcrzLWk)**.

## Live airport schedules

`atcli` now includes a nodriver-backed AirNavRadar fetcher that can be used as an alternate live airport schedule source.

1. Install the Python dependency:
   - `python -m pip install -r tools/airnavradar/requirements.txt`
2. Fetch an airport schedule, for example Madrid/LEMD:
   - `dotnet run --project src/daemon/Atc.Daemon.Cli -- schedule fetch --airport LEMD`
3. If you need to watch the browser solve a challenge interactively, add `--headed`.
