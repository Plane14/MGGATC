//
// This file is part of AT&C project which simulates virtual world of air traffic and ATC.
// Code licensing terms are available at https://github.com/felix-b/atc/blob/master/LICENSE
//
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <utility>
#include "stlhelpers.h"
#include "libworld.h"
#include "libdataxp.h"

using namespace std;
using namespace world;

namespace
{
    typedef unordered_map<string, GeoPoint> NavPointLookup;

    static string trim(const string& value)
    {
        size_t start = 0;
        while (start < value.length() && isspace((unsigned char)value[start]))
        {
            start++;
        }

        size_t end = value.length();
        while (end > start && isspace((unsigned char)value[end - 1]))
        {
            end--;
        }

        return value.substr(start, end - start);
    }

    static bool startsWith(const string& value, const string& prefix)
    {
        return value.length() >= prefix.length() && value.compare(0, prefix.length(), prefix) == 0;
    }

    static vector<string> splitWhitespace(const string& value)
    {
        vector<string> parts;
        stringstream input(value);
        string token;
        while (input >> token)
        {
            parts.push_back(token);
        }
        return parts;
    }

    static vector<string> splitPreservingEmpty(const string& value, char delimiter)
    {
        vector<string> parts;
        string current;
        for (char c : value)
        {
            if (c == delimiter)
            {
                parts.push_back(current);
                current.clear();
            }
            else
            {
                current.push_back(c);
            }
        }
        parts.push_back(current);
        return parts;
    }

    static bool tryParseDouble(const string& value, double& output)
    {
        string trimmed = trim(value);
        if (trimmed.empty())
        {
            return false;
        }

        char* end = nullptr;
        output = strtod(trimmed.c_str(), &end);
        return end && *end == 0;
    }

    static string normalizeProcedureName(const string& name)
    {
        string normalized;
        normalized.reserve(name.length());
        for (char c : name)
        {
            if (isalnum((unsigned char)c))
            {
                normalized.push_back((char)toupper((unsigned char)c));
            }
        }
        return normalized;
    }

    static float parseAltitudeValue(const string& value)
    {
        string digits;
        digits.reserve(value.length());
        for (char c : trim(value))
        {
            if (isdigit((unsigned char)c) || (digits.empty() && (c == '+' || c == '-')))
            {
                digits.push_back(c);
            }
        }

        if (digits.empty() || digits == "+" || digits == "-")
        {
            return 0.0f;
        }

        return (float)atof(digits.c_str());
    }

    static float parseAltitudeConstraint(const vector<string>& fields)
    {
        float altitude1 = fields.size() > 23 ? parseAltitudeValue(fields[23]) : 0.0f;
        float altitude2 = fields.size() > 24 ? parseAltitudeValue(fields[24]) : 0.0f;
        return max(altitude1, altitude2);
    }

    static float parseCourseHeading(const vector<string>& fields)
    {
        if (fields.size() <= 20)
        {
            return 0.0f;
        }

        string digits;
        for (char c : trim(fields[20]))
        {
            if (isdigit((unsigned char)c))
            {
                digits.push_back(c);
            }
        }

        if (digits.empty())
        {
            return 0.0f;
        }

        float value = (float)atof(digits.c_str());
        while (value > 360.0f)
        {
            value /= 10.0f;
        }
        return value;
    }

    static void loadFixDat(istream& input, NavPointLookup& lookup)
    {
        string line;
        while (getline(input, line))
        {
            string trimmed = trim(line);
            if (trimmed.empty() || (!isdigit((unsigned char)trimmed[0]) && trimmed[0] != '-' && trimmed[0] != '+'))
            {
                continue;
            }

            stringstream row(trimmed);
            double latitude;
            double longitude;
            string ident;
            row >> latitude >> longitude >> ident;
            if (!ident.empty())
            {
                lookup.insert({ ident, GeoPoint(latitude, longitude, 0) });
            }
        }
    }

    static void loadNavDat(istream& input, NavPointLookup& lookup)
    {
        string line;
        while (getline(input, line))
        {
            vector<string> fields = splitWhitespace(line);
            if (fields.size() < 8)
            {
                continue;
            }

            double latitude = 0;
            double longitude = 0;
            if (!tryParseDouble(fields[1], latitude) || !tryParseDouble(fields[2], longitude))
            {
                continue;
            }

            const string& ident = fields[7];
            if (!ident.empty())
            {
                lookup.insert({ ident, GeoPoint(latitude, longitude, 0) });
            }
        }
    }

    static shared_ptr<istream> tryOpenHostFile(shared_ptr<HostServices> host, const vector<string>& relativePathParts)
    {
        try
        {
            return host->openFileForRead(host->getHostFilePath(relativePathParts));
        }
        catch (const exception&)
        {
            return nullptr;
        }
    }

    static bool resolveProcedurePoint(
        const shared_ptr<Airport>& airport,
        const string& arrivalRunway,
        const string& fixIdent,
        const NavPointLookup& fixes,
        const NavPointLookup& navaids,
        GeoPoint& resolvedPoint)
    {
        const string normalizedFix = normalizeProcedureName(fixIdent);
        const string normalizedRunway = normalizeProcedureName(arrivalRunway);
        const string normalizedRunwayFix = normalizeProcedureName("RW" + arrivalRunway);
        if (airport && !arrivalRunway.empty() && (
            normalizedFix == normalizedRunway ||
            normalizedFix == normalizedRunwayFix))
        {
            resolvedPoint = airport->getRunwayEndOrThrow(arrivalRunway).centerlinePoint().geo();
            return true;
        }

        auto fixed = fixes.find(fixIdent);
        if (fixed != fixes.end())
        {
            resolvedPoint = fixed->second;
            return true;
        }

        auto navaid = navaids.find(fixIdent);
        if (navaid != navaids.end())
        {
            resolvedPoint = navaid->second;
            return true;
        }

        return false;
    }
}

XPFmsxReader::XPFmsxReader(shared_ptr<HostServices> _host) :
    m_host(_host)
{
}

shared_ptr<FlightPlan> XPFmsxReader::readFrom(istream &input)
{
    time_t departureTime = m_host->getWorld()->currentTime() + 45 * 60;
    time_t arrivalTime = departureTime + 180 * 60;
    auto plan = shared_ptr<FlightPlan>(new FlightPlan(departureTime, arrivalTime, "", ""));

    vector<Line> lines;
    parseInputLines(input, lines);

    if (isFmsFormat(lines))
    {
        parseFmsFormat(plan, lines);
    }
    else if (isFmxFormat(lines))
    {
        parseFmxFormat(plan, lines);
    }
    else
    {
        throw runtime_error("Flight plan file format not recognized");
    }

    tryLoadMissedApproachNavData(plan);
    return plan;
}

void XPFmsxReader::parseInputLines(istream &input, vector<Line> &lines)
{
    while (!input.eof() && !input.bad())
    {
        string text;

        try
        {
            getline(input, text);
        }
        catch(const exception &)
        {
            break;
        }

        size_t delimitierIndex = text.find_first_of(",: ");
        if (delimitierIndex != text.npos)
        {
            string token = text.substr(0, delimitierIndex);
            string suffix = text.substr(delimitierIndex + 1);

            if (!token.empty())
            {
                lines.push_back({ token, suffix, text.at(delimitierIndex), text });
            }
        }
    }
}

bool XPFmsxReader::isFmsFormat(const vector<Line> &lines)
{
    auto v11it = find_if(lines.begin(), lines.end(), [](const Line& line){
        return (line.token == "1100" && line.suffix == "Version");
    });
    bool foundV11 = (v11it != lines.end());
    return foundV11;
}

bool XPFmsxReader::isFmxFormat(const vector<Line> &lines)
{
    if (lines.empty())
    {
        return false;
    }

    const string& firstLineSuffix = lines.at(0).suffix;
    int commaCount = countCharOccurrences(firstLineSuffix, ',');
    return commaCount == 3;
}

void XPFmsxReader::parseFmsFormat(shared_ptr<FlightPlan> plan, const vector<Line> &lines)
{
    int routeStartIndex = -1;

    for (int i = 0 ; i < (int)lines.size() ; i++)
    {
        const Line& line = lines.at(i);

        if (line.token != "NUMENR")
        {
            addValue(plan, line.token, line.suffix);
        }
        else
        {
            routeStartIndex = i + 1;
            break;
        }
    }

    if (routeStartIndex >= 0)
    {
        parseFmsRouteLegs(plan, lines, routeStartIndex);
    }
}

void XPFmsxReader::parseFmxFormat(shared_ptr<FlightPlan> plan, const vector<Line> &lines)
{
    bool isEnrouteSection = true;
    vector<RoutePoint> routePoints;

    for (int i = 0 ; i < (int)lines.size() ; i++)
    {
        const Line& line = lines.at(i);

        if (isEnrouteSection)
        {
            bool continueEnrouteSection = (countCharOccurrences(line.suffix, ',') == 3);
            if (!continueEnrouteSection && plan->arrivalAirportIcao().empty() && i > 0)
            {
                plan->setArrivalAirportIcao(lines.at(i - 1).token);
            }
            isEnrouteSection = continueEnrouteSection;
        }

        if (isEnrouteSection)
        {
            if (plan->departureAirportIcao().empty())
            {
                plan->setDepartureAirportIcao(line.token);
            }

            vector<string> fields = splitPreservingEmpty(line.text, ',');
            if (fields.size() >= 5)
            {
                double latitude = 0;
                double longitude = 0;
                if (tryParseDouble(fields[3], latitude) && tryParseDouble(fields[4], longitude))
                {
                    routePoints.push_back({
                        trim(fields[0]),
                        GeoPoint(latitude, longitude, 0)
                    });
                }
            }
        }
        else
        {
            addValue(plan, line.token, line.suffix);
        }
    }

    addRouteLegs(plan, routePoints);
}

void XPFmsxReader::parseFmsRouteLegs(shared_ptr<FlightPlan> plan, const vector<Line>& lines, int startIndex)
{
    vector<RoutePoint> routePoints;

    for (int i = startIndex ; i < (int)lines.size() ; i++)
    {
        vector<string> fields = splitWhitespace(lines[i].text);
        if (fields.size() < 6)
        {
            continue;
        }

        double latitude = 0;
        double longitude = 0;
        if (tryParseDouble(fields[4], latitude) && tryParseDouble(fields[5], longitude))
        {
            routePoints.push_back({
                fields[1],
                GeoPoint(latitude, longitude, 0)
            });
        }
    }

    addRouteLegs(plan, routePoints);
}

void XPFmsxReader::parseFmxRouteLegs(shared_ptr<FlightPlan> plan, const vector<Line>& lines)
{
    vector<RoutePoint> routePoints;
    for (const auto& line : lines)
    {
        vector<string> fields = splitPreservingEmpty(line.text, ',');
        if (fields.size() < 5)
        {
            break;
        }

        double latitude = 0;
        double longitude = 0;
        if (tryParseDouble(fields[3], latitude) && tryParseDouble(fields[4], longitude))
        {
            routePoints.push_back({
                trim(fields[0]),
                GeoPoint(latitude, longitude, 0)
            });
        }
    }

    addRouteLegs(plan, routePoints);
}

void XPFmsxReader::addRouteLegs(shared_ptr<FlightPlan> plan, const vector<RoutePoint>& routePoints)
{
    if (routePoints.size() < 2)
    {
        return;
    }

    for (int i = 0 ; i < (int)routePoints.size() - 1 ; i++)
    {
        const auto& fromPoint = routePoints[i];
        const auto& toPoint = routePoints[i + 1];
        plan->addLeg(make_shared<FlightPlan::Leg>(
            FlightPlan::LegType::EnRoute,
            GeoPolygon::empty(),
            fromPoint.ident,
            toPoint.ident,
            0.0f,
            0.0f,
            toPoint.location,
            true,
            GeoMath::getHeadingFromPoints(fromPoint.location, toPoint.location),
            true,
            "DRCT"
        ));
    }
}

void XPFmsxReader::tryLoadMissedApproachNavData(shared_ptr<FlightPlan> plan)
{
    if (!plan || plan->arrivalAirportIcao().empty() || plan->approachName().empty())
    {
        return;
    }

    shared_ptr<Airport> arrivalAirport;
    try
    {
        arrivalAirport = m_host->getWorld()->getAirport(plan->arrivalAirportIcao());
    }
    catch (const exception&)
    {
        return;
    }

    shared_ptr<istream> cifpFile = tryOpenHostFile(m_host, {
        "Custom Data", "CIFP", plan->arrivalAirportIcao() + ".dat"
    });
    if (!cifpFile)
    {
        cifpFile = tryOpenHostFile(m_host, {
            "Resources", "default data", "CIFP", plan->arrivalAirportIcao() + ".dat"
        });
    }
    if (!cifpFile)
    {
        return;
    }

    NavPointLookup fixes;
    NavPointLookup navaids;

    if (auto earthFixFile = tryOpenHostFile(m_host, { "Custom Data", "earth_fix.dat" }))
    {
        loadFixDat(*earthFixFile, fixes);
    }
    else if (auto earthFixFile = tryOpenHostFile(m_host, { "Resources", "default data", "earth_fix.dat" }))
    {
        loadFixDat(*earthFixFile, fixes);
    }

    if (auto earthNavFile = tryOpenHostFile(m_host, { "Custom Data", "earth_nav.dat" }))
    {
        loadNavDat(*earthNavFile, navaids);
    }
    else if (auto earthNavFile = tryOpenHostFile(m_host, { "Resources", "default data", "earth_nav.dat" }))
    {
        loadNavDat(*earthNavFile, navaids);
    }

    const string normalizedApproachName = normalizeProcedureName(plan->approachName());
    bool inMissedApproachSection = false;
    string previousFix = plan->arrivalRunway();

    string line;
    while (getline(*cifpFile, line))
    {
        if (!startsWith(line, "APPCH:") && !startsWith(line, "PRDAT:"))
        {
            continue;
        }

        vector<string> fields = splitPreservingEmpty(line.substr(line.find(':') + 1), ',');
        if (fields.size() < 12 || normalizeProcedureName(trim(fields[2])) != normalizedApproachName)
        {
            continue;
        }

        const string descCode = fields.size() > 8 ? trim(fields[8]) : "";
        const string pathTerm = fields.size() > 11 ? trim(fields[11]) : "";
        const string fixIdent = fields.size() > 4 ? trim(fields[4]) : "";
        const string routeQualifier2 = fields.size() > 37 ? trim(fields[37]) : "";
        const bool isPrimaryMissedApproach = !routeQualifier2.empty() && (
            routeQualifier2[0] == 'A' ||
            routeQualifier2[0] == 'B' ||
            routeQualifier2[0] == 'E');
        const bool isFirstMissedApproachLeg = descCode.length() >= 3 && descCode[2] == 'M';
        const bool isMissedApproachPoint = descCode.length() >= 4 && descCode[3] == 'M';
        const bool shouldAddLeg = inMissedApproachSection || isPrimaryMissedApproach || isFirstMissedApproachLeg;

        if (shouldAddLeg)
        {
            GeoPoint resolvedPoint = GeoPoint::empty;
            bool hasTargetPoint = resolveProcedurePoint(
                arrivalAirport,
                plan->arrivalRunway(),
                fixIdent,
                fixes,
                navaids,
                resolvedPoint);

            plan->addLeg(make_shared<FlightPlan::Leg>(
                FlightPlan::LegType::GoAround,
                GeoPolygon::empty(),
                previousFix,
                fixIdent,
                parseAltitudeConstraint(fields),
                0.0f,
                resolvedPoint,
                hasTargetPoint,
                parseCourseHeading(fields),
                parseCourseHeading(fields) > 0.0f,
                pathTerm
            ));

            if (!fixIdent.empty())
            {
                previousFix = fixIdent;
            }
            inMissedApproachSection = true;
        }
        else if (isMissedApproachPoint)
        {
            inMissedApproachSection = true;
            if (!fixIdent.empty())
            {
                previousFix = fixIdent;
            }
        }
    }
}

void XPFmsxReader::addValue(shared_ptr<FlightPlan> plan, const string &key, const string &value)
{
    if (key == "ADEP")
    {
        plan->setDepartureAirportIcao(value);
    }
    else if (key == "ADES")
    {
        plan->setArrivalAirportIcao(value);
    }
    else if (key == "DEPRWY")
    {
        plan->setDepartureRunway(trimLead(value, "RW"));
    }
    else if (key == "DESRWY")
    {
        plan->setArrivalRunway(trimLead(value, "RW"));
    }
    else if (key == "SID")
    {
        plan->setSid(value);
    }
    else if (key == "SIDTRANS")
    {
        plan->setSidTransition(value);
    }
    else if (key == "STAR")
    {
        plan->setStar(value);
    }
    else if (key == "STARTRANS")
    {
        plan->setStarTransition(value);
    }
    else if (key == "APP")
    {
        plan->setApproach(value);
        if (plan->arrivalRunway().empty())
        {
            plan->setArrivalRunway(getRunwayFromApproachName(value));
        }
    }
    else if (key == "FLIGHT_NUM")
    {
        plan->setFlightNo(value);
    }
}

int XPFmsxReader::countCharOccurrences(const string& s, char c)
{
    return count_if(s.begin(), s.end(), [c](char ci){
        return (ci == c);
    });
}

string XPFmsxReader::trimLead(const string &s, const string& prefix)
{
    size_t pos = s.find(prefix);
    if (pos == 0)
    {
        string copy = s;
        copy.erase(0, prefix.length());
        return copy;
    }
    return s;
}

string XPFmsxReader::getRunwayFromApproachName(const string& approachName)
{
    string runwayName;
    bool copiedAnyDigits = false;

    for (int i = 0 ; i < (int)approachName.length() ; i++)
    {
        char c = approachName.at(i);

        if (isdigit((unsigned char)c))
        {
            runwayName += c;
            copiedAnyDigits = true;
        }
        else if (copiedAnyDigits)
        {
            if (c == 'L' || c == 'R' || c == 'C')
            {
                runwayName += c;
            }
            break;
        }
    }

    return runwayName;
}
