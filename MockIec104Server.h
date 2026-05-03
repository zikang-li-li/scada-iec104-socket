#pragma once

#include "scada/net/SocketCompat.h"

#include <atomic>
#include <cstdint>
#include <thread>

namespace scada::mock {

struct MockServerOptions {
    std::uint16_t port = 2404;
    int sendIntervalMs = 1000;
    int dropEverySeconds = 20;
};

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

