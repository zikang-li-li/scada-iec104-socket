#pragma once

#include "scada/net/SocketCompat.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace scada::mock {

enum class MockScenario {
    Normal,
    Alarm,
    Quality,
    Stale,
    DigitalTrip,
    Mixed
};

struct MockServerOptions {
    std::uint16_t port = 2404;
    int sendIntervalMs = 1000;
    int dropEverySeconds = 20;
    MockScenario scenario = MockScenario::Mixed;
    std::uint16_t commonAddress = 1;
    int loadIoa = 1001;
    int voltageIoa = 1002;
    int breakerIoa = 2001;
    int qualityEvery = 5;
    int quietAfterSeconds = 8;
};

MockScenario scenarioFromString(const std::string& value, MockScenario fallback);
std::string scenarioName(MockScenario scenario);

class MockIec104Server {
public:
    explicit MockIec104Server(MockServerOptions options);
    ~MockIec104Server();

    void start();
    void stop();
    bool running() const;

private:
    void run();
    void serveClient(scada::net::SocketHandle client);

    MockServerOptions options_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

} // namespace scada::mock
