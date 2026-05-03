#include "scada/net/TcpClient.h"

#include "scada/common/Logger.h"

#include <cstring>
#include <string>

namespace scada::net {

TcpClient::~TcpClient() {
    close();
}

bool TcpClient::connectTo(const std::string& host, std::uint16_t port, int timeoutMs) {
    close();
    initializeSockets();

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* results = nullptr;
    const auto portText = std::to_string(port);
    const int gai = getaddrinfo(host.c_str(), portText.c_str(), &hints, &results);
    if (gai != 0) {
        common::Logger::warn("getaddrinfo failed for " + host + ":" + portText);
        return false;
    }

    bool connected = false;
    for (auto* current = results; current != nullptr && !connected; current = current->ai_next) {
        SocketHandle candidate = ::socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (!isValid(candidate)) {
            continue;
        }

        setBlocking(candidate, false);
        const int result = ::connect(candidate, current->ai_addr, static_cast<int>(current->ai_addrlen));
        if (result == 0 || lastErrorWouldBlock()) {
            if (result == 0 || waitWritable(candidate, timeoutMs)) {
                int socketError = 0;
#ifdef _WIN32
                int optionLength = sizeof(socketError);
#else
                socklen_t optionLength = sizeof(socketError);
#endif
                if (getsockopt(candidate, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socketError), &optionLength) == 0 &&
                    socketError == 0) {
                    setBlocking(candidate, true);
                    socket_ = candidate;
                    connected = true;
                    break;
                }
            }
        }

        closeSocket(candidate);
    }

    freeaddrinfo(results);
    return connected;
}

bool TcpClient::sendAll(const std::vector<std::uint8_t>& data, int timeoutMs) {
    if (!connected()) {
        return false;
    }

    std::size_t sent = 0;
    while (sent < data.size()) {
        if (!waitWritable(socket_, timeoutMs)) {
            return false;
        }

        const auto remaining = static_cast<int>(data.size() - sent);
        const int count = ::send(socket_, reinterpret_cast<const char*>(data.data() + sent), remaining, 0);
        if (count <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(count);
    }

    return true;
}

bool TcpClient::receiveExact(std::uint8_t* buffer, std::size_t size, int timeoutMs) {
    if (!connected()) {
        return false;
    }

    std::size_t received = 0;
    while (received < size) {
        if (!waitReadable(socket_, timeoutMs)) {
            return false;
        }

        const int count = ::recv(socket_, reinterpret_cast<char*>(buffer + received), static_cast<int>(size - received), 0);
        if (count <= 0) {
            return false;
        }
        received += static_cast<std::size_t>(count);
    }

    return true;
}

void TcpClient::close() {
    if (isValid(socket_)) {
        closeSocket(socket_);
        socket_ = invalidSocket();
    }
}

bool TcpClient::connected() const {
    return isValid(socket_);
}

SocketHandle TcpClient::handle() const {
    return socket_;
}

} // namespace scada::net

