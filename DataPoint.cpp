#include "scada/scada/DataPoint.h"

#include <algorithm>
#include <sstream>

namespace scada::model {
namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

} // namespace

PointType pointTypeFromString(const std::string& value) {
    const auto normalized = lower(value);
    if (normalized == "analog") {
        return PointType::Analog;
    }
    if (normalized == "digital") {
        return PointType::Digital;
    }
    if (normalized == "counter") {
        return PointType::Counter;
    }
    return PointType::Unknown;
}

std::string pointTypeName(PointType type) {
    switch (type) {
    case PointType::Analog:
        return "analog";
    case PointType::Digital:
        return "digital";
    case PointType::Counter:
        return "counter";
    case PointType::Unknown:
        return "unknown";
    }
    return "unknown";
}

std::string severityName(AlarmSeverity severity) {
    switch (severity) {
    case AlarmSeverity::Info:
        return "INFO";
    case AlarmSeverity::Warning:
        return "WARNING";
    case AlarmSeverity::Critical:
        return "CRITICAL";
    }
    return "INFO";
}

std::string qualityText(const DataQuality& quality) {
    std::ostringstream stream;
    bool first = true;

    const auto add = [&](const char* text) {
        if (!first) {
            stream << ",";
        }
        first = false;
        stream << text;
    };

    if (quality.invalid) {
        add("invalid");
    }
    if (quality.notTopical) {
        add("not_topical");
    }
    if (quality.substituted) {
        add("substituted");
    }
    if (quality.blocked) {
        add("blocked");
    }
    if (quality.overflow) {
        add("overflow");
    }

    return first ? "good" : stream.str();
}

} // namespace scada::model

