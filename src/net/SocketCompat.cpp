#include "scada/net/SocketCompat.h"

#include <cerrno>
#include <cstring>
#include <mutex>
#include <sstream>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

namespace scada::net {
namespace {

#ifdef _WIN32
std::once_flag winsockOnce;
#endif

} // namespace

SocketHandle invalidSocket() {
#ifdef _WIN32
    return INVALID_SOCKET;
#else
    return -1;
#endif
}

void initializeSockets() {
#ifdef _WIN32
    std::call_once(winsockOnce, []() {
        WSADATA data{};
        WSAStartup(MAKEWORD(2, 2), &data);
    });
#endif
}

void closeSocket(SocketHandle socket) {
    if (!isValid(socket)) {
        return;
    }
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

bool isValid(SocketHandle socket) {
#ifdef _WIN32
    return socket != INVALID_SOCKET;
#else
    return socket >= 0;
#endif
}

bool setBlocking(SocketHandle socket, bool blocking) {
#ifdef _WIN32
    u_long mode = blocking ? 0 : 1;
    return ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    const int updated = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
    return fcntl(socket, F_SETFL, updated) == 0;
#endif
}

bool setReuseAddress(SocketHandle socket) {
    int value = 1;
    return setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&value), sizeof(value)) == 0;
}

bool waitReadable(SocketHandle socket, int timeoutMs) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(socket, &set);

    timeval timeout{};
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = (timeoutMs % 1000) * 1000;

#ifdef _WIN32
    const int result = select(0, &set, nullptr, nullptr, &timeout);
#else
    const int result = select(socket + 1, &set, nullptr, nullptr, &timeout);
#endif
    return result > 0 && FD_ISSET(socket, &set);
}

bool waitWritable(SocketHandle socket, int timeoutMs) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(socket, &set);

    timeval timeout{};
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = (timeoutMs % 1000) * 1000;

#ifdef _WIN32
    const int result = select(0, nullptr, &set, nullptr, &timeout);
#else
    const int result = select(socket + 1, nullptr, &set, nullptr, &timeout);
#endif
    return result > 0 && FD_ISSET(socket, &set);
}

bool lastErrorWouldBlock() {
#ifdef _WIN32
    const int error = WSAGetLastError();
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS || error == WSAEINVAL;
#else
    return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS;
#endif
}

std::string lastSocketError() {
#ifdef _WIN32
    const int error = WSAGetLastError();
    std::ostringstream stream;
    stream << "winsock error " << error;
    return stream.str();
#else
    return std::strerror(errno);
#endif
}

} // namespace scada::net

