#include "scada/iec104/Iec104Client.h"

#include "scada/common/Logger.h"

#include <chrono>
#include <thread>

namespace scada::iec104 {
namespace {

constexpr int LinkPollMs = 200;

std::string endpoint(const Iec104ClientOptions& options) {
    return options.host + ":" + std::to_string(options.port);
}

} // namespace

Iec104Client::Iec104Client(Iec104ClientOptions options)
    : options_(std::move(options)) {}

Iec104Client::~Iec104Client() {
    stop();
}

void Iec104Client::setDataHandler(DataHandler handler) {
    dataHandler_ = std::move(handler);
}

void Iec104Client::setConnectionHandler(ConnectionHandler handler) {
    connectionHandler_ = std::move(handler);
}

void Iec104Client::start() {
    if (running_.exchange(true)) {
        return;
    }
    worker_ = std::thread(&Iec104Client::run, this);
}

void Iec104Client::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    notifyConnection(false);
}

bool Iec104Client::running() const {
    return running_.load();
}

void Iec104Client::run() {
    int attempt = 0;
    while (running_) {
        net::TcpClient client;
        ++attempt;
        common::Logger::info("IEC104 connecting " + endpoint(options_) +
                             ", attempt=" + std::to_string(attempt));

        if (!client.connectTo(options_.host, options_.port, options_.connectTimeoutMs)) {
            notifyConnection(false);
            sleepBeforeReconnect("IEC104 connect failed: " + endpoint(options_));
            continue;
        }

        attempt = 0;
        common::Logger::info("IEC104 TCP connected: " + endpoint(options_));
        notifyConnection(true);
        receiveSequence_ = 0;

        if (!performStartDt(client)) {
            client.close();
            notifyConnection(false);
            sleepBeforeReconnect("IEC104 STARTDT handshake failed: " + endpoint(options_));
            continue;
        }

        common::Logger::info("IEC104 data transfer started: " + endpoint(options_));

        std::vector<std::uint8_t> frame;
        auto lastRx = std::chrono::steady_clock::now();
        auto heartbeatSentAt = lastRx;
        bool heartbeatPending = false;

        while (running_) {
            const auto now = std::chrono::steady_clock::now();
            if (heartbeatPending &&
                options_.heartbeatTimeoutMs > 0 &&
                now - heartbeatSentAt >= std::chrono::milliseconds(options_.heartbeatTimeoutMs)) {
                common::Logger::warn("IEC104 heartbeat timeout: " + endpoint(options_));
                break;
            }

            if (!heartbeatPending &&
                options_.heartbeatIntervalMs > 0 &&
                now - lastRx >= std::chrono::milliseconds(options_.heartbeatIntervalMs)) {
                if (!sendHeartbeat(client)) {
                    common::Logger::warn("IEC104 send TESTFR act failed: " + endpoint(options_));
                    break;
                }
                heartbeatPending = true;
                heartbeatSentAt = now;
            }

            if (!net::waitReadable(client.handle(), LinkPollMs)) {
                continue;
            }

            if (!readApdu(client, frame)) {
                common::Logger::warn("IEC104 receive failed: " + endpoint(options_));
                break;
            }

            lastRx = std::chrono::steady_clock::now();
            if (Iec104Frame::isTestFrCon(frame)) {
                heartbeatPending = false;
                common::Logger::info("IEC104 heartbeat OK: " + endpoint(options_));
                continue;
            }

            if (Iec104Frame::isTestFrAct(frame)) {
                const auto response = Iec104Frame::buildTestFrCon();
                common::Logger::debug("TX " + Iec104Frame::describe(response));
                if (!client.sendAll(response, options_.receiveTimeoutMs)) {
                    common::Logger::warn("IEC104 send TESTFR con failed: " + endpoint(options_));
                    break;
                }
                continue;
            }

            if (Iec104Frame::format(frame) != FrameFormat::I) {
                continue;
            }

            receiveSequence_ = static_cast<std::uint16_t>(Iec104Frame::sendSequence(frame) + 1);
            const auto ack = Iec104Frame::buildSFormat(receiveSequence_);
            common::Logger::debug("TX " + Iec104Frame::describe(ack));
            if (!client.sendAll(ack, options_.receiveTimeoutMs)) {
                common::Logger::warn("IEC104 send S-format ACK failed: " + endpoint(options_));
                break;
            }

            for (const auto& object : Iec104Frame::parseInformationObjects(frame)) {
                if (dataHandler_) {
                    dataHandler_(object);
                }
            }
        }

        client.close();
        notifyConnection(false);
        sleepBeforeReconnect("IEC104 link disconnected: " + endpoint(options_));
    }
}

bool Iec104Client::performStartDt(net::TcpClient& client) {
    const auto startDt = Iec104Frame::buildStartDtAct();
    common::Logger::debug("TX " + Iec104Frame::describe(startDt));
    if (!client.sendAll(startDt, options_.receiveTimeoutMs)) {
        return false;
    }

    std::vector<std::uint8_t> frame;
    if (!readApdu(client, frame)) {
        return false;
    }

    return Iec104Frame::isStartDtCon(frame);
}

bool Iec104Client::readApdu(net::TcpClient& client, std::vector<std::uint8_t>& frame) {
    frame.clear();

    std::uint8_t byte = 0;
    do {
        if (!client.receiveExact(&byte, 1, options_.receiveTimeoutMs)) {
            return false;
        }
    } while (byte != Iec104Frame::StartByte && running_);

    if (!running_) {
        return false;
    }

    std::uint8_t length = 0;
    if (!client.receiveExact(&length, 1, options_.receiveTimeoutMs)) {
        return false;
    }

    if (length < 4 || length > Iec104Frame::MaxApduLength - 2) {
        return false;
    }

    frame.resize(static_cast<std::size_t>(length) + 2);
    frame[0] = Iec104Frame::StartByte;
    frame[1] = length;

    if (!client.receiveExact(frame.data() + 2, length, options_.receiveTimeoutMs)) {
        return false;
    }

    const bool valid = Iec104Frame::isValidApdu(frame);
    if (valid) {
        common::Logger::debug("RX " + Iec104Frame::describe(frame));
    }
    return valid;
}

bool Iec104Client::sendHeartbeat(net::TcpClient& client) {
    const auto heartbeat = Iec104Frame::buildTestFrAct();
    common::Logger::info("IEC104 heartbeat TX: " + endpoint(options_));
    common::Logger::debug("TX " + Iec104Frame::describe(heartbeat));
    return client.sendAll(heartbeat, options_.receiveTimeoutMs);
}

void Iec104Client::sleepBeforeReconnect(const std::string& reason) {
    if (!running_) {
        return;
    }

    common::Logger::warn(reason + "; reconnecting in " + std::to_string(options_.reconnectMs) + " ms");

    const auto interval = std::chrono::milliseconds(options_.reconnectMs);
    const auto deadline = std::chrono::steady_clock::now() + interval;
    while (running_ && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void Iec104Client::notifyConnection(bool connected) {
    const bool previous = connected_.exchange(connected);
    if (previous != connected && connectionHandler_) {
        connectionHandler_(connected);
    }
}

} // namespace scada::iec104
