#!/usr/bin/env python3

import json
import re
import sys

try:
    import nodriver as uc
except (ImportError, ModuleNotFoundError) as exc:
    raise SystemExit("nodriver is required to scrape AirNavRadar airport schedules") from exc


AIRPORT_HREF_RE = re.compile(r"/data/airports/([A-Z]{4})(?:\b|/)", re.IGNORECASE)
CALLSIGN_RE = re.compile(r"\b([A-Z]{2,3}\d{1,4}[A-Z]?)\b")
TIME_RE = re.compile(r"^(?:[01]?\d|2[0-3]):[0-5]\d(?:AM|PM)?$")
STATUS_SKIP_WORDS = {
    "ARRIVED",
    "LANDED",
    "CANCELLED",
    "CANCELED",
    "DIVERTED",
    "DEPARTED",
}
MODEL_SKIP_WORDS = {
    "AIRLINE",
    "ARRIVAL",
    "DEPARTURE",
    "DELAYED",
    "ESTIMATED",
    "SCHEDULED",
    "STATUS",
}


def compact(text):
    return " ".join((text or "").split())


def split_callsign(call_sign):
    call_sign = compact(call_sign).upper()
    match = re.match(r"^([A-Z]{3})(\d{1,4}[A-Z]?)$", call_sign)
    if match:
        return match.group(1), match.group(2)
    return "", call_sign


def is_time_token(token):
    return bool(TIME_RE.match(token))


def tokenize(text):
    return [token.strip("()[],:;").upper() for token in compact(text).split() if token.strip("()[],:;")]


def extract_callsign(text):
    for match in CALLSIGN_RE.finditer(compact(text).upper()):
        return match.group(1)
    return ""


def extract_registration(text, excluded):
    excluded = {item for item in excluded if item}
    for token in tokenize(text):
        if token in excluded or token in STATUS_SKIP_WORDS or is_time_token(token):
            continue
        if re.match(r"^N\d{1,5}[A-Z]{0,2}$", token):
            return token
        if re.match(r"^[A-Z]{1,2}-[A-Z0-9]{2,6}$", token):
            return token
    return ""


def extract_model(text, excluded):
    excluded = {item for item in excluded if item}
    for token in tokenize(text):
        if token in excluded or token in MODEL_SKIP_WORDS or token in STATUS_SKIP_WORDS or is_time_token(token):
            continue
        if 3 <= len(token) <= 4 and any(ch.isalpha() for ch in token) and any(ch.isdigit() for ch in token):
            return token
    return ""


def has_skip_status(text):
    upper_text = compact(text).upper()
    return any(word in upper_text for word in STATUS_SKIP_WORDS)


def field_text(cells_by_id, names):
    for name in names:
        value = cells_by_id.get(name, "")
        if value:
            return value
    return ""


def find_counterpart_icao(current_icao, cells):
    for cell in cells:
        for link in cell["links"]:
            match = AIRPORT_HREF_RE.search(link["href"])
            if not match:
                continue
            airport_icao = match.group(1).upper()
            if airport_icao != current_icao:
                return airport_icao
    return ""


async def maybe_click(tab, selector, timeout):
    try:
        item = await tab.select(selector, timeout=timeout)
    except Exception:
        return False
    if not item:
        return False
    try:
        await item.click()
        return True
    except Exception:
        return False


async def accept_cookies(tab):
    for selector in (".fc-cta-consent", "#didomi-notice-agree-button"):
        if await maybe_click(tab, selector, 2):
            await tab.sleep(1)
            return

    for text in ("Accept all", "Accept", "I agree", "Agree"):
        try:
            item = await tab.find(text, best_match=True, timeout=1.5)
        except Exception:
            continue
        if not item:
            continue
        try:
            await item.click()
            await tab.sleep(1)
            return
        except Exception:
            continue


async def extract_links(cell):
    links = []
    for anchor in await cell.query_selector_all("a[href]"):
        href = compact(getattr(anchor.attrs, "href", ""))
        text = compact(anchor.text_all)
        if href:
            links.append({"href": href, "text": text})
    return links


async def read_cells(row):
    values = []
    for cell in await row.query_selector_all("td"):
        values.append({
            "id": compact(getattr(cell.attrs, "id", "")).lower(),
            "text": compact(cell.text_all),
            "links": await extract_links(cell),
        })
    return values


def build_candidate(direction, current_icao, cells):
    cells = [cell for cell in cells if cell["text"] or cell["links"]]
    if len(cells) < 3:
        return None

    joined_text = " ".join(cell["text"] for cell in cells if cell["text"])
    if not joined_text or has_skip_status(joined_text):
        return None

    counterpart_icao = find_counterpart_icao(current_icao, cells)
    if not counterpart_icao:
        return None

    cells_by_id = {}
    for cell in cells:
        if cell["id"] and cell["id"] not in cells_by_id:
            cells_by_id[cell["id"]] = cell["text"]

    call_sign_text = field_text(cells_by_id, ["callsign", "call", "flight", "flightnumber", "number"]) or joined_text
    call_sign = extract_callsign(call_sign_text)
    airline_icao, flight_no = split_callsign(call_sign)

    registration_text = field_text(cells_by_id, ["registration", "reg", "tail"]) or joined_text
    registration = extract_registration(registration_text, {current_icao, counterpart_icao, call_sign})

    model_text = field_text(cells_by_id, ["aircraft", "type", "equipment", "model"]) or joined_text
    model_icao = extract_model(model_text, {current_icao, counterpart_icao, call_sign, registration})

    return {
        "kind": direction,
        "counterpart_icao": counterpart_icao,
        "call_sign": call_sign,
        "flight_no": flight_no,
        "airline_icao": airline_icao,
        "registration": registration,
        "model_icao": model_icao,
    }


async def scrape_direction(browser, airport_icao, tab_name, kind):
    tab = await browser.get(f"https://www.airnavradar.com/data/airports/{airport_icao}?tab={tab_name}")
    await tab.select("body", timeout=15)
    await accept_cookies(tab)
    await tab.sleep(3)

    results = []
    seen = set()
    for row in await tab.select_all("tr", timeout=5):
        cells = await read_cells(row)
        candidate = build_candidate(kind, airport_icao, cells)
        if not candidate:
            continue

        dedupe_key = (
            candidate["kind"],
            candidate["counterpart_icao"],
            candidate["call_sign"],
            candidate["flight_no"],
            candidate["registration"],
        )
        if dedupe_key in seen:
            continue

        seen.add(dedupe_key)
        results.append(candidate)

    return results


def interleave(arrivals, departures):
    merged = []
    for index in range(max(len(arrivals), len(departures))):
        if index < len(arrivals):
            merged.append(arrivals[index])
        if index < len(departures):
            merged.append(departures[index])

    for sequence, candidate in enumerate(merged):
        candidate["sequence"] = sequence

    return merged


async def main():
    if len(sys.argv) < 2:
        raise SystemExit("usage: airnavradar_airport_schedules.py ICAO")

    airport_icao = compact(sys.argv[1]).upper()
    browser = await uc.start(
        headless=True,
        lang="en-US",
        browser_args=[
            "--no-sandbox",
            "--disable-dev-shm-usage",
            "--disable-gpu",
        ],
    )

    try:
        arrivals = await scrape_direction(browser, airport_icao, "arrivals", "arrival")
        departures = await scrape_direction(browser, airport_icao, "departures", "departure")
        payload = {
            "airport_icao": airport_icao,
            "candidates": interleave(arrivals, departures),
        }
        sys.stdout.write(json.dumps(payload))
    finally:
        browser.stop()


if __name__ == "__main__":
    uc.loop().run_until_complete(main())
