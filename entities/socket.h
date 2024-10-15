/*
 * socket.h
 */

#pragma once

#include <sys/socket.h>
#include <unistd.h>
#include <stdexcept>
#include <fcntl.h>

class Socket {
public:
    Socket(int domain, int type, int protocol);
    ~Socket();

    int set_non_blocking() const;

    int get_socket() const;
    void close_socket();

    /* Copy protection */
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

private:
    int _sock_desc;
};
