#include "scada/scada/AlarmManager.h"

#include <sstream>

namespace scada::scada {
namespace {

template <typename T>
void appendAll(std::vector<T>& target, std::vector<T> source) {
    target.insert(target.end(), std::make_move_iterator(source.begin()), std::make_move_iterator(source.end()));
}

std::string valueMessage(const std::string& label, double value, double limit) {
    std::ostringstream stream;
    stream << label << ": value=" << value << ", limit=" << limit;
    return stream.str();
}

} // namespace

std::vector<model::AlarmEvent> AlarmManager::evaluate(
    const common::PointDefinition& definition,
    const model::DataPointSnapshot& snapshot) {
    std::vector<model::AlarmEvent> events;

    const bool badQuality = snapshot.quality.invalid || snapshot.quality.blocked;
    appendAll(events, transition(
        definition,
        "DATA_QUALITY",
        badQuality,
        model::AlarmSeverity::Critical,
        snapshot.value,
        "Bad data quality: " + model::qualityText(snapshot.quality)));

    const auto type = model::pointTypeFromString(definition.type);
    if (type == model::PointType::Analog || type == model::PointType::Counter) {
        const bool highHigh = definition.highHigh && snapshot.value >= *definition.highHigh;
        const bool high = definition.high && snapshot.value >= *definition.high && !highHigh;
        const bool lowLow = definition.lowLow && snapshot.value <= *definition.lowLow;
        const bool low = definition.low && snapshot.value <= *definition.low && !lowLow;

        appendAll(events, transition(
            definition,
            "HIGH_HIGH",
            highHigh,
            model::AlarmSeverity::Critical,
            snapshot.value,
            definition.highHigh ? valueMessage("High-high limit exceeded", snapshot.value, *definition.highHigh) : ""));
        appendAll(events, transition(
            definition,
            "HIGH",
            high,
            model::AlarmSeverity::Warning,
            snapshot.value,
            definition.high ? valueMessage("High limit exceeded", snapshot.value, *definition.high) : ""));
        appendAll(events, transition(
            definition,
            "LOW_LOW",
            lowLow,
            model::AlarmSeverity::Critical,
            snapshot.value,
            definition.lowLow ? valueMessage("Low-low limit exceeded", snapshot.value, *definition.lowLow) : ""));
        appendAll(events, transition(
            definition,
            "LOW",
            low,
            model::AlarmSeverity::Warning,
            snapshot.value,
            definition.low ? valueMessage("Low limit exceeded", snapshot.value, *definition.low) : ""));
    }

    if (type == model::PointType::Digital && definition.normalState) {
        const bool abnormal = static_cast<int>(snapshot.state ? 1 : 0) != *definition.normalState;
        std::ostringstream message;
        message << "Digital state abnormal: value=" << (snapshot.state ? 1 : 0)
                << ", expected=" << *definition.normalState;
        appendAll(events, transition(
            definition,
            "STATE",
            abnormal,
            model::AlarmSeverity::Warning,
            snapshot.value,
            message.str()));
    }

    appendAll(events, evaluateStale(definition, false));
    return events;
}

std::vector<model::AlarmEvent> AlarmManager::evaluateStale(
    const common::PointDefinition& definition,
    bool stale) {
    return transition(
        definition,
        "STALE",
        stale,
        model::AlarmSeverity::Warning,
        0.0,
        "No fresh data within stale window");
}

std::vector<model::AlarmEvent> AlarmManager::transition(
    const common::PointDefinition& definition,
    const std::string& rule,
    bool active,
    model::AlarmSeverity severity,
    double value,
    const std::string& message) {
    const auto key = std::to_string(definition.address) + ":" + rule;
    const auto previous = activeRules_.find(key);
    const bool wasActive = previous != activeRules_.end() && previous->second;
    if (wasActive == active) {
        return {};
    }

    activeRules_[key] = active;

    model::AlarmEvent event;
    event.address = definition.address;
    event.pointName = definition.name.empty() ? ("IOA " + std::to_string(definition.address)) : definition.name;
    event.rule = rule;
    event.severity = severity;
    event.active = active;
    event.value = value;
    event.message = active ? message : ("Recovered: " + rule);
    event.timestamp = std::chrono::system_clock::now();
    return {event};
}

} // namespace scada::scada

