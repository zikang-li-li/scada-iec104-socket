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
    int reconnectMs = 2000;
    int statusIntervalMs = 1000;
};

struct CacheOptions {
    std::string path = "data/cache.log";
    std::size_t maxRecords = 10000;
};

struct AppConfig {
    ClientOptions client;
    CacheOptions cache;
    std::vector<PointDefinition> points;
};

class Config {
public:
    static AppConfig defaults();
    static AppConfig load(const std::string& path);
};

} // namespace scada::common

