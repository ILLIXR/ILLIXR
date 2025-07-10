#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

using namespace std;

namespace ILLIXR::network {

class TCPSocket {
public:
    TCPSocket() {
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
    }

    [[maybe_unused]] explicit TCPSocket(int fd) {
        fd_ = fd;
    }

    // Destructor
    // Close the file descriptor
    ~TCPSocket() {
        close(fd_);
    }

    // Bind socket to a specified local ip and port
    void socket_bind(const string& ip, int port) const {
        sockaddr_in local_addr;
        local_addr.sin_family      = AF_INET;
        local_addr.sin_port        = htons(port);
        local_addr.sin_addr.s_addr = inet_addr(ip.c_str());
        bind(fd_, (struct sockaddr*) &local_addr, sizeof(local_addr));
    }

    // Listen for a connection. It is typically called from the server socket.
    void socket_listen(const int backlog = 16) const {
        listen(fd_, backlog);
    }

    // Connect the socket to a specified peer addr
    void socket_connect(const string& ip, int port) const {
        sockaddr_in peer_addr;
        peer_addr.sin_family      = AF_INET;
        peer_addr.sin_port        = htons(port);
        peer_addr.sin_addr.s_addr = inet_addr(ip.c_str());
        connect(fd_, (struct sockaddr*) &peer_addr, sizeof(peer_addr));
    }

    // Accept connect from the client. It is typically called from the server socket.
    int socket_accept() const {
        int fd = accept(fd_, nullptr, nullptr);
        return fd;
    }

    // Allow the reuse of local addresses
    void socket_set_reuseaddr() const {
        const int enable = 1;
        setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
    }

    // Read data from the socket
    [[nodiscard]] string read_data(const size_t limit = BUFFER_SIZE) const {
        char    buffer[BUFFER_SIZE];
        ssize_t bytes_read = read(fd_, buffer, min(BUFFER_SIZE, limit));
        if (bytes_read == -1) {
            throw runtime_error("Error reading from socket");
        } else if (bytes_read == 0) {
            return ""; // EOF
        }
        return string(buffer, bytes_read);
    }

    // Write data to the socket
    void write_data(const string& buffer) {
        auto it = buffer.begin();
        do {
            it = write_helper(it, buffer.end());
        } while (it != buffer.end());
    }

    // Disable naggle algorithm. This allows socket to flush data immediately after calling write().
    void enable_no_delay() const {
        const int enable = 1;
        setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(int));
    }

    /* accessors */
    [[maybe_unused]] [[nodiscard]] string local_address() const {
        struct sockaddr_in local_address;
        socklen_t          size = sizeof(local_address);
        getsockname(fd_, (struct sockaddr*) &local_address, &size);
        char* local_ip   = inet_ntoa(local_address.sin_addr);
        int   local_port = ntohs(local_address.sin_port);
        return string(local_ip) + ":" + to_string(local_port);
    }

    [[nodiscard]] string peer_address() const {
        struct sockaddr_in peer_address;
        socklen_t          size = sizeof(peer_address);
        getpeername(fd_, (struct sockaddr*) &peer_address, &size);
        char* peer_ip   = inet_ntoa(peer_address.sin_addr);
        int   peer_port = ntohs(peer_address.sin_port);
        return string(peer_ip) + ":" + to_string(peer_port);
    }

private:
    [[nodiscard]] string::const_iterator write_helper(const string::const_iterator& begin,
                                                      const string::const_iterator& end) const {
        ssize_t bytes_written = write(fd_, &*begin, end - begin);
        return begin + bytes_written;
    }

    int fd_;
    /* maximum size of a read */
    static constexpr size_t BUFFER_SIZE = 1024 * 1024;
};

} // namespace ILLIXR::network
