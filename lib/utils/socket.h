#ifndef LUMINE_SOCKET_H
#define LUMINE_SOCKET_H

#include "base/common.h"

namespace faas::utils {

// Return sockfd on success, and return -1 on error
    int UnixDomainSocketConnect(std::string_view path);

    int TcpSocketBindAndListen(std::string_view addr, uint16_t port, int backlog = 4);

    int TcpSocketConnect(std::string_view addr, uint16_t port);

    int Tcp6SocketBindAndListen(std::string_view ip, uint16_t port, int backlog = 4);

    int Tcp6SocketConnect(std::string_view ip, uint16_t port);

// Will use `getaddrinfo` to resolve IP address if necessary
    bool FillTcpSocketAddr(struct sockaddr_in *addr, std::string_view host_or_ip, uint16_t port);

}  // namespace faas

#endif //LUMINE_SOCKET_H