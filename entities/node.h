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
#include <spdlog/spdlog.h>
#include <fstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <cerrno>
#include <cstring>


#include "../protocols/tcu.h"
#include "file.h"
#include "socket.h"

class Node {
public:
    Node();
    ~Node();

    inline tcu_pcb &get_pcb(){ return _pcb; };
    inline std::string get_path(){ return _file_path; };

    void set_port(uint16_t port);
    void set_dest(in_addr ip, uint16_t port);
    void set_path(std::string& path);

    void send_packet(unsigned char* buff, size_t length);    // Function to send packet
    void receive_packet();                                   // Function to receive packet

    void save_file(const File& file);

    void send_text(const std::string& message);
    void send_file(const std::string& path);

    void start_receiving();
    void stop_receiving();

    void start_keep_alive();
    void stop_keep_alive();

    void wait_for_ack();

    void fsm_process(unsigned char* buff, size_t length);

    void process_tcu_conn_req(tcu_packet packet);
    void process_tcu_conn_ack(tcu_packet packet);
    void process_tcu_disconn_req(tcu_packet packet);
    void process_tcu_disconn_ack(tcu_packet packet);
    void process_tcu_ka_req(tcu_packet packet);
    void process_tcu_ka_ack(tcu_packet packet);
    void process_tcu_single_text(tcu_packet packet);
    void process_tcu_more_frag_text(tcu_packet packet);
    void process_tcu_last_frag_text(tcu_packet packet);
    void process_tcu_single_file(tcu_packet packet);
    void process_tcu_more_frag_file(tcu_packet packet);
    void process_tcu_last_frag_file(tcu_packet packet);
    void process_tcu_positive_ack(tcu_packet packet);
    void process_tcu_negative_ack(tcu_packet packet);

    void send_tcu_conn_req();
    void send_tcu_conn_ack();
    void send_tcu_disconn_req();
    void send_tcu_disconn_ack();
    void send_keep_alive_req();
    void send_keep_alive_ack();
    void send_tcu_negative_ack(uint16_t seq_number);
    void send_tcu_positive_ack(uint16_t seq_number);

private:
    /* Socket control block */
    Socket _socket;

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

    /* File saving params*/
    std::string _file_path;
};
