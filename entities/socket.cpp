/*
 * socket.cpp
 */

#include "socket.h"

Socket::Socket(int domain, int type, int protocol) : _sock_desc(-1)
{
    _sock_desc = socket(domain, type, protocol);
    if (_sock_desc < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }
}

Socket::~Socket()
{
    close_socket();
}

int Socket::get_socket() const
{
    return _sock_desc;
}

void Socket::close_socket()
{
    if (_sock_desc != -1)
    {
        close(_sock_desc);
        _sock_desc = -1;
    }
}

int Socket::set_non_blocking() const
{
    int flags = fcntl(_sock_desc, F_GETFL, 0);
    if (flags == -1)
    {
        perror("fcntl");
        return -1;
    }
    if (fcntl(_sock_desc, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        perror("fcntl");
        return -1;
    }
    return 0;
}

