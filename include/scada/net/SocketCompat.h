#pragma once

#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
using SOCKET = int;
#endif

namespace scada::net {

#ifdef _WIN32
using SocketHandle = SOCKET;
#else
using SocketHandle = int;
#endif

SocketHandle invalidSocket();
void initializeSockets();
void closeSocket(SocketHandle socket);
bool isValid(SocketHandle socket);
bool setBlocking(SocketHandle socket, bool blocking);
bool setReuseAddress(SocketHandle socket);
bool waitReadable(SocketHandle socket, int timeoutMs);
bool waitWritable(SocketHandle socket, int timeoutMs);
bool lastErrorWouldBlock();
std::string lastSocketError();

} // namespace scada::net

