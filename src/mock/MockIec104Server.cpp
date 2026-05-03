#include "scada/mock/MockIec104Server.h"

#include "scada/common/Logger.h"
#include "scada/iec104/Iec104Frame.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <random>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

namespace scada::mock {
namespace {

constexpr std::uint8_t QualityInvalid = 0x80;
constexpr std::uint8_t QualityNotTopical = 0x40;
constexpr std::uint8_t QualityBlocked = 0x10;

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

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

MockScenario scenarioFromString(const std::string& value, MockScenario fallback) {
    const auto normalized = lower(value);
    if (normalized == "normal") {
        return MockScenario::Normal;
    }
    if (normalized == "alarm") {
        return MockScenario::Alarm;
    }
    if (normalized == "quality") {
        return MockScenario::Quality;
    }
    if (normalized == "stale") {
        return MockScenario::Stale;
    }
    if (normalized == "digital-trip" || normalized == "digital_trip") {
        return MockScenario::DigitalTrip;
    }
    if (normalized == "mixed") {
        return MockScenario::Mixed;
    }
    return fallback;
}

std::string scenarioName(MockScenario scenario) {
    switch (scenario) {
    case MockScenario::Normal:
        return "normal";
    case MockScenario::Alarm:
        return "alarm";
    case MockScenario::Quality:
        return "quality";
    case MockScenario::Stale:
        return "stale";
    case MockScenario::DigitalTrip:
        return "digital-trip";
    case MockScenario::Mixed:
        return "mixed";
    }
    return "mixed";
}

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

    common::Logger::info("Mock IEC104 server listening on 0.0.0.0:" + std::to_string(options_.port) +
                         ", scenario=" + scenarioName(options_.scenario));

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
    bool quietAnnounced = false;

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
            const auto elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(now - start).count();
            const bool quiet = options_.scenario == MockScenario::Stale &&
                               elapsedSeconds >= options_.quietAfterSeconds;
            if (quiet) {
                if (!quietAnnounced) {
                    common::Logger::warn("Mock IEC104 server keeps TCP up but stops sending telemetry");
                    quietAnnounced = true;
                }
                nextSend = now + std::chrono::milliseconds(options_.sendIntervalMs);
                continue;
            }

            float load = 72.0F + 5.0F * std::sin(tick * 0.35F) + noise(randomEngine);
            float voltage = 220.0F + 2.0F * std::sin(tick * 0.21F) + noise(randomEngine);
            bool breakerClosed = true;
            std::uint8_t loadQuality = 0;
            std::uint8_t voltageQuality = 0;
            std::uint8_t breakerQuality = 0;

            if (options_.scenario == MockScenario::Alarm) {
                load = tick % 2 == 0 ? 98.0F : 90.0F;
                voltage = tick % 3 == 0 ? 197.0F : 236.0F;
            } else if (options_.scenario == MockScenario::Quality) {
                const bool bad = options_.qualityEvery > 0 && tick % options_.qualityEvery == 0;
                loadQuality = bad ? QualityInvalid : 0;
                voltageQuality = bad ? QualityNotTopical : 0;
                breakerQuality = bad ? QualityBlocked : 0;
            } else if (options_.scenario == MockScenario::DigitalTrip) {
                breakerClosed = tick % 10 < 5;
            } else if (options_.scenario == MockScenario::Mixed) {
                load = tick % 18 == 0 ? 98.0F : 74.0F + 12.0F * std::sin(tick * 0.35F) + noise(randomEngine);
                voltage = tick % 23 == 0 ? 197.0F : 220.0F + 7.0F * std::sin(tick * 0.21F) + noise(randomEngine);
                breakerClosed = tick % 16 < 13;
                if (options_.qualityEvery > 0 && tick % (options_.qualityEvery * 4) == 0) {
                    loadQuality = QualityInvalid;
                }
            }

            const auto loadFrame = iec104::Iec104Frame::buildIFormat(
                sendSequence++,
                0,
                iec104::Iec104Frame::buildFloatMeasurementAsdu(
                    options_.commonAddress,
                    options_.loadIoa,
                    load,
                    loadQuality));
            const auto voltageFrame = iec104::Iec104Frame::buildIFormat(
                sendSequence++,
                0,
                iec104::Iec104Frame::buildFloatMeasurementAsdu(
                    options_.commonAddress,
                    options_.voltageIoa,
                    voltage,
                    voltageQuality));
            const auto breakerFrame = iec104::Iec104Frame::buildIFormat(
                sendSequence++,
                0,
                iec104::Iec104Frame::buildSinglePointAsdu(
                    options_.commonAddress,
                    options_.breakerIoa,
                    breakerClosed,
                    breakerQuality));

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
