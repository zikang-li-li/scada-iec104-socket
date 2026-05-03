#pragma once

#include "scada/net/SocketCompat.h"

#include <cstdint>
#include <string>
#include <vector>

namespace scada::net {

class TcpClient {
public:
    TcpClient() = default;
    ~TcpClient();

    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;

    bool connectTo(const std::string& host, std::uint16_t port, int timeoutMs);
    bool sendAll(const std::vector<std::uint8_t>& data, int timeoutMs);
    bool receiveExact(std::uint8_t* buffer, std::size_t size, int timeoutMs);
    void close();

    bool connected() const;
    SocketHandle handle() const;

private:
    SocketHandle socket_ = invalidSocket();
};

} // namespace scada::net

