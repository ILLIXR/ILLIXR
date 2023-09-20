/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef ADDRESS_HPP
#define ADDRESS_HPP

#include "exception.hpp"
#include "ezio.hpp"
#include "util.hpp"

#include <cassert>
#include <cstring>
#include <functional>
#include <memory>
#include <netdb.h>
#include <netinet/in.h>
#include <string>
#include <utility>

namespace ILLIXR {

/* error category for getaddrinfo and getnameinfo */
class gai_error_category : public std::error_category {
public:
    const char* name(void) const noexcept override {
        return "gai_error_category";
    }

    std::string message(const int return_value) const noexcept override {
        return gai_strerror(return_value);
    }
};

/* Address class for IPv4/v6 addresses */
class Address {
public:
    typedef union {
        sockaddr         as_sockaddr;
        sockaddr_storage as_sockaddr_storage;
    } raw;

private:
    socklen_t size_;
    raw       addr_;

    /* private constructor given ip/host, service/port, and optional hints */
    Address(const std::string& node, const std::string& service, const addrinfo& hints)
        : size_()
        , addr_() {
        /* prepare for the answer */
        addrinfo* resolved_address;

        /* look up the name or names */
        const int gai_ret = getaddrinfo(node.c_str(), service.c_str(), &hints, &resolved_address);
        if (gai_ret) {
            std::string explanation = "getaddrinfo(" + node + ":" + service;
            if (hints.ai_flags | (AI_NUMERICHOST | AI_NUMERICSERV)) {
                explanation += ", numeric";
            }
            explanation += ")";
            throw tagged_error(gai_error_category(), explanation, gai_ret);
        }

        /* if success, should always have at least one entry */
        if (not resolved_address) {
            throw std::runtime_error("getaddrinfo returned successfully but with no results");
        }

        /* put resolved_address in a wrapper so it will get freed if we have to throw an exception */
        std::unique_ptr<addrinfo, std::function<void(addrinfo*)>> wrapped_address{resolved_address, [](addrinfo* x) {
                                                                                      freeaddrinfo(x);
                                                                                  }};

        /* assign to our private members (making sure size fits) */
        *this = Address(*wrapped_address->ai_addr, wrapped_address->ai_addrlen);
    }

public:
    /* constructors */
    Address()
        : Address("0", 0) { }

    Address(const raw& addr, const size_t size)
        : Address(addr.as_sockaddr, size) { }

    Address(const sockaddr& addr, const size_t size)
        : size_(size)
        , addr_() {
        /* make sure proposed sockaddr can fit */
        if (size > sizeof(addr_)) {
            throw std::runtime_error("invalid sockaddr size");
        }

        memcpy(&addr_, &addr, size_);
    }

    Address(const sockaddr_in& addr)
        : size_(sizeof(sockaddr_in))
        , addr_() {
        assert(size_ <= sizeof(addr_));

        memcpy(&addr_, &addr, size_);
    }

    /* construct by resolving host name and service name */
    Address(const std::string& hostname, const std::string& service)
        : size_()
        , addr_() {
        addrinfo hints;
        zero(hints);
        hints.ai_family = AF_INET;

        *this = Address(hostname, service, hints);
    }

    /* construct with numerical IP address and numeral port number */
    Address(const std::string& ip, const uint16_t port)
        : size_()
        , addr_() {
        /* tell getaddrinfo that we don't want to resolve anything */
        addrinfo hints;
        zero(hints);
        hints.ai_family = AF_INET;
        hints.ai_flags  = AI_NUMERICHOST | AI_NUMERICSERV;

        *this = Address(ip, std::to_string(port), hints);
    }

    /* accessors */
    std::pair<std::string, uint16_t> ip_port(void) const {
        char ip[NI_MAXHOST], port[NI_MAXSERV];

        const int gni_ret =
            getnameinfo(&to_sockaddr(), size_, ip, sizeof(ip), port, sizeof(port), NI_NUMERICHOST | NI_NUMERICSERV);
        if (gni_ret) {
            throw tagged_error(gai_error_category(), "getnameinfo", gni_ret);
        }

        return std::make_pair(ip, myatoi(port));
    }

    std::string ip(void) const {
        return ip_port().first;
    }

    uint16_t port(void) const {
        return ip_port().second;
    }

    std::string str(const std::string port_separator) const {
        const auto ip_and_port = ip_port();
        return ip_and_port.first + port_separator + std::to_string(ip_and_port.second);
    }

    socklen_t size(void) const {
        return size_;
    }

    const sockaddr& to_sockaddr(void) const {
        return addr_.as_sockaddr;
    }

    /* comparisons */
    bool operator==(const Address& other) const {
        return 0 == memcmp(&addr_, &other.addr_, size_);
    }

    bool operator<(const Address& other) const {
        return (memcmp(&addr_, &other.addr_, sizeof(addr_)) < 0);
    }

    /* generate carrier-grade NAT address */
    Address cgnat(const uint8_t last_octet) {
        return Address("100.64.0." + std::to_string(last_octet), 0);
    }
};

} // namespace ILLIXR

#endif /* ADDRESS_HPP */
