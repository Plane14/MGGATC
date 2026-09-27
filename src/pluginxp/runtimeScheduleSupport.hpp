#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "libworld.h"

namespace runtime_schedule
{
    using namespace std;
    using namespace world;

    inline string trim(const string& value)
    {
        size_t start = 0;
        while (start < value.length() && isspace(static_cast<unsigned char>(value[start])))
        {
            start++;
        }

        size_t end = value.length();
        while (end > start && isspace(static_cast<unsigned char>(value[end - 1])))
        {
            end--;
        }

        return value.substr(start, end - start);
    }

    inline string upper(const string& value)
    {
        string result = value;
        transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
            return static_cast<char>(toupper(c));
        });
        return result;
    }

    inline string sanitizeCallsign(const string& value)
    {
        string result;
        string trimmed = upper(trim(value));
        for (char c : trimmed)
        {
            if (isalnum(static_cast<unsigned char>(c)))
            {
                result.push_back(c);
            }
        }
        return result;
    }

    class JsonValue
    {
    public:
        enum class Type
        {
            Null,
            Bool,
            Number,
            String,
            Array,
            Object
        };

    private:
        Type m_type;
        bool m_boolValue;
        double m_numberValue;
        string m_stringValue;
        vector<JsonValue> m_arrayValue;
        map<string, JsonValue> m_objectValue;

    public:
        JsonValue() :
            m_type(Type::Null),
            m_boolValue(false),
            m_numberValue(0)
        {
        }

        explicit JsonValue(bool value) :
            m_type(Type::Bool),
            m_boolValue(value),
            m_numberValue(0)
        {
        }

        explicit JsonValue(double value) :
            m_type(Type::Number),
            m_boolValue(false),
            m_numberValue(value)
        {
        }

        explicit JsonValue(const string& value) :
            m_type(Type::String),
            m_boolValue(false),
            m_numberValue(0),
            m_stringValue(value)
        {
        }

        explicit JsonValue(const vector<JsonValue>& value) :
            m_type(Type::Array),
            m_boolValue(false),
            m_numberValue(0),
            m_arrayValue(value)
        {
        }

        explicit JsonValue(const map<string, JsonValue>& value) :
            m_type(Type::Object),
            m_boolValue(false),
            m_numberValue(0),
            m_objectValue(value)
        {
        }

    public:
        Type type() const { return m_type; }
        bool isNull() const { return m_type == Type::Null; }
        bool isBool() const { return m_type == Type::Bool; }
        bool isNumber() const { return m_type == Type::Number; }
        bool isString() const { return m_type == Type::String; }
        bool isArray() const { return m_type == Type::Array; }
        bool isObject() const { return m_type == Type::Object; }

        bool boolOr(bool fallback) const
        {
            return isBool() ? m_boolValue : fallback;
        }

        double numberOr(double fallback) const
        {
            if (isNumber())
            {
                return m_numberValue;
            }

            if (isString())
            {
                char *end = nullptr;
                const char *text = m_stringValue.c_str();
                double parsed = strtod(text, &end);
                if (end != text && end != nullptr && *end == '\0')
                {
                    return parsed;
                }
            }

            return fallback;
        }

        string stringOr(const string& fallback) const
        {
            return isString() ? m_stringValue : fallback;
        }

        const vector<JsonValue>& arrayItems() const
        {
            static const vector<JsonValue> empty;
            return isArray() ? m_arrayValue : empty;
        }

        const map<string, JsonValue>& objectItems() const
        {
            static const map<string, JsonValue> empty;
            return isObject() ? m_objectValue : empty;
        }

        const JsonValue& at(const string& key) const
        {
            static const JsonValue empty;
            if (!isObject())
            {
                return empty;
            }

            auto it = m_objectValue.find(key);
            return it != m_objectValue.end()
                ? it->second
                : empty;
        }

    public:
        static JsonValue parse(const string& text)
        {
            class Parser
            {
            private:
                const string& m_text;
                size_t m_index;

            public:
                explicit Parser(const string& text) :
                    m_text(text),
                    m_index(0)
                {
                }

                JsonValue parseDocument()
                {
                    skipWhitespace();
                    JsonValue result = parseValue();
                    skipWhitespace();
                    if (m_index != m_text.length())
                    {
                        throw runtime_error("Unexpected trailing JSON content");
                    }
                    return result;
                }

            private:
                JsonValue parseValue()
                {
                    if (m_index >= m_text.length())
                    {
                        throw runtime_error("Unexpected end of JSON input");
                    }

                    char c = m_text[m_index];
                    switch (c)
                    {
                    case 'n':
                        consumeLiteral("null");
                        return JsonValue();
                    case 't':
                        consumeLiteral("true");
                        return JsonValue(true);
                    case 'f':
                        consumeLiteral("false");
                        return JsonValue(false);
                    case '"':
                        return JsonValue(parseString());
                    case '[':
                        return JsonValue(parseArray());
                    case '{':
                        return JsonValue(parseObject());
                    default:
                        if (c == '-' || isdigit(static_cast<unsigned char>(c)))
                        {
                            return JsonValue(parseNumber());
                        }
                        throw runtime_error("Unexpected JSON token");
                    }
                }

                vector<JsonValue> parseArray()
                {
                    vector<JsonValue> values;
                    expect('[');
                    skipWhitespace();

                    if (peek(']'))
                    {
                        expect(']');
                        return values;
                    }

                    while (true)
                    {
                        skipWhitespace();
                        values.push_back(parseValue());
                        skipWhitespace();

                        if (peek(']'))
                        {
                            expect(']');
                            return values;
                        }

                        expect(',');
                    }
                }

                map<string, JsonValue> parseObject()
                {
                    map<string, JsonValue> values;
                    expect('{');
                    skipWhitespace();

                    if (peek('}'))
                    {
                        expect('}');
                        return values;
                    }

                    while (true)
                    {
                        skipWhitespace();
                        string key = parseString();
                        skipWhitespace();
                        expect(':');
                        skipWhitespace();
                        values.insert({ key, parseValue() });
                        skipWhitespace();

                        if (peek('}'))
                        {
                            expect('}');
                            return values;
                        }

                        expect(',');
                    }
                }

                string parseString()
                {
                    string value;
                    expect('"');

                    while (m_index < m_text.length())
                    {
                        char c = m_text[m_index++];
                        if (c == '"')
                        {
                            return value;
                        }

                        if (c != '\\')
                        {
                            value.push_back(c);
                            continue;
                        }

                        if (m_index >= m_text.length())
                        {
                            throw runtime_error("Unexpected end of JSON escape sequence");
                        }

                        char escaped = m_text[m_index++];
                        switch (escaped)
                        {
                        case '"': value.push_back('"'); break;
                        case '\\': value.push_back('\\'); break;
                        case '/': value.push_back('/'); break;
                        case 'b': value.push_back('\b'); break;
                        case 'f': value.push_back('\f'); break;
                        case 'n': value.push_back('\n'); break;
                        case 'r': value.push_back('\r'); break;
                        case 't': value.push_back('\t'); break;
                        case 'u':
                        {
                            value.append(parseUnicodeEscape());
                            break;
                        }
                        default:
                            throw runtime_error("Unsupported JSON escape sequence");
                        }
                    }

                    throw runtime_error("Unterminated JSON string");
                }

                string parseUnicodeEscape()
                {
                    if (m_index + 4 > m_text.length())
                    {
                        throw runtime_error("Invalid JSON unicode escape");
                    }

                    int codePoint = 0;
                    for (int i = 0 ; i < 4 ; i++)
                    {
                        char c = m_text[m_index++];
                        codePoint <<= 4;
                        if (c >= '0' && c <= '9')
                        {
                            codePoint |= (c - '0');
                        }
                        else if (c >= 'a' && c <= 'f')
                        {
                            codePoint |= (10 + c - 'a');
                        }
                        else if (c >= 'A' && c <= 'F')
                        {
                            codePoint |= (10 + c - 'A');
                        }
                        else
                        {
                            throw runtime_error("Invalid JSON unicode escape");
                        }
                    }

                    string result;
                    if (codePoint <= 0x7F)
                    {
                        result.push_back(static_cast<char>(codePoint));
                    }
                    else if (codePoint <= 0x7FF)
                    {
                        result.push_back(static_cast<char>(0xC0 | ((codePoint >> 6) & 0x1F)));
                        result.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
                    }
                    else
                    {
                        result.push_back(static_cast<char>(0xE0 | ((codePoint >> 12) & 0x0F)));
                        result.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
                        result.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
                    }
                    return result;
                }

                double parseNumber()
                {
                    size_t start = m_index;

                    if (m_text[m_index] == '-')
                    {
                        m_index++;
                    }

                    while (m_index < m_text.length() && isdigit(static_cast<unsigned char>(m_text[m_index])))
                    {
                        m_index++;
                    }

                    if (m_index < m_text.length() && m_text[m_index] == '.')
                    {
                        m_index++;
                        while (m_index < m_text.length() && isdigit(static_cast<unsigned char>(m_text[m_index])))
                        {
                            m_index++;
                        }
                    }

                    if (m_index < m_text.length() && (m_text[m_index] == 'e' || m_text[m_index] == 'E'))
                    {
                        m_index++;
                        if (m_index < m_text.length() && (m_text[m_index] == '+' || m_text[m_index] == '-'))
                        {
                            m_index++;
                        }
                        while (m_index < m_text.length() && isdigit(static_cast<unsigned char>(m_text[m_index])))
                        {
                            m_index++;
                        }
                    }

                    return stod(m_text.substr(start, m_index - start));
                }

                void skipWhitespace()
                {
                    while (m_index < m_text.length() && isspace(static_cast<unsigned char>(m_text[m_index])))
                    {
                        m_index++;
                    }
                }

                void consumeLiteral(const char *value)
                {
                    size_t length = strlen(value);
                    if (m_text.compare(m_index, length, value) != 0)
                    {
                        throw runtime_error("Invalid JSON literal");
                    }
                    m_index += length;
                }

                void expect(char expected)
                {
                    if (m_index >= m_text.length() || m_text[m_index] != expected)
                    {
                        throw runtime_error("Unexpected JSON character");
                    }
                    m_index++;
                }

                bool peek(char expected) const
                {
                    return m_index < m_text.length() && m_text[m_index] == expected;
                }
            };

            return Parser(text).parseDocument();
        }
    };

    struct LiveAircraft
    {
        string hex;
        string callSign;
        string registration;
        string modelIcao;
        GeoPoint location;
        double altitudeFeet;
        double groundSpeedKt;
        bool onGround;
    };

    struct RouteAirport
    {
        string icao;
        GeoPoint location;
    };

    struct RouteData
    {
        string callSign;
        string flightNo;
        string airlineIcao;
        bool plausible;
        bool known;
        vector<RouteAirport> airports;
    };

    enum class CandidateType
    {
        DepartureOnly,
        ArrivalOnly,
        Turnaround
    };

    struct LiveScheduleCandidate
    {
        CandidateType type;
        LiveAircraft aircraft;
        RouteData route;
        string originIcao;
        string destinationIcao;
        string onwardDestinationIcao;
        size_t airportIndex;
        double distanceMeters;
    };

    struct MilitaryAirbase
    {
        string icao;
        string name;
        string country;
        string operation;
        string primaryModelIcao;
        string secondaryModelIcao;
        string tertiaryModelIcao;
    };

    inline bool tryParseGroundAltitude(const JsonValue& value, double& altitudeFeet, bool& onGround)
    {
        altitudeFeet = 0;
        onGround = false;

        if (value.isString())
        {
            string text = upper(trim(value.stringOr("")));
            if (text == "GROUND")
            {
                onGround = true;
                return true;
            }

            char *end = nullptr;
            const char *number = text.c_str();
            altitudeFeet = strtod(number, &end);
            return (end != number && end != nullptr && *end == '\0');
        }

        if (value.isNumber())
        {
            altitudeFeet = value.numberOr(0);
            return true;
        }

        return false;
    }

    inline vector<LiveAircraft> parsePointResponse(const string& body)
    {
        vector<LiveAircraft> results;
        JsonValue root = JsonValue::parse(body);

        for (const auto& item : root.at("ac").arrayItems())
        {
            if (!item.isObject())
            {
                continue;
            }

            string callSign = sanitizeCallsign(item.at("flight").stringOr(""));
            double lat = item.at("lat").numberOr(numeric_limits<double>::quiet_NaN());
            double lon = item.at("lon").numberOr(numeric_limits<double>::quiet_NaN());
            if (callSign.empty() || std::isnan(lat) || std::isnan(lon))
            {
                continue;
            }

            double altitudeFeet = 0;
            bool onGround = false;
            tryParseGroundAltitude(item.at("alt_baro"), altitudeFeet, onGround);

            results.push_back({
                upper(trim(item.at("hex").stringOr(""))),
                callSign,
                upper(trim(item.at("r").stringOr(""))),
                upper(trim(item.at("t").stringOr(""))),
                GeoPoint(lat, lon),
                altitudeFeet,
                item.at("gs").numberOr(0),
                onGround
            });
        }

        return results;
    }

    inline RouteData parseRouteResponse(const string& body)
    {
        JsonValue root = JsonValue::parse(body);
        if (!root.at("error").isNull())
        {
            return { "", "", "", false, false, {} };
        }

        RouteData route = {
            sanitizeCallsign(root.at("callsign").stringOr("")),
            upper(trim(root.at("number").stringOr(""))),
            upper(trim(root.at("airline_code").stringOr(""))),
            root.at("plausible").boolOr(true),
            false,
            {}
        };

        string airportCodes = upper(trim(root.at("airport_codes").stringOr("")));
        if (airportCodes.empty() || airportCodes == "UNKNOWN")
        {
            route.known = false;
            return route;
        }

        for (const auto& item : root.at("_airports").arrayItems())
        {
            string icao = upper(trim(item.at("icao").stringOr("")));
            double lat = item.at("lat").numberOr(numeric_limits<double>::quiet_NaN());
            double lon = item.at("lon").numberOr(numeric_limits<double>::quiet_NaN());
            if (icao.empty() || std::isnan(lat) || std::isnan(lon))
            {
                continue;
            }

            route.airports.push_back({
                icao,
                GeoPoint(lat, lon)
            });
        }

        route.known = route.airports.size() >= 2;
        if (route.flightNo == "UNKNOWN")
        {
            route.flightNo.clear();
        }
        if (route.airlineIcao == "UNKNOWN")
        {
            route.airlineIcao.clear();
        }

        return route;
    }

    inline bool isGroundState(const LiveAircraft& aircraft)
    {
        return aircraft.onGround || (
            aircraft.altitudeFeet >= 0 &&
            aircraft.altitudeFeet <= 1200 &&
            aircraft.groundSpeedKt <= 80);
    }

    inline bool isAtAirport(const GeoPoint& airportLocation, const LiveAircraft& aircraft)
    {
        return GeoMath::getDistanceMeters(airportLocation, aircraft.location) <= 8000.0;
    }

    inline bool tryBuildCandidate(
        const string& airportIcao,
        const GeoPoint& airportLocation,
        const LiveAircraft& aircraft,
        const RouteData& route,
        LiveScheduleCandidate& candidate)
    {
        if (!route.known || !route.plausible || route.airports.size() < 2)
        {
            return false;
        }

        size_t airportIndex = route.airports.size();
        for (size_t i = 0 ; i < route.airports.size() ; i++)
        {
            if (route.airports[i].icao == airportIcao)
            {
                airportIndex = i;
                break;
            }
        }

        if (airportIndex >= route.airports.size())
        {
            return false;
        }

        double distanceMeters = GeoMath::getDistanceMeters(airportLocation, aircraft.location);
        bool groundState = isGroundState(aircraft);
        bool atAirport = isAtAirport(airportLocation, aircraft);

        candidate.aircraft = aircraft;
        candidate.route = route;
        candidate.airportIndex = airportIndex;
        candidate.distanceMeters = distanceMeters;
        candidate.originIcao.clear();
        candidate.destinationIcao.clear();
        candidate.onwardDestinationIcao.clear();

        if (airportIndex == 0)
        {
            if (!groundState || !atAirport)
            {
                return false;
            }

            candidate.type = CandidateType::DepartureOnly;
            candidate.originIcao = airportIcao;
            candidate.destinationIcao = route.airports[1].icao;
            return !candidate.destinationIcao.empty() && candidate.destinationIcao != airportIcao;
        }

        if (airportIndex == route.airports.size() - 1)
        {
            if (groundState)
            {
                return false;
            }

            candidate.type = CandidateType::ArrivalOnly;
            candidate.originIcao = route.airports[airportIndex - 1].icao;
            candidate.destinationIcao = airportIcao;
            return !candidate.originIcao.empty() && candidate.originIcao != airportIcao;
        }

        if (groundState && atAirport)
        {
            candidate.type = CandidateType::DepartureOnly;
            candidate.originIcao = airportIcao;
            candidate.destinationIcao = route.airports[airportIndex + 1].icao;
            return !candidate.destinationIcao.empty() && candidate.destinationIcao != airportIcao;
        }

        if (!groundState)
        {
            candidate.type = CandidateType::Turnaround;
            candidate.originIcao = route.airports[airportIndex - 1].icao;
            candidate.destinationIcao = airportIcao;
            candidate.onwardDestinationIcao = route.airports[airportIndex + 1].icao;
            return (
                !candidate.originIcao.empty() &&
                !candidate.onwardDestinationIcao.empty() &&
                candidate.originIcao != airportIcao &&
                candidate.onwardDestinationIcao != airportIcao);
        }

        return false;
    }

    inline string buildArrivalCallsign(const string& callSign)
    {
        return sanitizeCallsign(callSign) + "A";
    }

    inline string chooseTailNumber(const LiveAircraft& aircraft, const string& fallback)
    {
        if (!aircraft.registration.empty())
        {
            return aircraft.registration;
        }
        if (!aircraft.hex.empty())
        {
            return aircraft.hex;
        }
        return fallback;
    }

    inline vector<LiveScheduleCandidate> parseAirNavRadarScheduleResponse(
        const string& body,
        const string& airportIcao,
        const GeoPoint& airportLocation)
    {
        vector<LiveScheduleCandidate> results;
        JsonValue root = JsonValue::parse(body);
        const string normalizedAirportIcao = upper(trim(airportIcao));
        const GeoPoint unknownLocation = GeoPoint::empty;

        for (const auto& item : root.at("candidates").arrayItems())
        {
            if (!item.isObject())
            {
                continue;
            }

            const string kind = upper(trim(item.at("kind").stringOr("")));
            const string counterpartIcao = upper(trim(item.at("counterpart_icao").stringOr("")));
            if (
                counterpartIcao.empty() ||
                counterpartIcao == normalizedAirportIcao ||
                (kind != "ARRIVAL" && kind != "DEPARTURE"))
            {
                continue;
            }

            string airlineIcao = upper(trim(item.at("airline_icao").stringOr("")));
            string flightNo = upper(trim(item.at("flight_no").stringOr("")));
            string callSign = sanitizeCallsign(item.at("call_sign").stringOr(""));
            if (callSign.empty() && !airlineIcao.empty() && !flightNo.empty())
            {
                callSign = sanitizeCallsign(airlineIcao + flightNo);
            }
            if (callSign.empty())
            {
                callSign = sanitizeCallsign(flightNo);
            }
            if (callSign.empty())
            {
                callSign = sanitizeCallsign(
                    normalizedAirportIcao +
                    string(kind == "ARRIVAL" ? "A" : "D") +
                    to_string(results.size() + 1));
            }

            RouteData route = {
                callSign,
                flightNo,
                airlineIcao,
                true,
                true,
                {}
            };

            LiveScheduleCandidate candidate = {};
            candidate.aircraft = {
                "",
                callSign,
                upper(trim(item.at("registration").stringOr(""))),
                upper(trim(item.at("model_icao").stringOr(""))),
                airportLocation,
                item.at("altitude_ft").numberOr(0),
                item.at("ground_speed_kt").numberOr(0),
                kind == "DEPARTURE"
            };
            candidate.route = route;
            candidate.airportIndex = kind == "DEPARTURE" ? 0 : 1;
            candidate.distanceMeters = item.at("distance_meters").numberOr(
                item.at("sequence").numberOr(results.size()));

            if (kind == "DEPARTURE")
            {
                candidate.type = CandidateType::DepartureOnly;
                candidate.originIcao = normalizedAirportIcao;
                candidate.destinationIcao = counterpartIcao;
                candidate.route.airports.push_back({ normalizedAirportIcao, airportLocation });
                candidate.route.airports.push_back({ counterpartIcao, unknownLocation });
            }
            else
            {
                candidate.type = CandidateType::ArrivalOnly;
                candidate.originIcao = counterpartIcao;
                candidate.destinationIcao = normalizedAirportIcao;
                candidate.route.airports.push_back({ counterpartIcao, unknownLocation });
                candidate.route.airports.push_back({ normalizedAirportIcao, airportLocation });
            }

            results.push_back(candidate);
        }

        return results;
    }

    inline vector<string> splitCsvRow(const string& line)
    {
        vector<string> values;
        string current;
        bool insideQuotes = false;

        for (char c : line)
        {
            if (c == '"')
            {
                insideQuotes = !insideQuotes;
                continue;
            }

            if (c == ',' && !insideQuotes)
            {
                values.push_back(trim(current));
                current.clear();
                continue;
            }

            current.push_back(c);
        }

        values.push_back(trim(current));
        return values;
    }

    inline vector<MilitaryAirbase> parseMilitaryAirbasesCsv(istream& input)
    {
        vector<MilitaryAirbase> airbases;
        string line;
        bool skippedHeader = false;

        while (getline(input, line))
        {
            line = trim(line);
            if (line.empty() || line[0] == '#')
            {
                continue;
            }

            const vector<string> values = splitCsvRow(line);
            if (!skippedHeader)
            {
                skippedHeader = true;
                continue;
            }

            if (values.size() < 7)
            {
                continue;
            }

            MilitaryAirbase airbase = {
                upper(values[0]),
                values[1],
                values[2],
                upper(values[3]),
                upper(values[4]),
                upper(values[5]),
                upper(values[6])
            };

            if (!airbase.icao.empty())
            {
                airbases.push_back(airbase);
            }
        }

        return airbases;
    }

    inline const MilitaryAirbase* findMilitaryAirbase(
        const vector<MilitaryAirbase>& airbases,
        const string& icao)
    {
        const string targetIcao = upper(trim(icao));
        for (const auto& airbase : airbases)
        {
            if (airbase.icao == targetIcao)
            {
                return &airbase;
            }
        }

        return nullptr;
    }
}
