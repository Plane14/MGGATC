//
// This file is part of AT&C project which simulates virtual world of air traffic and ATC.
// Code licensing terms are available at https://github.com/felix-b/atc/blob/master/LICENSE
//

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "aircraftPerformanceProfileService.hpp"

using namespace std;

namespace world
{
    namespace
    {
        struct RawPerformanceRow
        {
            unordered_map<string, string> values;
        };

        struct Doc8643Row
        {
            string aircraftClass;
            string wakeTurbulenceCategory;
        };

        string trim(const string& value)
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

        string upper(const string& value)
        {
            string result = value;
            transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
                return static_cast<char>(toupper(c));
            });
            return result;
        }

        vector<string> splitCsvLine(const string& line)
        {
            vector<string> values;
            string current;
            for (char c : line)
            {
                if (c == ',')
                {
                    values.push_back(trim(current));
                    current.clear();
                }
                else
                {
                    current.push_back(c);
                }
            }
            values.push_back(trim(current));
            return values;
        }

        bool tryParseInt(const unordered_map<string, string>& values, const string& key, int& result)
        {
            auto it = values.find(key);
            if (it == values.end() || it->second.empty())
            {
                return false;
            }

            char *end = nullptr;
            const char *text = it->second.c_str();
            long parsed = strtol(text, &end, 10);
            if (end == text || !end || *end != '\0')
            {
                return false;
            }

            result = (int)parsed;
            return true;
        }

        bool tryParseDouble(const unordered_map<string, string>& values, const string& key, double& result)
        {
            auto it = values.find(key);
            if (it == values.end() || it->second.empty())
            {
                return false;
            }

            char *end = nullptr;
            const char *text = it->second.c_str();
            double parsed = strtod(text, &end);
            if (end == text || !end || *end != '\0')
            {
                return false;
            }

            result = parsed;
            return true;
        }

        bool tryParseString(const unordered_map<string, string>& values, const string& key, string& result)
        {
            auto it = values.find(key);
            if (it == values.end() || it->second.empty())
            {
                return false;
            }

            result = trim(it->second);
            return !result.empty();
        }

        unordered_map<string, RawPerformanceRow> parsePerformanceRows(istream& stream)
        {
            unordered_map<string, RawPerformanceRow> rowsByIcao;
            string headerLine;
            if (!getline(stream, headerLine))
            {
                return rowsByIcao;
            }

            const vector<string> headers = splitCsvLine(headerLine);
            string line;
            while (getline(stream, line))
            {
                line = trim(line);
                if (line.empty())
                {
                    continue;
                }

                const vector<string> cells = splitCsvLine(line);
                RawPerformanceRow row;
                for (size_t i = 0 ; i < headers.size() && i < cells.size() ; i++)
                {
                    row.values.insert({ headers[i], cells[i] });
                }

                string icao;
                if (!tryParseString(row.values, "ICAO", icao))
                {
                    continue;
                }

                rowsByIcao.insert({ upper(icao), row });
            }

            return rowsByIcao;
        }

        unordered_map<string, Doc8643Row> parseDoc8643Rows(istream& stream)
        {
            unordered_map<string, Doc8643Row> rowsByIcao;
            string line;
            while (getline(stream, line))
            {
                if (trim(line).empty())
                {
                    continue;
                }

                vector<string> fields;
                string current;
                stringstream row(line);
                while (getline(row, current, '\t'))
                {
                    fields.push_back(trim(current));
                }

                if (fields.size() < 5)
                {
                    continue;
                }

                const string icao = upper(fields[2]);
                if (icao.empty() || icao == "ICAO")
                {
                    continue;
                }

                rowsByIcao[icao] = { upper(fields[3]), upper(fields[4]) };
            }

            return rowsByIcao;
        }

        Aircraft::Category deriveCategory(const string& doc8643Class, const string& wakeTurbulenceCategory)
        {
            const string aircraftClass = upper(doc8643Class);
            const string wtc = upper(wakeTurbulenceCategory);
            Aircraft::Category category = Aircraft::Category::None;

            if (!wtc.empty() && (wtc == "H" || wtc == "J"))
            {
                category = category | Aircraft::Category::Heavy;
            }

            if (!aircraftClass.empty() && aircraftClass.at(0) == 'H')
            {
                category = category | Aircraft::Category::Helicopter;
                return category;
            }

            char propulsion = aircraftClass.empty()
                ? '\0'
                : aircraftClass.at(aircraftClass.length() - 1);
            switch (propulsion)
            {
            case 'J':
                category = category | Aircraft::Category::Jet;
                break;
            case 'T':
                category = category | Aircraft::Category::Turboprop;
                break;
            case 'P':
                category = category | (wtc == "L"
                    ? Aircraft::Category::LightProp
                    : Aircraft::Category::Prop);
                break;
            }

            if (category == Aircraft::Category::None)
            {
                category = (wtc == "L")
                    ? Aircraft::Category::LightProp
                    : Aircraft::Category::Jet;
            }

            return category;
        }

        void applyCategoryDefaults(AircraftPerformanceProfileService::Profile& profile)
        {
            if ((profile.category & Aircraft::Category::Helicopter) == Aircraft::Category::Helicopter)
            {
                profile.takeOffV2Kt = 60;
                profile.takeOffDistanceMeters = 300;
                profile.initialClimbIASKt = 70;
                profile.initialClimbRocFpm = 1000;
                profile.climb150IASKt = 80;
                profile.climb150RocFpm = 900;
                profile.climb240IASKt = 90;
                profile.climb240RocFpm = 800;
                profile.cruiseTasKt = 120;
                profile.cruiseMach = 0;
                profile.cruiseCeilingFl = 120;
                profile.initialDescentMach = 0;
                profile.descentIASKt = 90;
                profile.descentRodFpm = 700;
                profile.approachIASKt = 70;
                profile.approachRodFpm = 700;
                profile.landingVatKt = 50;
                profile.landingDistanceMeters = 250;
                profile.taxiNormalMetersPerSecond = 3.5;
                profile.taxiHighSpeedMetersPerSecond = 5.0;
                profile.taxiPushbackMetersPerSecond = 0.5;
                return;
            }

            if ((profile.category & Aircraft::Category::Jet) == Aircraft::Category::Jet &&
                (profile.category & Aircraft::Category::Heavy) == Aircraft::Category::Heavy)
            {
                profile.takeOffV2Kt = 165;
                profile.takeOffDistanceMeters = 2600;
                profile.initialClimbIASKt = 175;
                profile.initialClimbRocFpm = 2500;
                profile.climb150IASKt = 220;
                profile.climb150RocFpm = 2200;
                profile.climb240IASKt = 250;
                profile.climb240RocFpm = 1800;
                profile.machClimbMach = 0.80;
                profile.machClimbRocFpm = 1200;
                profile.cruiseTasKt = 470;
                profile.cruiseMach = 0.82;
                profile.cruiseCeilingFl = 410;
                profile.initialDescentMach = 0.80;
                profile.descentIASKt = 290;
                profile.descentRodFpm = 3200;
                profile.approachIASKt = 200;
                profile.approachRodFpm = 1400;
                profile.landingVatKt = 155;
                profile.landingDistanceMeters = 1900;
                profile.taxiNormalMetersPerSecond = 7.0;
                profile.taxiHighSpeedMetersPerSecond = 12.0;
                profile.taxiPushbackMetersPerSecond = 1.0;
                return;
            }

            if ((profile.category & Aircraft::Category::Jet) == Aircraft::Category::Jet)
            {
                profile.takeOffV2Kt = 145;
                profile.takeOffDistanceMeters = 2200;
                profile.initialClimbIASKt = 165;
                profile.initialClimbRocFpm = 2500;
                profile.climb150IASKt = 190;
                profile.climb150RocFpm = 2000;
                profile.climb240IASKt = 220;
                profile.climb240RocFpm = 1500;
                profile.machClimbMach = 0.74;
                profile.machClimbRocFpm = 1100;
                profile.cruiseTasKt = 440;
                profile.cruiseMach = 0.78;
                profile.cruiseCeilingFl = 390;
                profile.initialDescentMach = 0.76;
                profile.descentIASKt = 280;
                profile.descentRodFpm = 3000;
                profile.approachIASKt = 180;
                profile.approachRodFpm = 1300;
                profile.landingVatKt = 140;
                profile.landingDistanceMeters = 1500;
                profile.taxiNormalMetersPerSecond = 6.0;
                profile.taxiHighSpeedMetersPerSecond = 10.0;
                profile.taxiPushbackMetersPerSecond = 1.0;
                return;
            }

            if ((profile.category & Aircraft::Category::Turboprop) == Aircraft::Category::Turboprop)
            {
                profile.takeOffV2Kt = 120;
                profile.takeOffDistanceMeters = 1300;
                profile.initialClimbIASKt = 140;
                profile.initialClimbRocFpm = 1700;
                profile.climb150IASKt = 160;
                profile.climb150RocFpm = 1500;
                profile.climb240IASKt = 180;
                profile.climb240RocFpm = 1200;
                profile.machClimbMach = 0.48;
                profile.machClimbRocFpm = 900;
                profile.cruiseTasKt = 260;
                profile.cruiseMach = 0.48;
                profile.cruiseCeilingFl = 250;
                profile.initialDescentMach = 0.45;
                profile.descentIASKt = 180;
                profile.descentRodFpm = 2000;
                profile.approachIASKt = 130;
                profile.approachRodFpm = 1000;
                profile.landingVatKt = 115;
                profile.landingDistanceMeters = 1100;
                profile.taxiNormalMetersPerSecond = 5.0;
                profile.taxiHighSpeedMetersPerSecond = 8.0;
                profile.taxiPushbackMetersPerSecond = 1.0;
                return;
            }

            profile.takeOffV2Kt = 75;
            profile.takeOffDistanceMeters = 600;
            profile.initialClimbIASKt = 90;
            profile.initialClimbRocFpm = 900;
            profile.climb150IASKt = 100;
            profile.climb150RocFpm = 800;
            profile.climb240IASKt = 110;
            profile.climb240RocFpm = 700;
            profile.machClimbMach = 0;
            profile.machClimbRocFpm = 0;
            profile.cruiseTasKt = 130;
            profile.cruiseMach = 0;
            profile.cruiseCeilingFl = 120;
            profile.initialDescentMach = 0;
            profile.descentIASKt = 110;
            profile.descentRodFpm = 1000;
            profile.approachIASKt = 90;
            profile.approachRodFpm = 700;
            profile.landingVatKt = 60;
            profile.landingDistanceMeters = 500;
            profile.taxiNormalMetersPerSecond = 4.0;
            profile.taxiHighSpeedMetersPerSecond = 6.0;
            profile.taxiPushbackMetersPerSecond = 1.0;
        }

        AircraftPerformanceProfileService::Profile createProfile(
            const string& icao,
            const RawPerformanceRow* perfRow,
            const Doc8643Row* docRow)
        {
            const string doc8643Class = docRow ? docRow->aircraftClass : "";
            const string docWtc = docRow ? docRow->wakeTurbulenceCategory : "";

            auto profile = AircraftPerformanceProfileService::createSafeDefault(icao, doc8643Class, docWtc);
            profile.hasDoc8643Reference = (docRow != nullptr);

            if (!perfRow)
            {
                return profile;
            }

            profile.hasPerformanceData = true;

            string stringValue;
            int intValue;
            double doubleValue;

            if (tryParseString(perfRow->values, "takeOffWTC", stringValue))
            {
                profile.wakeTurbulenceCategory = upper(stringValue);
            }
            if (tryParseString(perfRow->values, "takeOffRECAT", stringValue))
            {
                profile.takeOffRecat = stringValue;
            }
            if (tryParseString(perfRow->values, "landingAPC", stringValue))
            {
                profile.landingApc = upper(stringValue);
            }

            if (tryParseInt(perfRow->values, "takeOffV2", intValue)) profile.takeOffV2Kt = intValue;
            if (tryParseInt(perfRow->values, "takeOffDistance", intValue)) profile.takeOffDistanceMeters = intValue;
            if (tryParseInt(perfRow->values, "takeOffMTOW", intValue)) profile.takeOffMtowKg = intValue;
            if (tryParseInt(perfRow->values, "initialClimbIAS", intValue)) profile.initialClimbIASKt = intValue;
            if (tryParseInt(perfRow->values, "initialClimbROC", intValue)) profile.initialClimbRocFpm = intValue;
            if (tryParseInt(perfRow->values, "climb150IAS", intValue)) profile.climb150IASKt = intValue;
            if (tryParseInt(perfRow->values, "climb150ROC", intValue)) profile.climb150RocFpm = intValue;
            if (tryParseInt(perfRow->values, "climb240IAS", intValue)) profile.climb240IASKt = intValue;
            if (tryParseInt(perfRow->values, "climb240ROC", intValue)) profile.climb240RocFpm = intValue;
            if (tryParseDouble(perfRow->values, "machClimbMACH", doubleValue)) profile.machClimbMach = doubleValue;
            if (tryParseInt(perfRow->values, "machClimbROC", intValue)) profile.machClimbRocFpm = intValue;
            if (tryParseInt(perfRow->values, "cruiseTAS", intValue)) profile.cruiseTasKt = intValue;
            if (tryParseDouble(perfRow->values, "cruiseMACH", doubleValue)) profile.cruiseMach = doubleValue;
            if (tryParseInt(perfRow->values, "cruiseCeiling", intValue)) profile.cruiseCeilingFl = intValue;
            if (tryParseInt(perfRow->values, "cruiseRange", intValue)) profile.cruiseRangeNm = intValue;
            if (tryParseDouble(perfRow->values, "initialDescentMACH", doubleValue)) profile.initialDescentMach = doubleValue;
            if (tryParseInt(perfRow->values, "initialDescentROD", intValue)) profile.initialDescentRodFpm = intValue;
            if (tryParseInt(perfRow->values, "descentIAS", intValue)) profile.descentIASKt = intValue;
            if (tryParseInt(perfRow->values, "descentROD", intValue)) profile.descentRodFpm = intValue;
            if (tryParseInt(perfRow->values, "approachIAS", intValue)) profile.approachIASKt = intValue;
            if (tryParseInt(perfRow->values, "approachROD", intValue)) profile.approachRodFpm = intValue;
            if (tryParseInt(perfRow->values, "approachMCS", intValue)) profile.approachMcsKt = intValue;
            if (tryParseInt(perfRow->values, "landingVat", intValue)) profile.landingVatKt = intValue;
            if (tryParseInt(perfRow->values, "landingDistance", intValue)) profile.landingDistanceMeters = intValue;

            profile.category = deriveCategory(profile.doc8643Class, profile.wakeTurbulenceCategory);
            applyCategoryDefaults(profile);

            if (tryParseInt(perfRow->values, "takeOffV2", intValue)) profile.takeOffV2Kt = intValue;
            if (tryParseInt(perfRow->values, "takeOffDistance", intValue)) profile.takeOffDistanceMeters = intValue;
            if (tryParseInt(perfRow->values, "takeOffMTOW", intValue)) profile.takeOffMtowKg = intValue;
            if (tryParseInt(perfRow->values, "initialClimbIAS", intValue)) profile.initialClimbIASKt = intValue;
            if (tryParseInt(perfRow->values, "initialClimbROC", intValue)) profile.initialClimbRocFpm = intValue;
            if (tryParseInt(perfRow->values, "climb150IAS", intValue)) profile.climb150IASKt = intValue;
            if (tryParseInt(perfRow->values, "climb150ROC", intValue)) profile.climb150RocFpm = intValue;
            if (tryParseInt(perfRow->values, "climb240IAS", intValue)) profile.climb240IASKt = intValue;
            if (tryParseInt(perfRow->values, "climb240ROC", intValue)) profile.climb240RocFpm = intValue;
            if (tryParseDouble(perfRow->values, "machClimbMACH", doubleValue)) profile.machClimbMach = doubleValue;
            if (tryParseInt(perfRow->values, "machClimbROC", intValue)) profile.machClimbRocFpm = intValue;
            if (tryParseInt(perfRow->values, "cruiseTAS", intValue)) profile.cruiseTasKt = intValue;
            if (tryParseDouble(perfRow->values, "cruiseMACH", doubleValue)) profile.cruiseMach = doubleValue;
            if (tryParseInt(perfRow->values, "cruiseCeiling", intValue)) profile.cruiseCeilingFl = intValue;
            if (tryParseInt(perfRow->values, "cruiseRange", intValue)) profile.cruiseRangeNm = intValue;
            if (tryParseDouble(perfRow->values, "initialDescentMACH", doubleValue)) profile.initialDescentMach = doubleValue;
            if (tryParseInt(perfRow->values, "initialDescentROD", intValue)) profile.initialDescentRodFpm = intValue;
            if (tryParseInt(perfRow->values, "descentIAS", intValue)) profile.descentIASKt = intValue;
            if (tryParseInt(perfRow->values, "descentROD", intValue)) profile.descentRodFpm = intValue;
            if (tryParseInt(perfRow->values, "approachIAS", intValue)) profile.approachIASKt = intValue;
            if (tryParseInt(perfRow->values, "approachROD", intValue)) profile.approachRodFpm = intValue;
            if (tryParseInt(perfRow->values, "approachMCS", intValue)) profile.approachMcsKt = intValue;
            if (tryParseInt(perfRow->values, "landingVat", intValue)) profile.landingVatKt = intValue;
            if (tryParseInt(perfRow->values, "landingDistance", intValue)) profile.landingDistanceMeters = intValue;

            return profile;
        }
    }

    AircraftPerformanceProfileService::AircraftPerformanceProfileService(shared_ptr<HostServices> host)
    {
        if (!host)
        {
            throw runtime_error("AircraftPerformanceProfileService requires host services");
        }

        auto performancePath = host->getResourceFilePath({ "Resources", "AircraftPerformance.csv" });
        auto docPath = host->getResourceFilePath({ "Resources", "Doc8643.txt" });
        auto performanceStream = host->openFileForRead(performancePath);
        auto docStream = host->openFileForRead(docPath);
        m_profiles = parseBundledProfiles(*performanceStream, *docStream);
        host->writeLog(
            "AIRPERF|Loaded [%d] aircraft performance profiles from bundled resources",
            m_profiles.size());
    }

    AircraftPerformanceProfileService::AircraftPerformanceProfileService(const unordered_map<string, Profile>& profiles) :
        m_profiles(profiles)
    {
    }

    const AircraftPerformanceProfileService::Profile& AircraftPerformanceProfileService::resolve(const string& icao) const
    {
        const string normalizedIcao = upper(trim(icao));
        auto it = m_profiles.find(normalizedIcao);
        if (it != m_profiles.end())
        {
            return it->second;
        }

        auto unknownIt = m_unknownProfiles.find(normalizedIcao);
        if (unknownIt == m_unknownProfiles.end())
        {
            unknownIt = m_unknownProfiles.insert({
                normalizedIcao,
                createSafeDefault(normalizedIcao)
            }).first;
        }

        return unknownIt->second;
    }

    unordered_map<string, AircraftPerformanceProfileService::Profile> AircraftPerformanceProfileService::parseBundledProfiles(
        istream& performanceCsv,
        istream& doc8643)
    {
        const auto performanceRows = parsePerformanceRows(performanceCsv);
        const auto docRows = parseDoc8643Rows(doc8643);

        set<string> allIcaos;
        for (const auto& entry : performanceRows)
        {
            allIcaos.insert(entry.first);
        }
        for (const auto& entry : docRows)
        {
            allIcaos.insert(entry.first);
        }

        unordered_map<string, Profile> profiles;
        for (const auto& icao : allIcaos)
        {
            const auto perfIt = performanceRows.find(icao);
            const auto docIt = docRows.find(icao);
            profiles.insert({
                icao,
                createProfile(
                    icao,
                    perfIt != performanceRows.end() ? &perfIt->second : nullptr,
                    docIt != docRows.end() ? &docIt->second : nullptr)
            });
        }

        return profiles;
    }

    AircraftPerformanceProfileService::Profile AircraftPerformanceProfileService::createSafeDefault(
        const string& icao,
        const string& doc8643Class,
        const string& wakeTurbulenceCategory)
    {
        Profile profile;
        profile.icao = upper(trim(icao));
        profile.doc8643Class = upper(trim(doc8643Class));
        profile.wakeTurbulenceCategory = upper(trim(wakeTurbulenceCategory));
        profile.category = deriveCategory(profile.doc8643Class, profile.wakeTurbulenceCategory);
        applyCategoryDefaults(profile);
        return profile;
    }
}
