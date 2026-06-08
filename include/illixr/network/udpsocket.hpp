#pragma once

#if defined(_WIN32) || defined(_WIN64)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef _WINSOCKAPI_
        #define _WINSOCKAPI_
    #endif
// clang-format off
    #include <WinSock2.h>
    #include <ws2def.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "Ws2_32.lib")
    #define BYTE_TYPE   int
    #define SOCKET_TYPE SOCKET
// clang-format on
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
    #define BYTE_TYPE   ssize_t
    #define SOCKET_TYPE int
#endif

#include "illixr/export.hpp"

#include <cstring>
#include <stdexcept>
#include <string>

namespace ILLIXR::network {

class MY_EXPORT_API UDPSocket {
public:
    UDPSocket() {
#if defined(_WIN32) || defined(_WIN64)
        static bool initialized = false;
        if (!initialized) {
            WSAData wsa_data;
            if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
                throw std::runtime_error("WSAStartup failed.");
            initialized = true;
        }
        fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (fd_ == INVALID_SOCKET)
            throw std::runtime_error("UDP socket creation failed");
#else
        fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (fd_ < 0)
            throw std::runtime_error("UDP socket creation failed");
#endif
    }

    ~UDPSocket() {
#if defined(_WIN32) || defined(_WIN64)
        closesocket(fd_);
#else
        close(fd_);
#endif
    }

    // Bind to a local address and port (required on the server side, optional on the client)
    void socket_bind(const std::string& ip, int port) const {
        sockaddr_in local_addr{};
        local_addr.sin_family      = AF_INET;
        local_addr.sin_port        = htons(static_cast<uint16_t>(port));
        local_addr.sin_addr.s_addr = ip.empty() ? INADDR_ANY : inet_addr(ip.c_str());
        if (bind(fd_, reinterpret_cast<sockaddr*>(&local_addr), sizeof(local_addr)) < 0)
            throw std::runtime_error("UDP bind failed");
    }

    // Bind to all interfaces on a given port (server convenience overload)
    void socket_bind(int port) const {
        socket_bind("", port);
    }

    // Allow reuse of local addresses
    void socket_set_reuseaddr() const {
#if defined(_WIN32) || defined(_WIN64)
        int enable = 1;
        // Use SO_EXCLUSIVEADDRUSE on Windows — SO_REUSEADDR has unsafe semantics there
        if (setsockopt(fd_, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&enable), sizeof(enable)) < 0)
            throw std::runtime_error("SO_EXCLUSIVEADDRUSE failed");
#else
        const int enable = 1;
        setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
#endif
    }

    // Store the peer address for use by write_data().
    // On the client this is called once with the server's address.
    // On the server this is called each time a datagram is received from a new peer.
    void set_peer(const std::string& ip, int port) {
        std::memset(&peer_addr_, 0, sizeof(peer_addr_));
        peer_addr_.sin_family = AF_INET;
        peer_addr_.sin_port   = htons(static_cast<uint16_t>(port));
        inet_pton(AF_INET, ip.c_str(), &peer_addr_.sin_addr);
        peer_set_ = true;
    }

    // Send a datagram to the previously set peer address.
    // Returns false if no peer has been set or the send fails.
    bool write_data(const std::string& buffer) const {
        if (!peer_set_)
            return false;
        BYTE_TYPE sent = sendto(fd_,
#if defined(_WIN32) || defined(_WIN64)
                                buffer.data(), static_cast<int>(buffer.size()),
#else
                                buffer.data(), buffer.size(),
#endif
                                0, reinterpret_cast<const sockaddr*>(&peer_addr_), sizeof(peer_addr_));
        return sent == static_cast<BYTE_TYPE>(buffer.size());
    }

    // Send a datagram to an explicit destination (used by the server to reply to a specific client).
    bool write_data_to(const std::string& buffer, const sockaddr_in& dest) const {
        BYTE_TYPE sent = sendto(fd_,
#if defined(_WIN32) || defined(_WIN64)
                                buffer.data(), static_cast<int>(buffer.size()),
#else
                                buffer.data(), buffer.size(),
#endif
                                0, reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
        return sent == static_cast<BYTE_TYPE>(buffer.size());
    }

    // Receive a single datagram.  Blocks until data arrives.
    // Optionally fills src_addr with the sender's address.
    [[nodiscard]] std::string read_data(sockaddr_in* src_addr = nullptr) const {
        char        buffer[BUFFER_SIZE];
        sockaddr_in from{};
#if defined(_WIN32) || defined(_WIN64)
        int from_len = sizeof(from);
#else
        socklen_t from_len = sizeof(from);
#endif
        BYTE_TYPE bytes = recvfrom(fd_, buffer,
#if defined(_WIN32) || defined(_WIN64)
                                   static_cast<int>(BUFFER_SIZE),
#else
                                   BUFFER_SIZE,
#endif
                                   0, reinterpret_cast<sockaddr*>(&from), &from_len);
        if (bytes <= 0)
            return {};
        if (src_addr != nullptr)
            *src_addr = from;
        return std::string(buffer, static_cast<size_t>(bytes));
    }

    /* accessors */
    [[nodiscard]] std::string local_address() const {
        sockaddr_in local{};
        socklen_t   size = sizeof(local);
        getsockname(fd_, reinterpret_cast<sockaddr*>(&local), &size);
#if defined(_WIN32) || defined(_WIN64)
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &local.sin_addr, ip, sizeof(ip));
#else
        char* ip = inet_ntoa(local.sin_addr);
#endif
        return std::string(ip) + ":" + std::to_string(ntohs(local.sin_port));
    }

    [[nodiscard]] bool has_peer() const {
        return peer_set_;
    }

private:
    SOCKET_TYPE fd_;
    sockaddr_in peer_addr_{};
    bool        peer_set_{false};

    static constexpr size_t BUFFER_SIZE = 1024 * 64; // 64 KB max datagram
};

} // namespace ILLIXR::network
