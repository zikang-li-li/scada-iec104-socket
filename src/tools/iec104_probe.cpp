#include "scada/iec104/Iec104Frame.h"
#include "scada/net/TcpClient.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

struct ProbeOptions {
    std::string host = "127.0.0.1";
    std::uint16_t port = 2404;
    int connectTimeoutMs = 3000;
    int receiveTimeoutMs = 1000;
    int seconds = 10;
    int maxFrames = 0;
    bool hex = false;
};

struct ProbeStats {
    int frames = 0;
    int iFrames = 0;
    int sFrames = 0;
    int uFrames = 0;
    int objects = 0;
    int badQualityObjects = 0;
};

int toInt(const std::string& value, int fallback) {
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

bool hasBadQuality(const scada::model::DataQuality& quality) {
    return quality.invalid || quality.notTopical || quality.substituted ||
           quality.blocked || quality.overflow;
}

void printUsage(const char* program) {
    std::cout
        << "Usage: " << program
        << " [--host 127.0.0.1] [--port 2404] [--seconds 10]\n"
        << "       [--timeout-ms 1000] [--max-frames 20] [--hex]\n\n"
        << "Purpose: verify TCP/2404, STARTDT, I/S/U frames, IOA values and IEC104 quality bits.\n";
}

bool readApdu(scada::net::TcpClient& client, std::vector<std::uint8_t>& frame, int timeoutMs) {
    frame.clear();

    std::uint8_t byte = 0;
    do {
        if (!client.receiveExact(&byte, 1, timeoutMs)) {
            return false;
        }
    } while (byte != scada::iec104::Iec104Frame::StartByte);

    std::uint8_t length = 0;
    if (!client.receiveExact(&length, 1, timeoutMs)) {
        return false;
    }
    if (length < 4 || length > scada::iec104::Iec104Frame::MaxApduLength - 2) {
        return false;
    }

    frame.resize(static_cast<std::size_t>(length) + 2);
    frame[0] = scada::iec104::Iec104Frame::StartByte;
    frame[1] = length;
    if (!client.receiveExact(frame.data() + 2, length, timeoutMs)) {
        return false;
    }

    return scada::iec104::Iec104Frame::isValidApdu(frame);
}

void updateStats(const std::vector<std::uint8_t>& frame, ProbeStats& stats) {
    ++stats.frames;
    const auto format = scada::iec104::Iec104Frame::format(frame);
    if (format == scada::iec104::FrameFormat::I) {
        ++stats.iFrames;
        const auto info = scada::iec104::Iec104Frame::parseAsduInfo(frame);
        if (info) {
            stats.objects += static_cast<int>(info->objects.size());
            for (const auto& object : info->objects) {
                if (hasBadQuality(object.quality)) {
                    ++stats.badQualityObjects;
                }
            }
        }
    } else if (format == scada::iec104::FrameFormat::S) {
        ++stats.sFrames;
    } else if (format == scada::iec104::FrameFormat::U) {
        ++stats.uFrames;
    }
}

} // namespace

int main(int argc, char** argv) {
    ProbeOptions options;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            printUsage(argv[0]);
            return 0;
        }
        if (argument == "--host" && index + 1 < argc) {
            options.host = argv[++index];
        } else if (argument == "--port" && index + 1 < argc) {
            options.port = static_cast<std::uint16_t>(toInt(argv[++index], options.port));
        } else if (argument == "--seconds" && index + 1 < argc) {
            options.seconds = std::max(1, toInt(argv[++index], options.seconds));
        } else if (argument == "--timeout-ms" && index + 1 < argc) {
            options.receiveTimeoutMs = std::max(100, toInt(argv[++index], options.receiveTimeoutMs));
        } else if (argument == "--connect-timeout-ms" && index + 1 < argc) {
            options.connectTimeoutMs = std::max(100, toInt(argv[++index], options.connectTimeoutMs));
        } else if (argument == "--max-frames" && index + 1 < argc) {
            options.maxFrames = std::max(0, toInt(argv[++index], options.maxFrames));
        } else if (argument == "--hex") {
            options.hex = true;
        }
    }

    scada::net::TcpClient client;
    std::cout << "Connecting IEC104 " << options.host << ":" << options.port << " ...\n";
    if (!client.connectTo(options.host, options.port, options.connectTimeoutMs)) {
        std::cerr << "Connection failed. Check IP, route, firewall and TCP/2404 service.\n";
        return 1;
    }

    if (!client.sendAll(scada::iec104::Iec104Frame::buildStartDtAct(), options.receiveTimeoutMs)) {
        std::cerr << "Failed to send STARTDT act.\n";
        return 1;
    }

    std::vector<std::uint8_t> frame;
    if (!readApdu(client, frame, options.receiveTimeoutMs) ||
        !scada::iec104::Iec104Frame::isStartDtCon(frame)) {
        std::cerr << "STARTDT confirmation failed";
        if (!frame.empty()) {
            std::cerr << ": " << scada::iec104::Iec104Frame::describe(frame);
        }
        std::cerr << "\n";
        return 1;
    }

    std::cout << "STARTDT confirmed: " << scada::iec104::Iec104Frame::describe(frame) << "\n";

    ProbeStats stats;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(options.seconds);
    std::uint16_t receiveSequence = 0;

    while (std::chrono::steady_clock::now() < deadline &&
           (options.maxFrames == 0 || stats.frames < options.maxFrames)) {
        if (!readApdu(client, frame, options.receiveTimeoutMs)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        updateStats(frame, stats);
        std::cout << "[" << stats.frames << "] " << scada::iec104::Iec104Frame::describe(frame) << "\n";
        if (options.hex) {
            std::cout << "    hex: " << scada::iec104::Iec104Frame::toHex(frame) << "\n";
        }

        if (scada::iec104::Iec104Frame::isTestFrAct(frame)) {
            client.sendAll(scada::iec104::Iec104Frame::buildTestFrCon(), options.receiveTimeoutMs);
        } else if (scada::iec104::Iec104Frame::format(frame) == scada::iec104::FrameFormat::I) {
            receiveSequence = static_cast<std::uint16_t>(scada::iec104::Iec104Frame::sendSequence(frame) + 1);
            client.sendAll(scada::iec104::Iec104Frame::buildSFormat(receiveSequence), options.receiveTimeoutMs);
        }
    }

    std::cout << "\nSummary: frames=" << stats.frames
              << ", I=" << stats.iFrames
              << ", S=" << stats.sFrames
              << ", U=" << stats.uFrames
              << ", objects=" << stats.objects
              << ", bad_quality_objects=" << stats.badQualityObjects << "\n";

    return stats.frames > 0 ? 0 : 2;
}
