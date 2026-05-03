#pragma once

#include "scada/iec104/Iec104Frame.h"
#include "scada/net/TcpClient.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

namespace scada::iec104 {

struct Iec104ClientOptions {
    std::string host = "127.0.0.1";
    std::uint16_t port = 2404;
    int connectTimeoutMs = 3000;
    int receiveTimeoutMs = 5000;
    int reconnectMs = 2000;
};

class Iec104Client {
public:
    using DataHandler = std::function<void(const Iec104Object&)>;
    using ConnectionHandler = std::function<void(bool connected)>;

    explicit Iec104Client(Iec104ClientOptions options);
    ~Iec104Client();

    Iec104Client(const Iec104Client&) = delete;
    Iec104Client& operator=(const Iec104Client&) = delete;

    void setDataHandler(DataHandler handler);
    void setConnectionHandler(ConnectionHandler handler);

    void start();
    void stop();
    bool running() const;

private:
    void run();
    bool performStartDt(scada::net::TcpClient& client);
    bool readApdu(scada::net::TcpClient& client, std::vector<std::uint8_t>& frame);
    void notifyConnection(bool connected);

    Iec104ClientOptions options_;
    DataHandler dataHandler_;
    ConnectionHandler connectionHandler_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::thread worker_;
    std::uint16_t receiveSequence_ = 0;
};

} // namespace scada::iec104

