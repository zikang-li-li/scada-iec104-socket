#include "scada/common/Config.h"

#include "scada/common/Logger.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace scada::common {
namespace {

std::string trim(std::string value) {
    const auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
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

} // namespace

AppConfig Config::defaults() {
    AppConfig config;

    config.client.host = "127.0.0.1";
    config.client.port = 2404;
    config.client.connectTimeoutMs = 3000;
    config.client.receiveTimeoutMs = 5000;
    config.client.reconnectMs = 2000;
    config.client.statusIntervalMs = 1000;
    config.cache.path = "data/cache.log";
    config.cache.maxRecords = 10000;

    config.points = {
        PointDefinition{1001, "Main transformer load", "analog", "MW", 95.0, 85.0, 10.0, 5.0, std::nullopt, 15},
        PointDefinition{1002, "Bus voltage", "analog", "kV", 235.0, 230.0, 205.0, 198.0, std::nullopt, 15},
        PointDefinition{2001, "Breaker QF1", "digital", "", std::nullopt, std::nullopt, std::nullopt, std::nullopt, 1, 20},
    };

    return config;
}

AppConfig Config::load(const std::string& path) {
    AppConfig config = defaults();
    std::ifstream input(path);
    if (!input) {
        Logger::warn("Config file not found: " + path + ", using built-in defaults");
        return config;
    }

    std::map<int, PointDefinition> points;
    for (const auto& point : config.points) {
        points[point.address] = point;
    }

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

        if (key == "host") {
            config.client.host = value;
        } else if (key == "port") {
            config.client.port = static_cast<std::uint16_t>(toInt(value, config.client.port));
        } else if (key == "connect_timeout_ms") {
            config.client.connectTimeoutMs = toInt(value, config.client.connectTimeoutMs);
        } else if (key == "receive_timeout_ms") {
            config.client.receiveTimeoutMs = toInt(value, config.client.receiveTimeoutMs);
        } else if (key == "reconnect_ms") {
            config.client.reconnectMs = toInt(value, config.client.reconnectMs);
        } else if (key == "status_interval_ms") {
            config.client.statusIntervalMs = toInt(value, config.client.statusIntervalMs);
        } else if (key == "cache_path") {
            config.cache.path = value;
        } else if (key == "cache_max_records") {
            config.cache.maxRecords = toSize(value, config.cache.maxRecords);
        } else if (startsWith(key, "point.")) {
            const auto rest = key.substr(6);
            const auto dot = rest.find('.');
            if (dot == std::string::npos) {
                continue;
            }

            const auto address = toInt(rest.substr(0, dot), 0);
            if (address == 0) {
                continue;
            }

            auto& point = points[address];
            point.address = address;
            applyPointAttribute(point, rest.substr(dot + 1), value);
        }
    }

    config.points.clear();
    for (auto& item : points) {
        if (item.second.name.empty()) {
            item.second.name = "IOA " + std::to_string(item.first);
        }
        config.points.push_back(item.second);
    }

    return config;
}

} // namespace scada::common

