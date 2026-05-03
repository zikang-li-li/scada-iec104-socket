#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace scada::model {

enum class PointType {
    Analog,
    Digital,
    Counter,
    Unknown
};

struct DataQuality {
    bool invalid = false;
    bool notTopical = false;
    bool substituted = false;
    bool blocked = false;
    bool overflow = false;
};

struct DataPointSnapshot {
    int address = 0;
    std::string name;
    PointType type = PointType::Unknown;
    std::string iecType;
    std::string unit;
    double value = 0.0;
    bool state = false;
    DataQuality quality;
    std::chrono::system_clock::time_point timestamp = std::chrono::system_clock::now();
};

enum class AlarmSeverity {
    Info,
    Warning,
    Critical
};

struct AlarmEvent {
    int address = 0;
    std::string pointName;
    std::string rule;
    AlarmSeverity severity = AlarmSeverity::Info;
    bool active = false;
    double value = 0.0;
    std::string message;
    std::chrono::system_clock::time_point timestamp = std::chrono::system_clock::now();
};

PointType pointTypeFromString(const std::string& value);
std::string pointTypeName(PointType type);
std::string severityName(AlarmSeverity severity);
std::string qualityText(const DataQuality& quality);

} // namespace scada::model

