#include "scada/scada/ScadaSystem.h"

#include "scada/common/Logger.h"

#include <chrono>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <utility>

namespace scada::scada {
namespace {

iec104::Iec104ClientOptions toClientOptions(const common::DeviceDefinition& device) {
    iec104::Iec104ClientOptions options;
    options.host = device.client.host;
    options.port = device.client.port;
    options.connectTimeoutMs = device.client.connectTimeoutMs;
    options.receiveTimeoutMs = device.client.receiveTimeoutMs;
    options.reconnectMs = device.client.reconnectMs;
    options.heartbeatIntervalMs = device.client.heartbeatIntervalMs;
    options.heartbeatTimeoutMs = device.client.heartbeatTimeoutMs;
    return options;
}

std::string deviceLabel(const common::DeviceDefinition& device) {
    return device.id + "(" + device.name + ")";
}

std::string formatAnalogValue(const model::DataPointSnapshot& snapshot) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(2) << snapshot.value;
    if (!snapshot.unit.empty()) {
        stream << " " << snapshot.unit;
    }
    return stream.str();
}

std::string formatBusinessType(model::BusinessDataType type) {
    switch (type) {
    case model::BusinessDataType::Telemetry:
        return "Telemetry";
    case model::BusinessDataType::Telesignal:
        return "Telesignal";
    case model::BusinessDataType::Telecontrol:
        return "Telecontrol";
    case model::BusinessDataType::Unknown:
        return "Unknown";
    }
    return "Unknown";
}

void attachDevice(model::AlarmEvent& event, const common::DeviceDefinition& device) {
    event.deviceId = device.id;
    event.deviceName = device.name;
}

void attachDevice(std::vector<model::AlarmEvent>& events, const common::DeviceDefinition& device) {
    for (auto& event : events) {
        attachDevice(event, device);
    }
}

void logAlarm(const model::AlarmEvent& event) {
    const std::string state = event.active ? "RAISED" : "CLEARED";
    const std::string message = "Alarm " + state + " [" + model::severityName(event.severity) + "] " +
                                "[" + event.deviceId + "] " + event.pointName + " " +
                                event.rule + " - " + event.message;
    if (event.severity == model::AlarmSeverity::Critical) {
        common::Logger::error(message);
    } else if (event.severity == model::AlarmSeverity::Warning) {
        common::Logger::warn(message);
    } else {
        common::Logger::info(message);
    }
}

} // namespace

struct ScadaSystem::DeviceContext {
    explicit DeviceContext(common::DeviceDefinition definition)
        : definition(std::move(definition)),
          client(toClientOptions(this->definition)) {}

    common::DeviceDefinition definition;
    iec104::Iec104Client client;
    AlarmManager alarms;
    std::atomic<bool> connected{false};
};

ScadaSystem::ScadaSystem(common::AppConfig config)
    : config_(std::move(config)) {
    cache_.configure(config_.cache.path, config_.cache.maxRecords);

    for (const auto& device : config_.devices) {
        if (!device.enabled) {
            common::Logger::warn("Device disabled by config: " + deviceLabel(device));
            continue;
        }
        devices_.push_back(std::make_unique<DeviceContext>(device));
    }

    if (devices_.empty()) {
        common::Logger::warn("No enabled IEC104 devices configured");
    }
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
    common::Logger::info("Configured devices: " + std::to_string(devices_.size()) +
                         ", configured points: " + std::to_string(pointCount(devices_)));

    for (auto& contextPointer : devices_) {
        auto* context = contextPointer.get();
        common::Logger::info("Device " + deviceLabel(context->definition) +
                             " endpoint=" + context->definition.client.host + ":" +
                             std::to_string(context->definition.client.port) +
                             " points=" + std::to_string(context->definition.points.size()));

        context->client.setConnectionHandler([this, context](bool connected) {
            onConnectionChanged(*context, connected);
        });
        context->client.setDataHandler([this, context](const iec104::Iec104Object& object) {
            onDataObject(*context, object);
        });
    }

    monitor_ = std::thread(&ScadaSystem::monitorLoop, this);
    for (auto& contextPointer : devices_) {
        contextPointer->client.start();
    }
}

void ScadaSystem::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    for (auto& contextPointer : devices_) {
        contextPointer->client.stop();
    }
    if (monitor_.joinable()) {
        monitor_.join();
    }
}

void ScadaSystem::onDataObject(DeviceContext& context, const iec104::Iec104Object& object) {
    auto definition = pointDefinitionFor(context, object.address);
    auto type = model::pointTypeFromString(definition.type);
    if (type == model::PointType::Unknown) {
        type = object.iecType == "M_SP_NA_1" ? model::PointType::Digital : model::PointType::Analog;
    }

    model::DataPointSnapshot snapshot;
    snapshot.deviceId = context.definition.id;
    snapshot.deviceName = context.definition.name;
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
    auto businessRecord = buildBusinessData(definition, snapshot);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshots_[snapshotKey(context.definition.id, snapshot.address)] = snapshot;
        events = context.alarms.evaluate(definition, snapshot);
        attachDevice(events, context.definition);
    }

    cache_.appendBusinessData(businessRecord);
    cache_.appendMeasurement(snapshot);
    logBusinessData(businessRecord);

    for (const auto& event : events) {
        cache_.appendAlarm(event);
        logAlarm(event);
    }
}

model::BusinessDataRecord ScadaSystem::buildBusinessData(
    const common::PointDefinition& definition,
    const model::DataPointSnapshot& snapshot) const {
    model::BusinessDataRecord record;
    record.businessType = model::businessTypeFromPointType(snapshot.type);
    record.snapshot = snapshot;

    if (record.businessType == model::BusinessDataType::Telemetry) {
        record.displayValue = formatAnalogValue(snapshot);
    } else if (record.businessType == model::BusinessDataType::Telesignal) {
        record.displayValue = snapshot.state ? "CLOSED" : "OPEN";
    } else if (record.businessType == model::BusinessDataType::Telecontrol) {
        record.displayValue = snapshot.state ? "CLOSE_COMMAND" : "OPEN_COMMAND";
    } else {
        record.displayValue = definition.type + ":" + formatAnalogValue(snapshot);
    }

    return record;
}

void ScadaSystem::logBusinessData(const model::BusinessDataRecord& record) const {
    std::ostringstream stream;
    stream << formatBusinessType(record.businessType)
           << " [" << record.snapshot.deviceName << "] "
           << record.snapshot.name << "=" << record.displayValue
           << " IOA=" << record.snapshot.address
           << " quality=" << model::qualityText(record.snapshot.quality);
    common::Logger::info(stream.str());
}

void ScadaSystem::onConnectionChanged(DeviceContext& context, bool connected) {
    context.connected.store(connected);
    if (connected) {
        common::Logger::info("IEC104 link connected: " + deviceLabel(context.definition) +
                             "; cache records=" + std::to_string(cache_.recordCount()));
    } else {
        common::Logger::warn("IEC104 link offline: " + deviceLabel(context.definition) +
                             "; local cache remains active at " + cache_.path());
    }
}

void ScadaSystem::monitorLoop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.client.statusIntervalMs));
        const auto now = std::chrono::system_clock::now();

        std::vector<model::AlarmEvent> events;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& contextPointer : devices_) {
                auto& context = *contextPointer;
                for (const auto& definition : context.definition.points) {
                    bool stale = true;
                    const auto found = snapshots_.find(snapshotKey(context.definition.id, definition.address));
                    if (found != snapshots_.end()) {
                        const auto age = std::chrono::duration_cast<std::chrono::seconds>(now - found->second.timestamp).count();
                        stale = age > definition.staleSeconds;
                    }

                    auto generated = context.alarms.evaluateStale(definition, stale);
                    attachDevice(generated, context.definition);
                    events.insert(events.end(), std::make_move_iterator(generated.begin()), std::make_move_iterator(generated.end()));
                }
            }
        }

        for (const auto& event : events) {
            cache_.appendAlarm(event);
            logAlarm(event);
        }
    }
}

common::PointDefinition ScadaSystem::pointDefinitionFor(const DeviceContext& context, int address) const {
    for (const auto& point : context.definition.points) {
        if (point.address == address) {
            return point;
        }
    }

    common::PointDefinition fallback;
    fallback.address = address;
    fallback.name = context.definition.id + " IOA " + std::to_string(address);
    fallback.type = "unknown";
    return fallback;
}

std::string ScadaSystem::snapshotKey(const std::string& deviceId, int address) {
    return deviceId + ":" + std::to_string(address);
}

std::size_t ScadaSystem::pointCount(const std::vector<std::unique_ptr<DeviceContext>>& devices) {
    std::size_t count = 0;
    for (const auto& contextPointer : devices) {
        count += contextPointer->definition.points.size();
    }
    return count;
}

} // namespace scada::scada
