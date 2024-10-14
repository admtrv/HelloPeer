/*
 *  node.cpp
 */

#include "node.h"

Node::Node() : _socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP), _receive_running(false), _keep_alive_running(false)
{
    _pcb.new_phase(TCU_PHASE_INITIALIZE);

    if (_socket.get_socket() < 0)
    {
        exit(EXIT_FAILURE);
    }

    if (_socket.set_non_blocking() < 0)
    {
        exit(EXIT_FAILURE);
    }
}

Node::~Node()
{
    _pcb.new_phase(TCU_PHASE_CLOSED);

    stop_receiving();
    stop_keep_alive();
    _socket.close_socket();
}

void Node::start_receiving()
{
    if(!_receive_running)
    {
        _receive_running = true;
        _receive_thread = std::thread(&Node::receive_loop, this);
    }
}

void Node::stop_receiving()
{
    _socket.close_socket();

    _receive_running = false;

    if (_receive_thread.joinable())
    {
        _receive_thread.join();
    }
}

void Node::receive_loop()
{
    while (_receive_running)
    {
        if (_socket.get_socket() == -1)
        {
            break;
        }

        receive_packet();
    }
}

void Node::start_keep_alive()
{
    if (_keep_alive_thread.joinable())
    {
        _keep_alive_thread.join();
        _keep_alive_thread = std::thread();
    }

    _keep_alive_running = true;
    _keep_alive_thread = std::thread(&Node::keep_alive_loop, this);
}

void Node::stop_keep_alive()
{
    _keep_alive_running = false;
    if (_keep_alive_thread.joinable())
    {
        _keep_alive_thread.join();
        _keep_alive_thread = std::thread();
    }
}

void Node::keep_alive_loop()
{
    while (_keep_alive_running)
    {
        bool ack_received = false;

        // Waiting for TCU_KA_TIMEOUT before activity check
        auto start_time = std::chrono::steady_clock::now();
        while (_keep_alive_running && std::chrono::steady_clock::now() - start_time < std::chrono::seconds(TCU_KA_TIMEOUT))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (!_keep_alive_running)
        {
            break;
        }

        // Check activity
        for (int i = 0; i < TCU_KA_ATTEMPT_COUNT && _keep_alive_running; i++)
        {
            spdlog::info("[Node::keep_alive_loop] sending tcu keep-alive request {}", i + 1);
            send_keep_alive_req();

            // Wait for acknowledgment for TCU_KA_ATTEMPT_INTERVAL
            auto attempt_start = std::chrono::steady_clock::now();
            while (_keep_alive_running && std::chrono::steady_clock::now() - attempt_start < std::chrono::seconds(TCU_KA_ATTEMPT_INTERVAL))
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            if (!_keep_alive_running)
            {
                break;
            }

            if (_pcb.is_activity_recent())
            {
                ack_received = true;
                break;
            }
        }

        // If not get_socket acknowledgment, close connection
        if (!ack_received)
        {
            spdlog::info("[Node::keep_alive_loop] no tcu keep-alive acknowledgment, closing connection");

            _keep_alive_running = false;
            _pcb.new_phase(TCU_PHASE_HOLDOFF);

            std::cout << "connection closed" << std::endl;
        }
    }
}

void Node::receive_packet()
{
    char temp_buff[2048];

    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(_socket.get_socket(), &read_fds);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 100000;

    int result = select(_socket.get_socket() + 1, &read_fds, nullptr, nullptr, &timeout);

    if (result > 0 && FD_ISSET(_socket.get_socket(), &read_fds))
    {
        struct sockaddr_in src_addr;
        socklen_t src_addr_len = sizeof(src_addr);

        ssize_t num_bytes = recvfrom(_socket.get_socket(), temp_buff, sizeof(temp_buff), 0, (struct sockaddr*)&src_addr, &src_addr_len);

        if (num_bytes < 0)
        {
            perror("recvfrom");
        }
        else
        {
            spdlog::info("[Node::receive_packet] received {} bytes from {}:{}", num_bytes, inet_ntoa(src_addr.sin_addr), ntohs(src_addr.sin_port));

            _pcb.update_last_activity();
            fsm_process(reinterpret_cast<unsigned char*>(temp_buff), static_cast<size_t>(num_bytes));
        }
    }
    else if (result < 0)
    {
        perror("select");
    }
}

void Node::send_packet(unsigned char* buff, size_t length)
{
    ssize_t num_bytes = sendto(_socket.get_socket(), buff, length, 0, reinterpret_cast<struct sockaddr*>(&_pcb.dest_addr), sizeof(_pcb.dest_addr));

    if (num_bytes < 0)
    {
        perror("sendto");
    }
    else
    {
        spdlog::info("[Node::send_packet] sent {} bytes to {}:{}", num_bytes, inet_ntoa(_pcb.dest_addr.sin_addr), ntohs(_pcb.dest_addr.sin_port));
        _pcb.update_last_activity();
    }
}

void Node::fsm_process(unsigned char* buff, size_t length)
{
    tcu_packet packet = tcu_packet::from_buff(buff);

    uint16_t flags = packet.header.flags;

    switch (flags)
    {
        case TCU_HDR_FLAG_SYN:
            process_tcu_conn_req(packet);
            break;
        case (TCU_HDR_FLAG_SYN | TCU_HDR_FLAG_ACK):
            process_tcu_conn_ack(packet);
            break;
        case TCU_HDR_FLAG_FIN:
            process_tcu_disconn_req(packet);
            break;
        case (TCU_HDR_FLAG_FIN | TCU_HDR_FLAG_ACK):
            process_tcu_disconn_ack(packet);
            break;
        case TCU_HDR_FLAG_KA:
            process_tcu_ka_req(packet);
            break;
        case (TCU_HDR_FLAG_KA | TCU_HDR_FLAG_ACK):
            process_tcu_ka_ack(packet);
            break;
        default:
            spdlog::error("[Node::fsm_process] unknown flags {}", flags);
            break;
    }
}

void Node::process_tcu_conn_req(tcu_packet packet)
{
    if (_pcb.phase <= TCU_PHASE_INITIALIZE)
    {
        spdlog::info("[Node::process_tcu_conn_req] received tcu connection request");
        _pcb.new_phase(TCU_PHASE_CONNECT);
        start_keep_alive();
        std::cout << "connected" << std::endl;
        send_tcu_conn_ack();
    }
    else
    {
        spdlog::warn("[Node::process_tcu_conn_req] unexpected phase {}", _pcb.phase);
    }
}

void Node::process_tcu_conn_ack(tcu_packet packet)
{
    if (_pcb.phase == TCU_PHASE_CONNECT)
    {
        spdlog::info("[Node::process_tcu_conn_ack] received tcu connection acknowledgment");
        _pcb.new_phase(TCU_PHASE_NETWORK);
        start_keep_alive();
        std::cout << "connected" << std::endl;
    }
    else
    {
        spdlog::warn("[Node::process_tcu_conn_ack] unexpected phase {}", _pcb.phase);
    }
}

void Node::process_tcu_disconn_req(tcu_packet packet)
{
    if (_pcb.phase >= TCU_PHASE_CONNECT && _pcb.phase <= TCU_PHASE_NETWORK)
    {
        spdlog::info("[Node::process_tcu_disconn_req] received tcu disconnection request");
        _pcb.new_phase(TCU_PHASE_DISCONNECT);
        stop_keep_alive();
        std::cout << "disconnected" << std::endl;
        send_tcu_disconn_ack();
    }
    else
    {
        spdlog::warn("[Node::process_tcu_disconn_req] unexpected phase {}", _pcb.phase);
    }
}

void Node::process_tcu_disconn_ack(tcu_packet packet)
{
    if (_pcb.phase == TCU_PHASE_DISCONNECT)
    {
        spdlog::info("[Node::process_tcu_disconn_ack] received tcu disconnection acknowledgment");
        _pcb.new_phase(TCU_PHASE_HOLDOFF);
        stop_keep_alive();
        std::cout << "disconnected" << std::endl;
    }
    else
    {
        spdlog::warn("[Node::process_tcu_disconn_ack] unexpected phase {}", _pcb.phase);
    }
}

void Node::process_tcu_ka_req(tcu_packet packet)
{
    spdlog::info("[Node::process_tcu_ka_req] received tcu keep-alive request");
    send_keep_alive_ack();
}

void Node::process_tcu_ka_ack(tcu_packet packet)
{
    spdlog::info("[Node::process_tcu_ka_ack] received tcu keep-alive acknowledgment");
    _pcb.update_last_activity();
}

void Node::send_tcu_conn_req()
{
    if (_pcb.phase <= TCU_PHASE_INITIALIZE)
    {
        spdlog::info("[Node::send_tcu_conn_req] sending tcu connection request");

        // SYN
        tcu_packet packet;
        packet.header.flags = TCU_HDR_FLAG_SYN;
        packet.header.length = 0;
        packet.header.seq_number = 0;
        packet.calculate_crc();

        send_packet(packet.to_buff(), TCU_HDR_LEN);

        _pcb.new_phase(TCU_PHASE_CONNECT);
    }
    else
    {
        spdlog::warn("[Node::send_tcu_conn_req] unexpected phase {}", _pcb.phase);
    }
}

void Node::send_tcu_conn_ack()
{
    if (_pcb.phase == TCU_PHASE_CONNECT)
    {
        spdlog::info("[Node::send_tcu_conn_ack] sending tcu connection acknowledgment");

        // SYN + ACK
        tcu_packet packet;
        packet.header.flags = TCU_HDR_FLAG_SYN | TCU_HDR_FLAG_ACK;
        packet.header.length = 0;
        packet.header.seq_number = 0;
        packet.calculate_crc();

        send_packet(packet.to_buff(), TCU_HDR_LEN);

        _pcb.new_phase(TCU_PHASE_NETWORK);
    }
    else
    {
        spdlog::warn("[Node::send_tcu_conn_ack] unexpected phase {}", _pcb.phase);
    }
}

void Node::send_tcu_disconn_req()
{
    if (_pcb.phase >= TCU_PHASE_CONNECT && _pcb.phase <= TCU_PHASE_NETWORK)
    {
        spdlog::info("[Node::send_tcu_disconn_req] sending tcu disconnection request");

        // FIN
        tcu_packet packet;
        packet.header.flags = TCU_HDR_FLAG_FIN;
        packet.header.length = 0;
        packet.header.seq_number = 0;
        packet.calculate_crc();

        send_packet(packet.to_buff(), TCU_HDR_LEN);

        _pcb.new_phase(TCU_PHASE_DISCONNECT);
    }
    else
    {
        spdlog::warn("[Node::send_tcu_disconn_req] unexpected phase {}", _pcb.phase);
    }
}

void Node::send_tcu_disconn_ack()
{
    if (_pcb.phase == TCU_PHASE_DISCONNECT)
    {
        spdlog::info("[Node::send_tcu_disconn_ack] sending tcu disconnection acknowledgment");

        // FIN + ACK
        tcu_packet packet;
        packet.header.flags = TCU_HDR_FLAG_FIN | TCU_HDR_FLAG_ACK;
        packet.header.length = 0;
        packet.header.seq_number = 0;
        packet.calculate_crc();

        send_packet(packet.to_buff(), TCU_HDR_LEN);

        _pcb.new_phase(TCU_PHASE_HOLDOFF);
    }
    else
    {
        spdlog::warn("[Node::send_tcu_disconn_ack] unexpected phase {}", _pcb.phase);
    }
}

void Node::send_keep_alive_req()
{
    // KA
    tcu_packet packet;
    packet.header.flags = TCU_HDR_FLAG_KA;
    packet.header.length = 0;
    packet.header.seq_number = 0;
    packet.calculate_crc();

    send_packet(packet.to_buff(), TCU_HDR_LEN);
}

void Node::send_keep_alive_ack()
{
    if (_pcb.phase >= TCU_PHASE_CONNECT && _pcb.phase <= TCU_PHASE_NETWORK)
    {
        spdlog::info("[Node::send_keep_alive_ack] sending tcu keep-alive acknowledgment");

        // KA + ACK
        tcu_packet packet;
        packet.header.flags = TCU_HDR_FLAG_KA | TCU_HDR_FLAG_ACK;
        packet.header.length = 0;
        packet.header.seq_number = 0;
        packet.calculate_crc();

        send_packet(packet.to_buff(), TCU_HDR_LEN);
    }
    else
    {
        spdlog::warn("[Node::send_keep_alive_ack] unexpected phase {}", _pcb.phase);
    }

}