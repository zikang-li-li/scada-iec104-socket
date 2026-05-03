#include "scada/scada/ScadaSystem.h"

#include "scada/common/Logger.h"

#include <chrono>
#include <iomanip>
#include <sstream>
#include <utility>

namespace scada::scada {
namespace {

iec104::Iec104ClientOptions toClientOptions(const common::AppConfig& config) {
    iec104::Iec104ClientOptions options;
    options.host = config.client.host;
    options.port = config.client.port;
    options.connectTimeoutMs = config.client.connectTimeoutMs;
    options.receiveTimeoutMs = config.client.receiveTimeoutMs;
    options.reconnectMs = config.client.reconnectMs;
    return options;
}

std::string formatMeasurement(const model::DataPointSnapshot& snapshot) {
    std::ostringstream stream;
    stream << snapshot.name
           << " IOA=" << snapshot.address
           << " value=" << std::fixed << std::setprecision(2) << snapshot.value;
    if (!snapshot.unit.empty()) {
        stream << " " << snapshot.unit;
    }
    stream << " quality=" << model::qualityText(snapshot.quality);
    return stream.str();
}

void logAlarm(const model::AlarmEvent& event) {
    const std::string state = event.active ? "RAISED" : "CLEARED";
    const std::string message = "Alarm " + state + " [" + model::severityName(event.severity) + "] " +
                                event.pointName + " " + event.rule + " - " + event.message;
    if (event.severity == model::AlarmSeverity::Critical) {
        common::Logger::error(message);
    } else if (event.severity == model::AlarmSeverity::Warning) {
        common::Logger::warn(message);
    } else {
        common::Logger::info(message);
    }
}

} // namespace

ScadaSystem::ScadaSystem(common::AppConfig config)
    : config_(std::move(config)),
      client_(toClientOptions(config_)) {
    cache_.configure(config_.cache.path, config_.cache.maxRecords);
}

ScadaSystem::~ScadaSystem() {
    stop();
}

void ScadaSystem::start() {
    if (running_.exchange(true)) {
        return;
    }

    cache_.configure(config_.cache.path, config_.cache.maxRecords);
    common::Logger::info("SCADA cache file: " + cache_.path());
    common::Logger::info("Configured points: " + std::to_string(config_.points.size()));

    client_.setConnectionHandler([this](bool connected) {
        onConnectionChanged(connected);
    });
    client_.setDataHandler([this](const iec104::Iec104Object& object) {
        onDataObject(object);
    });

    monitor_ = std::thread(&ScadaSystem::monitorLoop, this);
    client_.start();
}

void ScadaSystem::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    client_.stop();
    if (monitor_.joinable()) {
        monitor_.join();
    }
}

void ScadaSystem::onDataObject(const iec104::Iec104Object& object) {
    auto definition = pointDefinitionFor(object.address);
    auto type = model::pointTypeFromString(definition.type);
    if (type == model::PointType::Unknown) {
        type = object.iecType == "M_SP_NA_1" ? model::PointType::Digital : model::PointType::Analog;
    }

    model::DataPointSnapshot snapshot;
    snapshot.address = object.address;
    snapshot.name = definition.name.empty() ? ("IOA " + std::to_string(object.address)) : definition.name;
    snapshot.type = type;
    snapshot.iecType = object.iecType;
    snapshot.unit = definition.unit;
    snapshot.value = object.value;
    snapshot.state = object.state;
    snapshot.quality = object.quality;
    snapshot.timestamp = std::chrono::system_clock::now();

    std::vector<model::AlarmEvent> events;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshots_[snapshot.address] = snapshot;
        events = alarms_.evaluate(definition, snapshot);
    }

    cache_.appendMeasurement(snapshot);
    common::Logger::info("Measure " + formatMeasurement(snapshot));

    for (const auto& event : events) {
        cache_.appendAlarm(event);
        logAlarm(event);
    }
}

void ScadaSystem::onConnectionChanged(bool connected) {
    connected_.store(connected);
    if (connected) {
        common::Logger::info("IEC104 link connected; cache records=" + std::to_string(cache_.recordCount()));
    } else {
        common::Logger::warn("IEC104 link offline; local cache remains active at " + cache_.path());
    }
}

void ScadaSystem::monitorLoop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.client.statusIntervalMs));
        const auto now = std::chrono::system_clock::now();

        std::vector<model::AlarmEvent> events;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& definition : config_.points) {
                bool stale = true;
                const auto found = snapshots_.find(definition.address);
                if (found != snapshots_.end()) {
                    const auto age = std::chrono::duration_cast<std::chrono::seconds>(now - found->second.timestamp).count();
                    stale = age > definition.staleSeconds;
                }

                auto generated = alarms_.evaluateStale(definition, stale);
                events.insert(events.end(), std::make_move_iterator(generated.begin()), std::make_move_iterator(generated.end()));
            }
        }

        for (const auto& event : events) {
            cache_.appendAlarm(event);
            logAlarm(event);
        }
    }
}

common::PointDefinition ScadaSystem::pointDefinitionFor(int address) const {
    for (const auto& point : config_.points) {
        if (point.address == address) {
            return point;
        }
    }

    common::PointDefinition fallback;
    fallback.address = address;
    fallback.name = "IOA " + std::to_string(address);
    fallback.type = "unknown";
    return fallback;
}

} // namespace scada::scada

