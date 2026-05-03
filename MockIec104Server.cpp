#include "scada/mock/MockIec104Server.h"

#include "scada/common/Logger.h"
#include "scada/iec104/Iec104Frame.h"

#include <chrono>
#include <cmath>
#include <random>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

namespace scada::mock {
namespace {

bool sendAll(scada::net::SocketHandle socket, const std::vector<std::uint8_t>& data, int timeoutMs) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        if (!scada::net::waitWritable(socket, timeoutMs)) {
            return false;
        }
        const int count = ::send(socket, reinterpret_cast<const char*>(data.data() + sent), static_cast<int>(data.size() - sent), 0);
        if (count <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(count);
    }
    return true;
}

bool receiveExact(scada::net::SocketHandle socket, std::uint8_t* buffer, std::size_t size, int timeoutMs) {
    std::size_t received = 0;
    while (received < size) {
        if (!scada::net::waitReadable(socket, timeoutMs)) {
            return false;
        }
        const int count = ::recv(socket, reinterpret_cast<char*>(buffer + received), static_cast<int>(size - received), 0);
        if (count <= 0) {
            return false;
        }
        received += static_cast<std::size_t>(count);
    }
    return true;
}

bool readApdu(scada::net::SocketHandle socket, std::vector<std::uint8_t>& frame, int timeoutMs) {
    frame.clear();

    std::uint8_t start = 0;
    do {
        if (!receiveExact(socket, &start, 1, timeoutMs)) {
            return false;
        }
    } while (start != iec104::Iec104Frame::StartByte);

    std::uint8_t length = 0;
    if (!receiveExact(socket, &length, 1, timeoutMs) || length < 4) {
        return false;
    }

    frame.resize(static_cast<std::size_t>(length) + 2);
    frame[0] = iec104::Iec104Frame::StartByte;
    frame[1] = length;
    if (!receiveExact(socket, frame.data() + 2, length, timeoutMs)) {
        return false;
    }

    return iec104::Iec104Frame::isValidApdu(frame);
}

} // namespace

MockIec104Server::MockIec104Server(MockServerOptions options)
    : options_(options) {}

MockIec104Server::~MockIec104Server() {
    stop();
}

void MockIec104Server::start() {
    if (running_.exchange(true)) {
        return;
    }
    worker_ = std::thread(&MockIec104Server::run, this);
}

void MockIec104Server::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool MockIec104Server::running() const {
    return running_.load();
}

void MockIec104Server::run() {
    net::initializeSockets();

    const auto listenSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (!net::isValid(listenSocket)) {
        common::Logger::error("Failed to create mock server socket");
        running_.store(false);
        return;
    }

    net::setReuseAddress(listenSocket);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(options_.port);

    if (::bind(listenSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        common::Logger::error("Failed to bind mock IEC104 server on port " + std::to_string(options_.port));
        net::closeSocket(listenSocket);
        running_.store(false);
        return;
    }

    if (::listen(listenSocket, 1) != 0) {
        common::Logger::error("Failed to listen on mock IEC104 server socket");
        net::closeSocket(listenSocket);
        running_.store(false);
        return;
    }

    common::Logger::info("Mock IEC104 server listening on 0.0.0.0:" + std::to_string(options_.port));

    while (running_) {
        if (!net::waitReadable(listenSocket, 500)) {
            continue;
        }

        sockaddr_storage peer{};
#ifdef _WIN32
        int peerLength = sizeof(peer);
#else
        socklen_t peerLength = sizeof(peer);
#endif
        const auto client = ::accept(listenSocket, reinterpret_cast<sockaddr*>(&peer), &peerLength);
        if (!net::isValid(client)) {
            continue;
        }

        common::Logger::info("Mock IEC104 client accepted");
        serveClient(client);
        net::closeSocket(client);
        common::Logger::warn("Mock IEC104 client closed");
    }

    net::closeSocket(listenSocket);
}

void MockIec104Server::serveClient(net::SocketHandle client) {
    std::vector<std::uint8_t> frame;
    if (!readApdu(client, frame, 5000) || !iec104::Iec104Frame::isStartDtAct(frame)) {
        common::Logger::warn("Mock IEC104 server did not receive STARTDT act");
        return;
    }

    if (!sendAll(client, iec104::Iec104Frame::buildStartDtCon(), 1000)) {
        return;
    }

    std::default_random_engine randomEngine{std::random_device{}()};
    std::uniform_real_distribution<float> noise(-2.0F, 2.0F);
    std::uint16_t sendSequence = 0;
    int tick = 0;

    const auto start = std::chrono::steady_clock::now();
    auto nextSend = start;

    while (running_) {
        const auto now = std::chrono::steady_clock::now();
        if (options_.dropEverySeconds > 0 &&
            std::chrono::duration_cast<std::chrono::seconds>(now - start).count() >= options_.dropEverySeconds) {
            common::Logger::warn("Mock IEC104 server simulating link drop");
            return;
        }

        if (net::waitReadable(client, 10)) {
            if (!readApdu(client, frame, 100)) {
                return;
            }
            if (iec104::Iec104Frame::isTestFrAct(frame)) {
                sendAll(client, iec104::Iec104Frame::buildTestFrCon(), 1000);
            } else if (iec104::Iec104Frame::isStartDtAct(frame)) {
                sendAll(client, iec104::Iec104Frame::buildStartDtCon(), 1000);
            }
        }

        if (now >= nextSend) {
            ++tick;
            const float load = tick % 18 == 0 ? 98.0F : 74.0F + 12.0F * std::sin(tick * 0.35F) + noise(randomEngine);
            const float voltage = tick % 23 == 0 ? 197.0F : 220.0F + 7.0F * std::sin(tick * 0.21F) + noise(randomEngine);
            const bool breakerClosed = tick % 16 < 13;

            const auto loadFrame = iec104::Iec104Frame::buildIFormat(
                sendSequence++,
                0,
                iec104::Iec104Frame::buildFloatMeasurementAsdu(1, 1001, load));
            const auto voltageFrame = iec104::Iec104Frame::buildIFormat(
                sendSequence++,
                0,
                iec104::Iec104Frame::buildFloatMeasurementAsdu(1, 1002, voltage));
            const auto breakerFrame = iec104::Iec104Frame::buildIFormat(
                sendSequence++,
                0,
                iec104::Iec104Frame::buildSinglePointAsdu(1, 2001, breakerClosed));

            if (!sendAll(client, loadFrame, 1000) ||
                !sendAll(client, voltageFrame, 1000) ||
                !sendAll(client, breakerFrame, 1000)) {
                return;
            }

            nextSend = now + std::chrono::milliseconds(options_.sendIntervalMs);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

} // namespace scada::mock

