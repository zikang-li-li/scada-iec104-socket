#include "scada/iec104/Iec104Client.h"

#include "scada/common/Logger.h"

#include <chrono>
#include <thread>

namespace scada::iec104 {

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
    while (running_) {
        net::TcpClient client;
        common::Logger::info("Connecting IEC104 server " + options_.host + ":" + std::to_string(options_.port));

        if (!client.connectTo(options_.host, options_.port, options_.connectTimeoutMs)) {
            common::Logger::warn("IEC104 connection failed, retrying in " + std::to_string(options_.reconnectMs) + " ms");
            notifyConnection(false);
            std::this_thread::sleep_for(std::chrono::milliseconds(options_.reconnectMs));
            continue;
        }

        notifyConnection(true);
        receiveSequence_ = 0;

        if (!performStartDt(client)) {
            common::Logger::warn("IEC104 STARTDT handshake failed");
            client.close();
            notifyConnection(false);
            std::this_thread::sleep_for(std::chrono::milliseconds(options_.reconnectMs));
            continue;
        }

        std::vector<std::uint8_t> frame;
        while (running_ && readApdu(client, frame)) {
            if (Iec104Frame::isTestFrAct(frame)) {
                client.sendAll(Iec104Frame::buildTestFrCon(), options_.receiveTimeoutMs);
                continue;
            }

            if (Iec104Frame::format(frame) != FrameFormat::I) {
                continue;
            }

            receiveSequence_ = static_cast<std::uint16_t>(Iec104Frame::sendSequence(frame) + 1);
            client.sendAll(Iec104Frame::buildSFormat(receiveSequence_), options_.receiveTimeoutMs);

            for (const auto& object : Iec104Frame::parseInformationObjects(frame)) {
                if (dataHandler_) {
                    dataHandler_(object);
                }
            }
        }

        common::Logger::warn("IEC104 link disconnected");
        client.close();
        notifyConnection(false);

        if (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(options_.reconnectMs));
        }
    }
}

bool Iec104Client::performStartDt(net::TcpClient& client) {
    if (!client.sendAll(Iec104Frame::buildStartDtAct(), options_.receiveTimeoutMs)) {
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

    return Iec104Frame::isValidApdu(frame);
}

void Iec104Client::notifyConnection(bool connected) {
    const bool previous = connected_.exchange(connected);
    if (previous != connected && connectionHandler_) {
        connectionHandler_(connected);
    }
}

} // namespace scada::iec104

