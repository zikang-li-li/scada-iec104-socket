#include "scada/common/Config.h"

#include "scada/common/Logger.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace scada::common {
namespace {

std::string trim(std::string value) {
    const auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool startsWith(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0) == 0;
}

int toInt(const std::string& value, int fallback) {
    try {
        return std::stoi(value);
    } catch (const std::exception&) {
        return fallback;
    }
}

std::size_t toSize(const std::string& value, std::size_t fallback) {
    try {
        const auto parsed = std::stoull(value);
        return static_cast<std::size_t>(parsed);
    } catch (const std::exception&) {
        return fallback;
    }
}

double toDouble(const std::string& value, double fallback) {
    try {
        return std::stod(value);
    } catch (const std::exception&) {
        return fallback;
    }
}

bool toBool(const std::string& value, bool fallback) {
    const auto normalized = lower(trim(value));
    if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on") {
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off") {
        return false;
    }
    return fallback;
}

void applyClientAttribute(ClientOptions& client, const std::string& attribute, const std::string& value) {
    if (attribute == "host") {
        client.host = value;
    } else if (attribute == "port") {
        client.port = static_cast<std::uint16_t>(toInt(value, client.port));
    } else if (attribute == "connect_timeout_ms") {
        client.connectTimeoutMs = toInt(value, client.connectTimeoutMs);
    } else if (attribute == "receive_timeout_ms") {
        client.receiveTimeoutMs = toInt(value, client.receiveTimeoutMs);
    } else if (attribute == "reconnect_ms") {
        client.reconnectMs = toInt(value, client.reconnectMs);
    } else if (attribute == "heartbeat_interval_ms") {
        client.heartbeatIntervalMs = toInt(value, client.heartbeatIntervalMs);
    } else if (attribute == "heartbeat_timeout_ms") {
        client.heartbeatTimeoutMs = toInt(value, client.heartbeatTimeoutMs);
    } else if (attribute == "status_interval_ms") {
        client.statusIntervalMs = toInt(value, client.statusIntervalMs);
    }
}

void applyPointAttribute(PointDefinition& point, const std::string& attribute, const std::string& value) {
    if (attribute == "name") {
        point.name = value;
    } else if (attribute == "type") {
        point.type = value;
    } else if (attribute == "unit") {
        point.unit = value;
    } else if (attribute == "high_high") {
        point.highHigh = toDouble(value, 0.0);
    } else if (attribute == "high") {
        point.high = toDouble(value, 0.0);
    } else if (attribute == "low") {
        point.low = toDouble(value, 0.0);
    } else if (attribute == "low_low") {
        point.lowLow = toDouble(value, 0.0);
    } else if (attribute == "normal_state") {
        point.normalState = toInt(value, 0);
    } else if (attribute == "stale_seconds") {
        point.staleSeconds = toInt(value, point.staleSeconds);
    }
}

std::vector<PointDefinition> defaultPoints() {
    return {
        PointDefinition{1001, "Main transformer load", "analog", "MW", 95.0, 85.0, 10.0, 5.0, std::nullopt, 15},
        PointDefinition{1002, "Bus voltage", "analog", "kV", 235.0, 230.0, 205.0, 198.0, std::nullopt, 15},
        PointDefinition{2001, "Breaker QF1", "digital", "", std::nullopt, std::nullopt, std::nullopt, std::nullopt, 1, 20},
    };
}

DeviceDefinition makeDevice(const std::string& id, const ClientOptions& defaults) {
    DeviceDefinition device;
    device.id = id.empty() ? "rtu_main" : id;
    device.name = device.id;
    device.client = defaults;
    return device;
}

void normalizePointNames(std::vector<PointDefinition>& points) {
    for (auto& point : points) {
        if (point.name.empty()) {
            point.name = "IOA " + std::to_string(point.address);
        }
    }
}

std::vector<PointDefinition> pointMapToVector(std::map<int, PointDefinition>& points) {
    std::vector<PointDefinition> result;
    for (auto& item : points) {
        if (item.second.name.empty()) {
            item.second.name = "IOA " + std::to_string(item.first);
        }
        result.push_back(item.second);
    }
    return result;
}

void applyDeviceAttribute(
    std::map<std::string, DeviceDefinition>& devices,
    std::map<std::string, std::map<int, PointDefinition>>& devicePoints,
    const ClientOptions& globalClient,
    const std::string& rest,
    const std::string& value) {
    const auto dot = rest.find('.');
    if (dot == std::string::npos) {
        return;
    }

    const auto deviceId = rest.substr(0, dot);
    const auto attribute = rest.substr(dot + 1);
    if (deviceId.empty()) {
        return;
    }

    auto found = devices.find(deviceId);
    if (found == devices.end()) {
        found = devices.emplace(deviceId, makeDevice(deviceId, globalClient)).first;
    }

    auto& device = found->second;
    if (attribute == "name") {
        device.name = value;
    } else if (attribute == "enabled") {
        device.enabled = toBool(value, device.enabled);
    } else if (attribute == "common_address") {
        device.commonAddress = static_cast<std::uint16_t>(toInt(value, device.commonAddress));
    } else if (startsWith(attribute, "point.")) {
        const auto pointRest = attribute.substr(6);
        const auto pointDot = pointRest.find('.');
        if (pointDot == std::string::npos) {
            return;
        }

        const auto address = toInt(pointRest.substr(0, pointDot), 0);
        if (address == 0) {
            return;
        }

        auto& point = devicePoints[deviceId][address];
        point.address = address;
        applyPointAttribute(point, pointRest.substr(pointDot + 1), value);
    } else {
        applyClientAttribute(device.client, attribute, value);
    }
}

} // namespace

AppConfig Config::defaults() {
    AppConfig config;

    config.client.host = "127.0.0.1";
    config.client.port = 2404;
    config.client.connectTimeoutMs = 3000;
    config.client.receiveTimeoutMs = 5000;
    config.client.reconnectMs = 3000;
    config.client.heartbeatIntervalMs = 10000;
    config.client.heartbeatTimeoutMs = 3000;
    config.client.statusIntervalMs = 1000;
    config.cache.path = "data/cache.log";
    config.cache.maxRecords = 10000;
    config.log.path = "logs/scada_client.log";
    config.log.level = "info";
    config.log.console = true;
    config.log.append = true;
    config.points = defaultPoints();

    DeviceDefinition device = makeDevice("rtu_main", config.client);
    device.name = "Main RTU";
    device.commonAddress = 1;
    device.points = config.points;
    config.devices.push_back(device);

    return config;
}

AppConfig Config::load(const std::string& path) {
    const auto defaults = Config::defaults();
    AppConfig config;
    config.client = defaults.client;
    config.cache = defaults.cache;

    std::ifstream input(path);
    if (!input) {
        Logger::warn("Config file not found: " + path + ", using built-in defaults");
        return defaults;
    }

    std::map<int, PointDefinition> legacyPoints;
    std::map<std::string, DeviceDefinition> devices;
    std::map<std::string, std::map<int, PointDefinition>> devicePoints;
    bool sawLegacyPoint = false;
    bool sawDevice = false;

    std::string line;
    while (std::getline(input, line)) {
        const auto comment = line.find('#');
        if (comment != std::string::npos) {
            line = line.substr(0, comment);
        }

        line = trim(line);
        if (line.empty()) {
            continue;
        }

        const auto equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }

        const auto key = trim(line.substr(0, equals));
        const auto value = trim(line.substr(equals + 1));

        if (key == "cache_path") {
            config.cache.path = value;
        } else if (key == "cache_max_records") {
            config.cache.maxRecords = toSize(value, config.cache.maxRecords);
        } else if (key == "log_path") {
            config.log.path = value;
        } else if (key == "log_level") {
            config.log.level = value;
        } else if (key == "log_console") {
            config.log.console = toBool(value, config.log.console);
        } else if (key == "log_append") {
            config.log.append = toBool(value, config.log.append);
        } else if (startsWith(key, "device.")) {
            sawDevice = true;
            applyDeviceAttribute(devices, devicePoints, config.client, key.substr(7), value);
        } else if (startsWith(key, "point.")) {
            sawLegacyPoint = true;
            const auto rest = key.substr(6);
            const auto dot = rest.find('.');
            if (dot == std::string::npos) {
                continue;
            }

            const auto address = toInt(rest.substr(0, dot), 0);
            if (address == 0) {
                continue;
            }

            auto& point = legacyPoints[address];
            point.address = address;
            applyPointAttribute(point, rest.substr(dot + 1), value);
        } else {
            applyClientAttribute(config.client, key, value);
        }
    }

    if (sawLegacyPoint) {
        config.points = pointMapToVector(legacyPoints);
    } else {
        config.points = defaults.points;
    }

    for (auto& [deviceId, pointMap] : devicePoints) {
        auto found = devices.find(deviceId);
        if (found == devices.end()) {
            found = devices.emplace(deviceId, makeDevice(deviceId, config.client)).first;
        }
        found->second.points = pointMapToVector(pointMap);
    }

    if (!sawDevice) {
        DeviceDefinition device = makeDevice("rtu_main", config.client);
        device.name = "Main RTU";
        device.points = config.points;
        normalizePointNames(device.points);
        config.devices.push_back(device);
    } else {
        for (auto& [id, device] : devices) {
            if (device.name.empty()) {
                device.name = id;
            }
            normalizePointNames(device.points);
            config.devices.push_back(device);
        }
    }

    return config;
}

} // namespace scada::common
