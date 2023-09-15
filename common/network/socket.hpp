/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef SOCKET_HPP
#define SOCKET_HPP

#include "address.hpp"
#include "exception.hpp"
#include "file_descriptor.hpp"
#include "timestamp.hpp"

#include <chrono>
#include <functional>
#include <linux/netfilter_ipv4.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

namespace ILLIXR {
/* class for network sockets (UDP, TCP, etc.) */
class Socket : public FileDescriptor {
protected:
    /* default constructor */
    Socket(const int domain, const int type)
        : FileDescriptor(system_call("socket", socket(domain, type, 0))) { }

    /* construct from file descriptor */
    Socket(FileDescriptor&& fd, const int domain, const int type)
        : FileDescriptor(std::move(fd)) {
        int       actual_value;
        socklen_t len;

        /* verify domain */
        len = getsockopt(SOL_SOCKET, SO_DOMAIN, actual_value);
        if ((len != sizeof(actual_value)) or (actual_value != domain)) {
            throw std::runtime_error("socket domain mismatch");
        }

        /* verify type */
        len = getsockopt(SOL_SOCKET, SO_TYPE, actual_value);
        if ((len != sizeof(actual_value)) or (actual_value != type)) {
            throw std::runtime_error("socket type mismatch");
        }
    }

    /* get and set socket option */
    template<typename option_type>
    socklen_t getsockopt(const int level, const int option, option_type& option_value) const {
        socklen_t optlen = sizeof(option_value);
        system_call("getsockopt", ::getsockopt(fd_num(), level, option, &option_value, &optlen));
        return optlen;
    }

    template<typename option_type>
    void setsockopt(const int level, const int option, const option_type& option_value) {
        system_call("setsockopt", ::setsockopt(fd_num(), level, option, &option_value, sizeof(option_value)));
    }

public:
    /* bind socket to a specified local address (usually to listen/accept) */
    void bind(const Address& address) {
        system_call("bind", ::bind(fd_num(), &address.to_sockaddr(), address.size()));
    }

    /* connect socket to a specified peer address */
    void connect(const Address& address) {
        system_call("connect", ::connect(fd_num(), &address.to_sockaddr(), address.size()));
    }

    /* accessors */
    Address local_address(void) const {
        return get_address("getsockname", getsockname);
    }

    Address peer_address(void) const {
        return get_address("getpeername", getpeername);
    }

    /* allow local address to be reused sooner, at the cost of some robustness */
    void set_reuseaddr(void) {
        setsockopt(SOL_SOCKET, SO_REUSEADDR, int(true));
    }

    /* get the local or peer address the socket is connected to */
    Address get_address(const std::string&                                    name_of_function,
                        const std::function<int(int, sockaddr*, socklen_t*)>& function) const {
        Address::raw address;
        socklen_t    size = sizeof(address);

        system_call(name_of_function, function(fd_num(), &address.as_sockaddr, &size));

        return Address(address, size);
    }

    void set_timeout(long int timeout_microseconds) {
        struct timeval read_timeout;
        read_timeout.tv_sec  = 0;
        read_timeout.tv_usec = timeout_microseconds;
        setsockopt(SOL_SOCKET, SO_RCVTIMEO, read_timeout);
    }
};

/* UDP socket */
class UDPSocket : public Socket {
public:
    UDPSocket()
        : Socket(AF_INET, SOCK_DGRAM) { }

    /* receive datagram and where it came from */
    std::pair<Address, std::string> recv_from(void) {
        static const ssize_t RECEIVE_MTU = 65536;

        /* receive source address and payload */
        Address::raw datagram_source_address;
        char         buffer[RECEIVE_MTU];

        socklen_t fromlen = sizeof(datagram_source_address);

        ssize_t recv_len = system_call(
            "recvfrom",
            ::recvfrom(fd_num(), buffer, sizeof(buffer), MSG_TRUNC, &datagram_source_address.as_sockaddr, &fromlen));

        if (recv_len > RECEIVE_MTU) {
            throw std::runtime_error("recvfrom (oversized datagram)");
        }

        register_read();

        return std::make_pair(Address(datagram_source_address, fromlen), std::string(buffer, recv_len));
    }

    /* receive datagram and where it came from */
    std::vector<std::pair<Address, std::string>> recv_from_nonblocking(int wait_time_ms) {
        static const ssize_t                         RECEIVE_MTU = 65536;
        std::vector<std::pair<Address, std::string>> result;

        std::chrono::time_point<std::chrono::steady_clock> start_time = std::chrono::steady_clock::now();
        std::chrono::duration<double>                      duration   = std::chrono::seconds(0);

        /* receive source address and payload */
        Address::raw datagram_source_address;
        char         buffer[RECEIVE_MTU];

        socklen_t fromlen = sizeof(datagram_source_address);
        ssize_t   recv_len;

        while (duration.count() * 1000 < wait_time_ms) {
            recv_len = recvfrom(fd_num(), buffer, sizeof(buffer), MSG_TRUNC, &datagram_source_address.as_sockaddr, &fromlen);

            if (recv_len > RECEIVE_MTU) {
                throw std::runtime_error("recvfrom (oversized datagram)");
            }

            if (recv_len >= 0) {
                result.push_back(std::make_pair(Address(datagram_source_address, fromlen), std::string(buffer, recv_len)));
                register_read();
            }

            duration = std::chrono::steady_clock::now() - start_time;
        }

        return result;
    }

    /* send datagram to specified address */
    void sendto(const Address& destination, const std::string& payload) {
        const ssize_t bytes_sent = system_call(
            "sendto", ::sendto(fd_num(), payload.data(), payload.size(), 0, &destination.to_sockaddr(), destination.size()));

        register_write();

        if (size_t(bytes_sent) != payload.size()) {
            throw std::runtime_error("datagram payload too big for sendto()");
        }
    }

    /* send datagram to connected address */
    void send(const std::string& payload) {
        const ssize_t bytes_sent = system_call("send", ::send(fd_num(), payload.data(), payload.size(), 0));

        register_write();

        if (size_t(bytes_sent) != payload.size()) {
            throw std::runtime_error("datagram payload too big for send()");
        }
    }

    /* turn on timestamps on receipt */
    void set_timestamps(void) {
        setsockopt(SOL_SOCKET, SO_TIMESTAMPNS, int(true));
    }
};

/* TCP socket */
class TCPSocket : public Socket {
protected:
public:
    /* constructor used by accept() and SecureSocket() */
    TCPSocket(FileDescriptor&& fd)
        : Socket(std::move(fd), AF_INET, SOCK_STREAM) { }

    TCPSocket()
        : Socket(AF_INET, SOCK_STREAM) { }

    /* enable tcp socket to immediately send data whenever it receives one */
    void enable_no_delay(void) {
        setsockopt(IPPROTO_TCP, TCP_NODELAY, int(true));
    }

    /* mark the socket as listening for incoming connections */
    void listen(const int backlog = 16) {
        system_call("listen", ::listen(fd_num(), backlog));
    }

    /* accept a new incoming connection */
    TCPSocket accept(void) {
        register_read();
        return TCPSocket(FileDescriptor(system_call("accept", ::accept(fd_num(), nullptr, nullptr))));
    }

    /* original destination of a DNAT connection */
    Address original_dest(void) const {
        Address::raw dstaddr;
        socklen_t    len = getsockopt(SOL_IP, SO_ORIGINAL_DST, dstaddr);

        return Address(dstaddr, len);
    }

    /* get the destination ip */
    Address get_destination(void) const {
        return get_address("getpeername", getpeername);
    }

    /* recreate socket */
    void recreate_socket(void) {
        // Recreate new socket
        int fd = system_call("socket", socket(AF_INET, SOCK_STREAM, 0));
        replace_fd(fd);
    }

    /* accept a new incoming connection and allocate the read_socket */
    void accept_nonblocking(TCPSocket* read_socket) {
        register_read();
        read_socket =
            new TCPSocket(FileDescriptor(system_call("accept4", ::accept4(fd_num(), nullptr, nullptr, SOCK_NONBLOCK))));
    }

    std::string recv_non_blocking() {
        char        buffer[FileDescriptor::BUFFER_SIZE];
        std::string result    = "";
        int         recv_size = recv(fd_num(), buffer, sizeof(buffer), 0);
        if (recv_size > 0) {
            register_read();
            return std::string(buffer, recv_size);
        } else {
            return "";
        }
    }
};

} // namespace ILLIXR

#endif /* SOCKET_HPP */
