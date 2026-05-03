#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

struct LatestMeasure {
    std::string timestamp;
    std::string deviceId;
    std::string deviceName;
    std::string address;
    std::string name;
    std::string type;
    std::string iecType;
    std::string value;
    std::string state;
    std::string unit;
    std::string quality;
};

struct ActiveAlarm {
    std::string timestamp;
    std::string deviceId;
    std::string deviceName;
    std::string address;
    std::string pointName;
    std::string rule;
    std::string severity;
    std::string value;
    std::string message;
};

struct Report {
    int records = 0;
    int measurements = 0;
    int alarms = 0;
    int alarmsRaised = 0;
    int alarmsCleared = 0;
    std::map<std::string, LatestMeasure> latestMeasures;
    std::map<std::string, ActiveAlarm> activeAlarms;
};

std::vector<std::string> split(const std::string& line, char delimiter) {
    std::vector<std::string> fields;
    std::string current;
    for (char ch : line) {
        if (ch == delimiter) {
            fields.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    fields.push_back(current);
    return fields;
}

int toInt(const std::string& value, int fallback) {
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [--cache data/cache.log] [--limit 20]\n";
}

} // namespace

int main(int argc, char** argv) {
    std::string path = "data/cache.log";
    int limit = 20;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            printUsage(argv[0]);
            return 0;
        }
        if (argument == "--cache" && index + 1 < argc) {
            path = argv[++index];
        } else if (argument == "--limit" && index + 1 < argc) {
            limit = toInt(argv[++index], limit);
        }
    }

    std::ifstream input(path);
    if (!input) {
        std::cerr << "Cannot open cache file: " << path << "\n";
        return 1;
    }

    Report report;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }

        const auto fields = split(line, '|');
        if (fields.size() < 3) {
            continue;
        }

        ++report.records;
        const auto& kind = fields[1];
        if (kind == "MEASURE" && fields.size() >= 12) {
            ++report.measurements;
            const std::string key = fields[2] + ":" + fields[4];
            report.latestMeasures[key] = LatestMeasure{
                fields[0],
                fields[2],
                fields[3],
                fields[4],
                fields[5],
                fields[6],
                fields[7],
                fields[8],
                fields[9],
                fields[10],
                fields[11],
            };
        } else if (kind == "MEASURE" && fields.size() >= 10) {
            ++report.measurements;
            const std::string key = "legacy:" + fields[2];
            report.latestMeasures[key] = LatestMeasure{
                fields[0],
                "legacy",
                "legacy",
                fields[2],
                fields[3],
                fields[4],
                fields[5],
                fields[6],
                fields[7],
                fields[8],
                fields[9],
            };
        } else if (kind == "ALARM" && fields.size() >= 11) {
            ++report.alarms;
            const std::string key = fields[2] + ":" + fields[4] + ":" + fields[6];
            if (fields[8] == "RAISED") {
                ++report.alarmsRaised;
                report.activeAlarms[key] = ActiveAlarm{
                    fields[0],
                    fields[2],
                    fields[3],
                    fields[4],
                    fields[5],
                    fields[6],
                    fields[7],
                    fields[9],
                    fields[10],
                };
            } else if (fields[8] == "CLEARED") {
                ++report.alarmsCleared;
                report.activeAlarms.erase(key);
            }
        } else if (kind == "ALARM" && fields.size() >= 9) {
            ++report.alarms;
            const std::string key = "legacy:" + fields[2] + ":" + fields[4];
            if (fields[6] == "RAISED") {
                ++report.alarmsRaised;
                report.activeAlarms[key] = ActiveAlarm{
                    fields[0],
                    "legacy",
                    "legacy",
                    fields[2],
                    fields[3],
                    fields[4],
                    fields[5],
                    fields[7],
                    fields[8],
                };
            } else if (fields[6] == "CLEARED") {
                ++report.alarmsCleared;
                report.activeAlarms.erase(key);
            }
        }
    }

    std::cout << "Cache: " << path << "\n"
              << "records=" << report.records
              << ", measurements=" << report.measurements
              << ", alarms=" << report.alarms
              << ", raised=" << report.alarmsRaised
              << ", cleared=" << report.alarmsCleared
              << ", active_alarms=" << report.activeAlarms.size() << "\n\n";

    std::cout << "Latest measurements:\n";
    int printed = 0;
    for (const auto& [key, measure] : report.latestMeasures) {
        (void)key;
        if (limit >= 0 && printed >= limit) {
            break;
        }
        std::cout << "  [" << measure.deviceId << "] "
                  << "IOA=" << measure.address
                  << " " << measure.name
                  << " value=" << measure.value;
        if (!measure.unit.empty()) {
            std::cout << " " << measure.unit;
        }
        std::cout << " type=" << measure.type
                  << " iec=" << measure.iecType
                  << " quality=" << measure.quality
                  << " at " << measure.timestamp << "\n";
        ++printed;
    }

    std::cout << "\nActive alarms:\n";
    if (report.activeAlarms.empty()) {
        std::cout << "  none\n";
    } else {
        for (const auto& [key, alarm] : report.activeAlarms) {
            (void)key;
            std::cout << "  " << alarm.timestamp
                      << " [" << alarm.severity << "] "
                      << "[" << alarm.deviceId << "] "
                      << alarm.pointName
                      << " " << alarm.rule
                      << " value=" << alarm.value
                      << " - " << alarm.message << "\n";
        }
    }

    return 0;
}
