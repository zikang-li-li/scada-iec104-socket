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
    std::cout << "Usage: " << program << " [--port 2404] [--interval-ms 1000] [--drop-every-sec 20]\n";
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

