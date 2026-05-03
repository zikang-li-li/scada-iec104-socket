#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace scada::model {

enum class PointType {
    Analog,
    Digital,
    Counter,
    Control,
    Unknown
};

enum class BusinessDataType {
    Telemetry,
    Telesignal,
    Telecontrol,
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
    std::string deviceId;
    std::string deviceName;
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

struct BusinessDataRecord {
    BusinessDataType businessType = BusinessDataType::Unknown;
    DataPointSnapshot snapshot;
    std::string displayValue;
};

enum class AlarmSeverity {
    Info,
    Warning,
    Critical
};

struct AlarmEvent {
    std::string deviceId;
    std::string deviceName;
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
BusinessDataType businessTypeFromPointType(PointType type);
std::string businessTypeName(BusinessDataType type);
std::string severityName(AlarmSeverity severity);
std::string qualityText(const DataQuality& quality);

} // namespace scada::model
