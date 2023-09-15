/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef SOCKETINSTANCE_HPP
#define SOCKETINSTANCE_HPP

#include "address.hpp"
#include "exception.hpp"
#include "socket.hpp"

namespace ILLIXR {
/* class for network sockets (UDP, TCP, etc.) */
class SocketInstance {
public:
    static UDPSocket socket;
    static Address   client_addr;
};

UDPSocket SocketInstance::socket;
Address   SocketInstance::client_addr;

} // namespace ILLIXR

#endif /* SOCKETINSTANCE_HPP */
