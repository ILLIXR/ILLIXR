#pragma once
#if defined(_WIN32) || defined(_WIN64)
#    define _WINSOCKAPI_
#    define WIN32_LEAN_AND_MEAN
#endif
#include <stdexcept>
#if defined(_WIN32) || defined(_WIN64)
// clang-format off
#  include <WinSock2.h>  // Must come FIRST
#  include <ws2def.h>
#  include <ws2tcpip.h>
#  include <mstcpip.h>
#  include <WindNS.h>
#  include <iphlpapi.h>
#  include <icmpapi.h>
#  include <iphlpapi.h>
#  include <mstcpip.h>
#  include <nldef.h>
// clang-format on
#    pragma comment(lib, "Ws2_32.lib")
#    pragma comment(lib, "Iphlpapi.lib")
#    define BYTE_TYPE   int
#    define SOCKET_TYPE SOCKET
#else
#    include <arpa/inet.h>
#    include <netinet/in.h>
#    include <netinet/tcp.h>
#    include <sys/socket.h>
#    include <unistd.h>
#    define BYTE_TYPE   ssize_t
#    define SOCKET_TYPE int
#endif

#include "illixr/export.hpp"

#include <string>

namespace ILLIXR::network {

class MY_EXPORT_API TCPSocket {
public:
    TCPSocket() {
#if defined(_WIN32) || defined(_WIN64)
        static bool initialized = false;
        if (!initialized) {
            WSAData wsa_data;
            if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
                throw std::runtime_error("WSAStartup failed.");
            }
            initialized = true;
        }
#endif
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
#if defined(_WIN32) || defined(_WIN64)
        if (fd_ == INVALID_SOCKET)
            throw std::runtime_error("startup failed");
#endif
    }

    [[maybe_unused]] explicit TCPSocket(int fd) {
        fd_ = fd;
    }

    // Destructor
    // Close the file descriptor
    ~TCPSocket() {
#if defined(_WIN32) || defined(_WIN64)
        closesocket(fd_);
#else
        close(fd_);
#endif
    }

    // Bind socket to a specified local ip and port.
    // Always sets SO_REUSEADDR + SO_REUSEPORT and SO_LINGER=0 before binding
    // so that the port is immediately reusable after a crash or force-quit,
    // regardless of whether the caller remembered to set these options first.
    void socket_bind(const std::string& ip, int port) const {
        socket_set_reuseaddr();
        socket_set_linger_zero();
        sockaddr_in local_addr;
        local_addr.sin_family      = AF_INET;
        local_addr.sin_port        = htons(port);
        local_addr.sin_addr.s_addr = inet_addr(ip.c_str());
        if (bind(fd_, (struct sockaddr*) &local_addr, sizeof(local_addr)) < 0) {
            throw std::runtime_error("Bind failed");
        }
    }

    // Listen for a connection. It is typically called from the server socket.
    void socket_listen(const int backlog = 16) const {
        if (listen(fd_, backlog) < 0) {
            throw std::runtime_error("Listen failed");
        }
    }

    // Connect the socket to a specified peer addr
    void socket_connect(const std::string& ip, int port) const {
        sockaddr_in peer_addr;
        peer_addr.sin_family      = AF_INET;
        peer_addr.sin_port        = htons(port);
        peer_addr.sin_addr.s_addr = inet_addr(ip.c_str());
        if (connect(fd_, (struct sockaddr*) &peer_addr, sizeof(peer_addr)) < 0)
            throw std::runtime_error("Connect failed");
    }

    // Accept connect from the client. It is typically called from the server socket.
#if defined(_WIN32) || defined(_WIN64)
    SOCKET
#else
    int
#endif
    socket_accept() const {
#if defined(_WIN32) || defined(_WIN64)
        SOCKET
#else
        int
#endif
        fd = accept(fd_, nullptr, nullptr);
#if defined(_WIN32) || defined(_WIN64)
        if (fd == INVALID_SOCKET)
            throw std::runtime_error("Accept failed");
#endif
        return fd;
    }

    // Allow the reuse of local addresses.
    // On Linux/Android, also sets SO_REUSEPORT so that a new socket can bind
    // to a port still in TIME_WAIT after a crash, without waiting ~60 seconds.
    void socket_set_reuseaddr() const {
#if defined(_WIN32) || defined(_WIN64)
        int enable = 1;
        if (setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, (char*) &enable, sizeof(enable)) < 0) {
            int err = WSAGetLastError();
            throw std::runtime_error("Reuseaddr failed");
        }
#else
        const int enable = 1;
        setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
        setsockopt(fd_, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int));
#endif
    }

    // Set SO_LINGER with a zero timeout so that close() sends a TCP RST
    // rather than going through the graceful FIN/FIN-ACK sequence.
    // This prevents the socket from entering TIME_WAIT after a crash,
    // allowing the port to be reused immediately on restart.
    void socket_set_linger_zero() const {
#if defined(_WIN32) || defined(_WIN64)
        LINGER lg{};
        lg.l_onoff  = 1;
        lg.l_linger = 0;
        setsockopt(fd_, SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&lg), sizeof(lg));
#else
        struct linger lg{};
        lg.l_onoff  = 1;
        lg.l_linger = 0;
        setsockopt(fd_, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
#endif
    }

    // Read data from the socket
    [[nodiscard]] std::string read_data(const size_t limit = BUFFER_SIZE) {
#ifndef __ANDROID__
        char buffer_[BUFFER_SIZE];
#endif
        BYTE_TYPE bytes_read =
#if defined(_WIN32) || defined(_WIN64)
            recv(fd_, buffer_, static_cast<int>(min(BUFFER_SIZE, limit)), 0);
#elif defined(__ANDROID__)
            read(fd_, static_cast<void* const>(buffer_), std::min(BUFFER_SIZE, limit));
#else
            read(fd_, buffer_, static_cast<int>(std::min(BUFFER_SIZE, limit)));
#endif
        if (bytes_read <= 0)
            return {};
        return std::string(buffer_, bytes_read);
    }

    // Write data to the socket
    void write_data(const std::string& buffer) {
        auto it = buffer.begin();
        do {
            it = write_helper(it, buffer.end());
        } while (it != buffer.end());
    }

    // Disable naggle algorithm. This allows socket to flush data immediately after calling write().
    void enable_no_delay() const {
#if defined(_WIN32) || defined(_WIN64)
        int enable = 1;
        if (setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, (char*) &enable, sizeof(enable)) < 0)
            throw std::runtime_error("No delay failed");
#else
        const int enable = 1;
        if (setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(int)) < 0)
            throw std::runtime_error("TCPSocket] TCP_NODELAY setsockopt failed");
#endif
    }

    /* accessors */
    [[maybe_unused]] [[nodiscard]] std::string local_address() const {
        sockaddr_in local_address;
        socklen_t   size = sizeof(local_address);
        getsockname(fd_, (struct sockaddr*) &local_address, &size);

#if defined(_WIN32) || defined(_WIN64)
        char local_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &local_address.sin_addr, local_ip, sizeof(local_ip));
#else
        char* local_ip = inet_ntoa(local_address.sin_addr);
#endif
        int local_port = ntohs(local_address.sin_port);
        return std::string(local_ip) + ":" + std::to_string(local_port);
    }

    [[nodiscard]] std::string peer_address() const {
        sockaddr_in peer_address;
        socklen_t   size = sizeof(peer_address);
        getpeername(fd_, (struct sockaddr*) &peer_address, &size);
#if defined(_WIN32) || defined(_WIN64)
        char peer_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &peer_address.sin_addr, peer_ip, sizeof(peer_ip));
#else
        char* peer_ip = inet_ntoa(peer_address.sin_addr);
#endif
        int peer_port = ntohs(peer_address.sin_port);
        return std::string(peer_ip) + ":" + std::to_string(peer_port);
    }

    [[nodiscard]] bool is_no_delay() const {
        int val = 0;
#if defined(_WIN32) || defined(_WIN64)
        int len = sizeof(val);
        getsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, (char*) &val, &len);
#else
        socklen_t len = sizeof(val);
        getsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &val, &len);
#endif
        return val != 0;
    }

private:
    [[nodiscard]] std::string::const_iterator write_helper(const std::string::const_iterator& begin,
                                                           const std::string::const_iterator& end) const {
        BYTE_TYPE bytes_written =
#if defined(_WIN32) || defined(_WIN64)
            send(fd_, &*begin, static_cast<int>(end - begin), 0);
        if (bytes_written < 0)
            throw std::runtime_error("Write failed");
#else
            write(fd_, &*begin, end - begin);
#endif
        return begin + bytes_written;
    }

    SOCKET_TYPE fd_;
    /* maximum size of a read */
    static constexpr size_t BUFFER_SIZE = 1024 * 1024;
#ifdef __ANDROID__
    char buffer_[BUFFER_SIZE];
#endif
};

} // namespace ILLIXR::network
