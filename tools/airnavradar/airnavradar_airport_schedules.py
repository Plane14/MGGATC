#!/usr/bin/env python3

import argparse
import json
import sys
import time
from datetime import datetime, timezone
from typing import Any

import nodriver as uc


SEARCH_KEYS = {
    "arrivals": "mrgapdstic",
    "departures": "mrgaporgic",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Fetch live AirNavRadar airport schedules through a nodriver browser session."
    )
    parser.add_argument("--airport", required=True, help="Airport ICAO code, e.g. LEMD")
    parser.add_argument(
        "--timeout-seconds",
        type=int,
        default=45,
        help="Maximum number of seconds to wait for the page challenge and JSON fetches",
    )
    parser.add_argument(
        "--headed",
        action="store_true",
        help="Run with a visible browser window instead of headless mode",
    )
    return parser.parse_args()


def normalize_code(value: Any) -> str | None:
    if not isinstance(value, str):
        return None

    normalized = "".join(value.split()).upper()
    return normalized or None


def iso_utc_now() -> str:
    return datetime.now(tz=timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def epoch_to_iso(value: Any) -> str | None:
    if not isinstance(value, (int, float)) or value <= 0:
        return None

    return (
        datetime.fromtimestamp(value, tz=timezone.utc)
        .replace(microsecond=0)
        .isoformat()
        .replace("+00:00", "Z")
    )


def pick_string(*values: Any) -> str | None:
    for value in values:
        normalized = normalize_code(value)
        if normalized:
            return normalized
    return None


def extract_items(payload: Any) -> list[dict[str, Any]]:
    if isinstance(payload, dict):
        items = payload.get("list")
        if isinstance(items, list):
            return [item for item in items if isinstance(item, dict)]
        return []

    if isinstance(payload, list):
        items: list[dict[str, Any]] = []
        for chunk in payload:
            if not isinstance(chunk, dict):
                continue
            list_items = chunk.get("list")
            if isinstance(list_items, list):
                items.extend(item for item in list_items if isinstance(item, dict))
        return items

    return []


def extract_schedule_entry(entry: dict[str, Any], airport_icao: str, direction: str) -> dict[str, Any] | None:
    origin_icao = normalize_code(entry.get("aporgic"))
    destination_icao = normalize_code(entry.get("apdstic"))
    if not origin_icao or not destination_icao or origin_icao == destination_icao:
        return None

    if direction == "arrivals" and destination_icao != airport_icao:
        return None

    if direction == "departures" and origin_icao != airport_icao:
        return None

    flight_number = pick_string(
        entry.get("fnia"),
        entry.get("fnic"),
        entry.get("cs"),
        entry.get("ectlcs"),
        entry.get("acr"),
    )
    if not flight_number:
        return None

    scheduled_time = epoch_to_iso(entry.get("depsu" if direction == "departures" else "arrsu"))
    estimated_time = epoch_to_iso(entry.get("depeu" if direction == "departures" else "arreu"))
    actual_time = epoch_to_iso(entry.get("depau" if direction == "departures" else "arrau"))

    if not scheduled_time:
        scheduled_time = (
            estimated_time
            or actual_time
            or epoch_to_iso(entry.get("depsts" if direction == "departures" else "arrsts"))
        )

    if not scheduled_time:
        return None

    aircraft_type = pick_string(entry.get("act"))
    if aircraft_type in {"GRND", "ZZZZ"}:
        aircraft_type = None

    return {
        "flightNumber": flight_number,
        "callsign": pick_string(
            entry.get("cs"),
            entry.get("fnic"),
            entry.get("ectlcs"),
            entry.get("fnia"),
            entry.get("acr"),
        ),
        "tailNo": pick_string(entry.get("acr")),
        "aircraftTypeIcao": aircraft_type,
        "airlineIcao": pick_string(entry.get("csalic"), entry.get("alic")),
        "originIcao": origin_icao,
        "destinationIcao": destination_icao,
        "scheduledTimeUtc": scheduled_time,
        "estimatedTimeUtc": estimated_time,
        "actualTimeUtc": actual_time,
    }


async def fetch_endpoint(tab: Any, relative_path: str) -> dict[str, Any]:
    expression = f"""
        (async () => {{
            const response = await fetch({json.dumps(relative_path)}, {{
                credentials: 'include',
                headers: {{
                    accept: 'application/json, text/plain, */*'
                }}
            }});

            return {{
                ok: response.ok,
                status: response.status,
                url: response.url,
                title: document.title,
                body: await response.text()
            }};
        }})()
    """
    result = await tab.evaluate(expression, await_promise=True, return_by_value=True)
    if not isinstance(result, dict):
        raise RuntimeError(f"Unexpected browser result while fetching {relative_path}: {result!r}")
    return result


async def load_snapshot(airport_icao: str, timeout_seconds: int, headless: bool) -> dict[str, Any]:
    browser = await uc.start(
        headless=headless,
        browser_args=["--no-sandbox", "--disable-dev-shm-usage"],
    )

    try:
        tab = await browser.get(f"https://www.airnavradar.com/data/airports/{airport_icao}")
        await tab.select("body")

        deadline = time.monotonic() + timeout_seconds
        last_error = f"Timed out waiting for AirNavRadar schedule data for {airport_icao}."

        while time.monotonic() < deadline:
            schedules: dict[str, list[dict[str, Any]]] = {
                "arrivals": [],
                "departures": [],
            }

            try:
                any_success = False
                for direction, key in SEARCH_KEYS.items():
                    fetch_result = await fetch_endpoint(tab, f"/data/airports/search/{airport_icao}?key={key}")
                    status = int(fetch_result.get("status", 0))
                    body = fetch_result.get("body", "")

                    if status != 200:
                        last_error = (
                            f"{direction} fetch returned HTTP {status} while page title was "
                            f"{fetch_result.get('title', '<unknown>')!r}."
                        )
                        continue

                    payload = json.loads(body)
                    schedules[direction] = [
                        normalized
                        for item in extract_items(payload)
                        if (normalized := extract_schedule_entry(item, airport_icao, direction)) is not None
                    ]
                    any_success = True

                if any_success:
                    return {
                        "airportIcao": airport_icao,
                        "fetchedAtUtc": iso_utc_now(),
                        "source": "airnavradar-nodriver",
                        "arrivals": schedules["arrivals"],
                        "departures": schedules["departures"],
                    }
            except Exception as exc:  # noqa: BLE001
                last_error = str(exc)

            await tab.sleep(1)

        raise RuntimeError(last_error)
    finally:
        browser.stop()


def main() -> int:
    args = parse_args()
    airport_icao = normalize_code(args.airport)
    if not airport_icao:
        print("Airport ICAO must not be empty.", file=sys.stderr)
        return 2

    if args.timeout_seconds <= 0:
        print("Timeout must be a positive number of seconds.", file=sys.stderr)
        return 2

    try:
        snapshot = uc.loop().run_until_complete(
            load_snapshot(
                airport_icao=airport_icao,
                timeout_seconds=args.timeout_seconds,
                headless=not args.headed,
            )
        )
    except Exception as exc:  # noqa: BLE001
        print(str(exc), file=sys.stderr)
        return 1

    json.dump(snapshot, sys.stdout)
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
