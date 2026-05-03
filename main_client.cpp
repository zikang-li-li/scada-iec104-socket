#include "scada/common/Config.h"
#include "scada/common/Logger.h"
#include "scada/scada/ScadaSystem.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>

namespace {

std::atomic<bool> stopRequested{false};

void handleSignal(int) {
    stopRequested.store(true);
}

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [--config config/scada.conf]\n";
}

} // namespace

int main(int argc, char** argv) {
    std::string configPath = "config/scada.conf";

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            printUsage(argv[0]);
            return 0;
        }
        if ((argument == "--config" || argument == "-c") && index + 1 < argc) {
            configPath = argv[++index];
        } else if (argument.rfind("--config=", 0) == 0) {
            configPath = argument.substr(9);
        }
    }

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    const auto config = scada::common::Config::load(configPath);
    scada::scada::ScadaSystem system(config);
    system.start();

    scada::common::Logger::info("SCADA client started. Press Ctrl+C to stop.");
    while (!stopRequested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    scada::common::Logger::info("Stopping SCADA client");
    system.stop();
    return 0;
}

