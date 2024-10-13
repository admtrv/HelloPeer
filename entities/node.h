/*
 *  node.h
 */

#pragma once

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <arpa/inet.h>
#include <fcntl.h>
#include <condition_variable>

#include "../protocols/tcu.h"

class Node {
public:
    Node();
    ~Node();

    inline void set_port(uint16_t port)
    {
        _pcb.src_port = port;
        sockaddr_in local_addr{};
        local_addr.sin_family = AF_INET;
        local_addr.sin_port = htons(_pcb.src_port);
        local_addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(_sock_desc, reinterpret_cast<struct sockaddr*>(&local_addr), sizeof(local_addr)) < 0)
        {
            perror("bind");
            exit(EXIT_FAILURE);
        }

        start_receiving();
    }

    inline void set_dest(in_addr ip, uint16_t port)
    {
        _pcb.dest_ip = ip;
        _pcb.dest_port = port;
        _pcb.dest_addr.sin_family = AF_INET;
        _pcb.dest_addr.sin_port = htons(_pcb.dest_port);
        _pcb.dest_addr.sin_addr = _pcb.dest_ip;
    }

    void send_packet(unsigned char* buff, size_t length);    // Function to send packet
    void receive_packet();                                   // Function to receive packet

    void start_receiving();
    void stop_receiving();

    void start_keep_alive();
    void stop_keep_alive();

    void fsm_process(unsigned char* buff, size_t length);

    void process_tcu_conn_req(tcu_packet packet);
    void process_tcu_conn_ack(tcu_packet packet);
    void process_tcu_disconn_req(tcu_packet packet);
    void process_tcu_disconn_ack(tcu_packet packet);
    void process_tcu_ka_req(tcu_packet packet);
    void process_tcu_ka_ack(tcu_packet packet);


    void send_tcu_conn_req();
    void send_tcu_conn_ack();
    void send_tcu_disconn_req();
    void send_tcu_disconn_ack();
    void send_keep_alive();
    void send_keep_alive_ack();

private:
    /* Socket params */
    int _sock_desc;

    /* TCU protocol control block */
    tcu_pcb _pcb;

    /* Receiving thread params */
    void receive_loop();
    std::atomic<bool> _receive_running;
    std::thread _receive_thread;

    /* Keep-Alive thread params */
    void keep_alive_loop();
    std::atomic<bool> _keep_alive_running;
    std::thread _keep_alive_thread;
};
