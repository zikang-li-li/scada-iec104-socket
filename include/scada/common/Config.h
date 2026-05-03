#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace scada::common {

struct PointDefinition {
    int address = 0;
    std::string name;
    std::string type = "analog";
    std::string unit;
    std::optional<double> highHigh;
    std::optional<double> high;
    std::optional<double> low;
    std::optional<double> lowLow;
    std::optional<int> normalState;
    int staleSeconds = 30;
};

struct ClientOptions {
    std::string host = "127.0.0.1";
    std::uint16_t port = 2404;
    int connectTimeoutMs = 3000;
    int receiveTimeoutMs = 5000;
    int reconnectMs = 3000;
    int heartbeatIntervalMs = 10000;
    int heartbeatTimeoutMs = 3000;
    int statusIntervalMs = 1000;
};

struct DeviceDefinition {
    std::string id = "rtu_main";
    std::string name = "Main RTU";
    bool enabled = true;
    std::uint16_t commonAddress = 1;
    ClientOptions client;
    std::vector<PointDefinition> points;
};

struct CacheOptions {
    std::string path = "data/cache.log";
    std::size_t maxRecords = 10000;
};

struct LogOptions {
    std::string path = "logs/scada_client.log";
    std::string level = "info";
    bool console = true;
    bool append = true;
};

struct AppConfig {
    ClientOptions client;
    CacheOptions cache;
    LogOptions log;
    std::vector<DeviceDefinition> devices;
    std::vector<PointDefinition> points;
};

class Config {
public:
    static AppConfig defaults();
    static AppConfig load(const std::string& path);
};

} // namespace scada::common
