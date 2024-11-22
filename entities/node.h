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
#include "../types/uint24_t.h"
#include "file.h"
#include "socket.h"

class Node {
public:
    Node();
    ~Node();

    /* Getters */
    inline tcu_pcb &get_pcb(){ return _pcb; };
    inline std::string get_path(){ return _file_path; };

    /* Setters */
    void set_port(uint16_t port);
    void set_dest(in_addr ip, uint16_t port);
    void set_path(std::string& path);
    void set_max_frag_size(size_t size);
    void set_window_size(uint24_t size);
    void set_dynamic_window();

    /* Abstract methods */
    void send_packet(unsigned char* buff, size_t length);    // Function to send packet
    void receive_packet();                                   // Function to receive packet

    /* Concrete methods */
    void send_text(const std::string& message);
    void send_file(const std::string& path);
    void send_window();

    /* Process information methods */
    void assemble_text();
    void assemble_file();
    void save_file(const File& file);

    /* Thread methods */
    void start_receiving();
    void stop_receiving();

    void start_keep_alive();
    void stop_keep_alive();

    void start_sending();
    void stop_sending();

    void wait_for_ack();

    /* FSM methods */
    void fsm_process(unsigned char* buff, size_t length);

    /* Processing */
    void process_tcu_conn_req(tcu_packet packet);
    void process_tcu_conn_ack(tcu_packet packet);

    void process_tcu_disconn_req(tcu_packet packet);
    void process_tcu_disconn_ack(tcu_packet packet);

    void process_tcu_ka_req(tcu_packet packet);
    void process_tcu_ka_ack(tcu_packet packet);

    void process_tcu_single_text(tcu_packet packet);
    void process_tcu_single_file(tcu_packet packet);

    void process_tcu_more_frag_text(tcu_packet packet);
    void process_tcu_last_wind_frag_text(tcu_packet packet);
    void process_tcu_last_frag_text(tcu_packet packet);

    void process_tcu_more_frag_file(tcu_packet packet);
    void process_tcu_last_wind_frag_file(tcu_packet packet);
    void process_tcu_last_frag_file(tcu_packet packet);

    void process_tcu_positive_ack(tcu_packet packet);
    void process_tcu_negative_ack(tcu_packet packet);

    /* Sending */
    void send_tcu_conn_req();
    void send_tcu_conn_ack();

    void send_tcu_disconn_req();
    void send_tcu_disconn_ack();

    void send_keep_alive_req();
    void send_keep_alive_ack();

    void send_tcu_negative_ack(uint24_t seq_number);
    void send_tcu_positive_ack(uint24_t seq_number);

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

    /* Sending thread params */
    void send_loop();
    std::atomic<bool> _send_running;
    std::thread _send_thread;

    /* Sending params */
    std::map<uint24_t, tcu_packet> _send_packets;
    std::chrono::steady_clock::time_point _send_time;

    void dynamic_window_size();
    bool _dynamic_window;
    uint24_t _window_size;

    size_t _max_frag_size;
    uint24_t _seq_num;
    uint24_t _total_packets;

    /* Receiving params */
    std::map<uint24_t, tcu_packet> _received_packets;
    std::map<uint24_t, tcu_packet> _error_packets;

    std::chrono::steady_clock::time_point _receive_start_time_text;
    std::chrono::steady_clock::time_point _receive_start_time_file;

    /* File saving params*/
    std::string _file_path;
};
