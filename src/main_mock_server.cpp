#include "scada/common/Logger.h"
#include "scada/mock/MockIec104Server.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>

namespace {

std::atomic<bool> stopRequested{false};

void handleSignal(int) {
    stopRequested.store(true);
}

void printUsage(const char* program) {
    std::cout
        << "Usage: " << program
        << " [--port 2404] [--interval-ms 1000] [--drop-every-sec 20]\n"
        << "       [--scenario mixed|normal|alarm|quality|stale|digital-trip]\n"
        << "       [--common-address 1] [--load-ioa 1001] [--voltage-ioa 1002]\n"
        << "       [--breaker-ioa 2001] [--quality-every 5] [--quiet-after-sec 8]\n"
        << "       [--log-file logs/mock_server.log] [--debug]\n";
}

int toInt(const std::string& value, int fallback) {
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

} // namespace

int main(int argc, char** argv) {
    scada::mock::MockServerOptions options;
    std::string logFile;
    bool debug = false;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            printUsage(argv[0]);
            return 0;
        }

        if (argument == "--port" && index + 1 < argc) {
            options.port = static_cast<std::uint16_t>(toInt(argv[++index], options.port));
        } else if (argument == "--interval-ms" && index + 1 < argc) {
            options.sendIntervalMs = toInt(argv[++index], options.sendIntervalMs);
        } else if (argument == "--drop-every-sec" && index + 1 < argc) {
            options.dropEverySeconds = toInt(argv[++index], options.dropEverySeconds);
        } else if (argument == "--scenario" && index + 1 < argc) {
            options.scenario = scada::mock::scenarioFromString(argv[++index], options.scenario);
        } else if (argument == "--common-address" && index + 1 < argc) {
            options.commonAddress = static_cast<std::uint16_t>(toInt(argv[++index], options.commonAddress));
        } else if (argument == "--load-ioa" && index + 1 < argc) {
            options.loadIoa = toInt(argv[++index], options.loadIoa);
        } else if (argument == "--voltage-ioa" && index + 1 < argc) {
            options.voltageIoa = toInt(argv[++index], options.voltageIoa);
        } else if (argument == "--breaker-ioa" && index + 1 < argc) {
            options.breakerIoa = toInt(argv[++index], options.breakerIoa);
        } else if (argument == "--quality-every" && index + 1 < argc) {
            options.qualityEvery = toInt(argv[++index], options.qualityEvery);
        } else if (argument == "--quiet-after-sec" && index + 1 < argc) {
            options.quietAfterSeconds = toInt(argv[++index], options.quietAfterSeconds);
        } else if (argument == "--log-file" && index + 1 < argc) {
            logFile = argv[++index];
        } else if (argument == "--debug") {
            debug = true;
        }
    }

    if (debug) {
        scada::common::Logger::setLevel(scada::common::LogLevel::Debug);
    }
    if (!logFile.empty()) {
        if (scada::common::Logger::setOutputFile(logFile, true)) {
            scada::common::Logger::info("Log file enabled: " + logFile);
        } else {
            scada::common::Logger::error("Failed to open log file: " + logFile);
        }
    }

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    scada::mock::MockIec104Server server(options);
    server.start();

    scada::common::Logger::info("Mock IEC104 server started. Press Ctrl+C to stop.");
    while (!stopRequested.load() && server.running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    scada::common::Logger::info("Stopping mock IEC104 server");
    server.stop();
    return 0;
}
