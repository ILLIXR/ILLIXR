#pragma once

#if defined(_WIN32) || defined(_WIN64)
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef _WINSOCKAPI_
#        define _WINSOCKAPI_
#    endif
// clang-format off
#  include <WinSock2.h>
#  include <ws2def.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "Ws2_32.lib")
#  define BYTE_TYPE   int
#  define SOCKET_TYPE SOCKET
// clang-format on
#else
#    include <arpa/inet.h>
#    include <netinet/in.h>
#    include <sys/socket.h>
#    include <sys/time.h>
#    include <unistd.h>
#    define BYTE_TYPE   ssize_t
#    define SOCKET_TYPE int
#endif

#include "illixr/export.hpp"

#include <cstring>
#include <mutex>
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

    // Allow reuse of local addresses.
    // On Linux/Android, also sets SO_REUSEPORT so that a new socket can bind
    // to a port still in TIME_WAIT after a crash, without waiting ~60 seconds.
    void socket_set_reuseaddr() const {
#if defined(_WIN32) || defined(_WIN64)
        int enable = 1;
        // Use SO_EXCLUSIVEADDRUSE on Windows - SO_REUSEADDR has unsafe semantics there
        if (setsockopt(fd_, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&enable), sizeof(enable)) < 0)
            throw std::runtime_error("SO_EXCLUSIVEADDRUSE failed");
#else
        const int enable = 1;
        setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
        setsockopt(fd_, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int));
#endif
    }

    /// Increase kernel socket buffers for high-rate video streams. The kernel may
    /// clamp the requested value to its configured maximum; that is acceptable.
    void socket_set_buffer_sizes(int bytes) const {
        if (bytes <= 0) {
            return;
        }
#if defined(_WIN32) || defined(_WIN64)
        setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&bytes), sizeof(bytes));
        setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&bytes), sizeof(bytes));
#else
        setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, &bytes, sizeof(bytes));
        setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &bytes, sizeof(bytes));
#endif
    }

    /// Bound the time a receive thread can remain asleep so plugin shutdown can
    /// join it even when the peer is no longer sending datagrams.
    void socket_set_receive_timeout(int milliseconds) const {
#if defined(_WIN32) || defined(_WIN64)
        const DWORD timeout = static_cast<DWORD>(milliseconds);
        setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
        const timeval timeout{milliseconds / 1000, (milliseconds % 1000) * 1000};
        setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif
    }

    /// Interrupt a blocking receive when supported. The receive timeout above
    /// remains the fallback for unconnected UDP sockets.
    void socket_shutdown() const noexcept {
#if defined(_WIN32) || defined(_WIN64)
        shutdown(fd_, SD_BOTH);
#else
        shutdown(fd_, SHUT_RDWR);
#endif
    }

    /// Mark latency-sensitive datagrams for expedited forwarding when the
    /// network honors DSCP. Failures are intentionally non-fatal.
    void socket_set_dscp_expedited_forwarding() const {
        constexpr int tos = 0b101110 << 2;
#if defined(_WIN32) || defined(_WIN64)
        setsockopt(fd_, IPPROTO_IP, IP_TOS, reinterpret_cast<const char*>(&tos), sizeof(tos));
#else
        setsockopt(fd_, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
#endif
    }

    // Store the peer address for use by write_data().
    // On the client this is called once with the server's address.
    // On the server this is called each time a datagram is received from a new peer.
    void set_peer(const std::string& ip, int port) {
        std::lock_guard<std::mutex> lock(peer_mutex_);
        std::memset(&peer_addr_, 0, sizeof(peer_addr_));
        peer_addr_.sin_family = AF_INET;
        peer_addr_.sin_port   = htons(static_cast<uint16_t>(port));
        inet_pton(AF_INET, ip.c_str(), &peer_addr_.sin_addr);
        peer_set_ = true;
    }

    // Store the peer address directly from a sockaddr_in, e.g. the src_addr filled in
    // by read_data(). This lets the server learn the client's address from an incoming
    // datagram, since UDP is connectionless and the server has no peer until it hears
    // from one.
    void set_peer(const sockaddr_in& addr) {
        std::lock_guard<std::mutex> lock(peer_mutex_);
        peer_addr_ = addr;
        peer_set_  = true;
    }

    // Send a datagram to the previously set peer address.
    // Returns false if no peer has been set or the send fails.
    bool write_data(const std::string& buffer) const {
        std::lock_guard<std::mutex> lock(peer_mutex_);
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
        std::lock_guard<std::mutex> lock(peer_mutex_);
        return peer_set_;
    }

private:
    SOCKET_TYPE        fd_;
    mutable std::mutex peer_mutex_;
    sockaddr_in        peer_addr_{};
    bool               peer_set_{false};

    static constexpr size_t BUFFER_SIZE = 1024 * 64; // 64 KB max datagram
};

} // namespace ILLIXR::network
