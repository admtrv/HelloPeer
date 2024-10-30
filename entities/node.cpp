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

    const char* home_dir = std::getenv("HOME");
    if (home_dir != nullptr)
    {
        _file_path = std::string(home_dir) + "/recv";
    }
    else
    {
        _file_path = "./recv";
    }
}

Node::~Node()
{
    _pcb.new_phase(TCU_PHASE_CLOSED);

    stop_receiving();
    stop_keep_alive();
    _socket.close_socket();
}

void Node::set_port(uint16_t port)
{
    _pcb.src_port = port;
    sockaddr_in local_addr{};
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(_pcb.src_port);
    local_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(_socket.get_socket(), reinterpret_cast<struct sockaddr*>(&local_addr), sizeof(local_addr)) < 0)
    {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if (_pcb.dest_port != 0 && _pcb.dest_ip.s_addr != 0)
    {
        start_receiving();
    }
}

void Node::set_dest(in_addr ip, uint16_t port)
{
    _pcb.dest_ip = ip;
    _pcb.dest_port = port;
    _pcb.dest_addr.sin_family = AF_INET;
    _pcb.dest_addr.sin_port = htons(_pcb.dest_port);
    _pcb.dest_addr.sin_addr = _pcb.dest_ip;

    if (_pcb.src_port != 0)
    {
        start_receiving();
    }
}

void Node::set_path(std::string& path)
{
    struct stat info;

    if (stat(_file_path.c_str(), &info) != 0)
    {
        if (mkdir(_file_path.c_str(), 0777) != 0)
        {
            spdlog::error("[Node::set_path] cannot create directory {}", _file_path);
            std::cout << "invalid path" << std::endl;
            return;
        }
    }
    else if (!(info.st_mode & S_IFDIR))
    {
        spdlog::error("[Node::set_path] {} not directory", _file_path);
        std::cout << "invalid path" << std::endl;
        return;
    }

    _file_path = path;
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

        // Waiting for TCU_ACTIVITY_TIMEOUT_INTERVAL before activity check
        auto start_time = std::chrono::steady_clock::now();
        while (_keep_alive_running && std::chrono::steady_clock::now() - start_time < std::chrono::seconds(TCU_ACTIVITY_TIMEOUT_INTERVAL))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (!_keep_alive_running)
        {
            break;
        }

        // Check activity by sending TCU_ACTIVITY_ATTEMPT_COUNT keep-alive messages
        for (int i = 0; i < TCU_ACTIVITY_ATTEMPT_COUNT && _keep_alive_running; i++)
        {
            spdlog::info("[Node::keep_alive_loop] sending tcu keep-alive request {}", i + 1);
            send_keep_alive_req();

            // Wait for acknowledgment for TCU_ACTIVITY_ATTEMPT_INTERVAL
            auto attempt_start = std::chrono::steady_clock::now();
            while (_keep_alive_running && std::chrono::steady_clock::now() - attempt_start < std::chrono::seconds(TCU_ACTIVITY_ATTEMPT_INTERVAL))
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

        // If not get acknowledgment, close connection
        if (!ack_received)
        {
            spdlog::info("[Node::keep_alive_loop] no tcu keep-alive acknowledgment, closing connection");

            _keep_alive_running = false;
            _pcb.new_phase(TCU_PHASE_HOLDOFF);

            std::cout << "destination node down, connection closed" << std::endl;
        }
        else
        {
            _pcb.is_active.store(false, std::memory_order_relaxed);
        }
    }
}

void Node::receive_packet()
{
    char temp_buff[2048];

    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(_socket.get_socket(), &read_fds);

    struct timeval timeout{};
    timeout.tv_sec = 0;
    timeout.tv_usec = 100000;

    int result = select(_socket.get_socket() + 1, &read_fds, nullptr, nullptr, &timeout);

    if (result > 0 && FD_ISSET(_socket.get_socket(), &read_fds))
    {
        struct sockaddr_in src_addr{};
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
    }
}

void Node::wait_for_ack()
{
    spdlog::info("[Node::wait_for_ack] waiting for tcu acknowledgment");

    auto start_time = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(TCU_ACTIVITY_ATTEMPT_INTERVAL))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (_pcb.is_activity_recent())
        {
            break;
        }
    }

    if (!_pcb.is_activity_recent())
    {
        spdlog::info("[Node::wait_for_ack] no tcu acknowledgment, closing connection");
        _pcb.new_phase(TCU_PHASE_HOLDOFF);
        std::cout << "destination node down, connection closed" << std::endl;
    }
}

void Node::save_file(const File& file)
{
    struct stat info;

    if (stat(_file_path.c_str(), &info) != 0)
    {
        if (mkdir(_file_path.c_str(), 0777) != 0)
        {
            spdlog::error("[Node::save_file] cannot create directory {}", _file_path);
            std::cout << "invalid path" << std::endl;
            return;
        }
    }
    else if (!(info.st_mode & S_IFDIR))
    {
        spdlog::error("[Node::save_file] {} not directory", _file_path);
        std::cout << "invalid path" << std::endl;
        return;
    }

    std::string save_path = _file_path + '/' + file.get_header().file_name;

    std::ofstream outfile(save_path, std::ios::binary);
    if (!outfile)
    {
        spdlog::error("[Node::save_file] cannot open file for writing {}", save_path);
        return;
    }

    outfile.write(reinterpret_cast<const char*>(file.get_data()), file.get_size());
    outfile.close();

    std::cout << "received file " << save_path << std::endl;
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
        case TCU_HDR_FLAG_DF:
            process_tcu_single_text(packet);
            break;
        case TCU_HDR_FLAG_MF:
            process_tcu_more_frag_text(packet);
            break;
        case TCU_HDR_NO_FLAG:
            process_tcu_last_frag_text(packet);
            break;
        case (TCU_HDR_FLAG_DF | TCU_HDR_FLAG_FL):
            process_tcu_single_file(packet);
            break;
        case (TCU_HDR_FLAG_MF | TCU_HDR_FLAG_FL):
            process_tcu_more_frag_file(packet);
            break;
        case TCU_HDR_FLAG_FL:
            process_tcu_last_frag_file(packet);
            break;
        case TCU_HDR_FLAG_NACK:
            process_tcu_negative_ack(packet);
            break;
        case TCU_HDR_FLAG_ACK:
            process_tcu_positive_ack(packet);
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
        spdlog::error("[Node::process_tcu_conn_req] unexpected phase {}", _pcb.phase);
        exit(EXIT_FAILURE);
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
        spdlog::error("[Node::process_tcu_conn_ack] unexpected phase {}", _pcb.phase);
        exit(EXIT_FAILURE);
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
        spdlog::error("[Node::process_tcu_disconn_req] unexpected phase {}", _pcb.phase);
        exit(EXIT_FAILURE);
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
        spdlog::error("[Node::process_tcu_disconn_ack] unexpected phase {}", _pcb.phase);
        exit(EXIT_FAILURE);
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

void Node::process_tcu_single_text(tcu_packet packet)
{
    if (_pcb.phase >= TCU_PHASE_CONNECT && _pcb.phase <= TCU_PHASE_NETWORK)
    {
        spdlog::info("[Node::process_tcu_single_text] received tcu single message");

        if (!packet.validate_crc())
        {
            spdlog::warn("[Node::process_tcu_single_text] invalid checksum");
            send_tcu_negative_ack(packet.header.seq_number);
            return;
        }

        std::string message(reinterpret_cast<char*>(packet.payload), packet.header.length);
        std::cout << "received text " << message << std::endl;

        send_tcu_positive_ack(0);
    }
    else
    {
        spdlog::error("[Node::process_tcu_single_text] unexpected phase {}", _pcb.phase);
        exit(EXIT_FAILURE);
    }
}

void Node::process_tcu_more_frag_text(tcu_packet packet)
{
    if (_pcb.phase >= TCU_PHASE_CONNECT && _pcb.phase <= TCU_PHASE_NETWORK)
    {
        spdlog::info("[Node::process_tcu_more_frag_text] received tcu fragment {}", packet.header.seq_number);

        _pcb.window[packet.header.seq_number] = packet;
    }
    else
    {
        spdlog::error("[Node::process_tcu_more_frag_text] unexpected phase {}", _pcb.phase);
        exit(EXIT_FAILURE);
    }
}

void Node::process_tcu_last_frag_text(tcu_packet packet)
{
    if (_pcb.phase >= TCU_PHASE_CONNECT && _pcb.phase <= TCU_PHASE_NETWORK)
    {
        spdlog::info("[Node::process_tcu_last_frag_text] received tcu last fragment {}", packet.header.seq_number);

        _pcb.window[packet.header.seq_number] = packet;

        uint24_t first_seq = _pcb.window.begin()->first;
        uint24_t last_seq = _pcb.window.rbegin()->first;
        for (uint24_t seq_num = first_seq; seq_num <= last_seq; seq_num++)
        {
            auto it = _pcb.window.find(seq_num);
            if (it == _pcb.window.end())
            {
                spdlog::warn("[Node::process_tcu_last_frag_text] missing fragment {}", seq_num);
                send_tcu_negative_ack(seq_num);
                return;
            }

            tcu_packet frag = it->second;
            if (!frag.validate_crc())
            {
                spdlog::warn("[Node::process_tcu_last_frag_text] invalid checksum in fragment {}", frag.header.seq_number);
                send_tcu_negative_ack(frag.header.seq_number);
                return;
            }
        }

        send_tcu_positive_ack(last_seq);

        std::string assembled_message;

        std::map<uint24_t, tcu_packet> sorted_fragments = _pcb.window;

        for (const auto& entry : sorted_fragments)
        {
            const tcu_packet& frag = entry.second;
            assembled_message.append(reinterpret_cast<char*>(frag.payload), frag.header.length);
        }

        std::cout << "received text " << assembled_message << std::endl;

        _pcb.window.clear();
    }
    else
    {
        spdlog::error("[Node::process_tcu_last_frag_text] unexpected phase {}", _pcb.phase);
        exit(EXIT_FAILURE);
    }
}

void Node::process_tcu_single_file(tcu_packet packet)
{
    if (_pcb.phase >= TCU_PHASE_CONNECT && _pcb.phase <= TCU_PHASE_NETWORK)
    {
        spdlog::info("[Node::process_tcu_single_file] received tcu single file");

        if (!packet.validate_crc())
        {
            spdlog::warn("[Node::process_tcu_single_file] invalid checksum");
            send_tcu_negative_ack(packet.header.seq_number);
            return;
        }

        File file = File::from_buff(packet.payload);

        save_file(file);

        send_tcu_positive_ack(0);
    }
    else
    {
        spdlog::error("[Node::process_tcu_single_file] unexpected phase {}", _pcb.phase);
        exit(EXIT_FAILURE);
    }
}

void Node::process_tcu_more_frag_file(tcu_packet packet)
{
    if (_pcb.phase >= TCU_PHASE_CONNECT && _pcb.phase <= TCU_PHASE_NETWORK)
    {
        spdlog::info("[Node::process_tcu_more_frag_file] received tcu file fragment {}", packet.header.seq_number);

        _pcb.window[packet.header.seq_number] = packet;
    }
    else
    {
        spdlog::error("[Node::process_tcu_more_frag_file] unexpected phase {}", _pcb.phase);
        exit(EXIT_FAILURE);
    }
}

void Node::process_tcu_last_frag_file(tcu_packet packet)
{
    if (_pcb.phase >= TCU_PHASE_CONNECT && _pcb.phase <= TCU_PHASE_NETWORK)
    {
        spdlog::info("[Node::process_tcu_last_frag_file] received tcu last file fragment {}", packet.header.seq_number);

        _pcb.window[packet.header.seq_number] = packet;

        uint24_t first_seq = _pcb.window.begin()->first;
        uint24_t last_seq = _pcb.window.rbegin()->first;

        for (uint24_t seq_num = first_seq; seq_num <= last_seq; seq_num++)
        {
            auto it = _pcb.window.find(seq_num);
            if (it == _pcb.window.end())
            {
                spdlog::warn("[Node::process_tcu_last_frag_file] missing fragment {}", seq_num);
                send_tcu_negative_ack(seq_num);
                return;
            }

            tcu_packet frag = it->second;
            if (!frag.validate_crc())
            {
                spdlog::warn("[Node::process_tcu_last_frag_file] invalid checksum in fragment {}", frag.header.seq_number);
                send_tcu_negative_ack(frag.header.seq_number);
                return;
            }
        }

        std::map<uint24_t, tcu_packet> sorted_fragments = _pcb.window;

        size_t total_size = 0;
        for (const auto& entry : sorted_fragments)
        {
            total_size += entry.second.header.length;
        }

        unsigned char* full_data = new unsigned char[total_size];
        size_t offset = 0;

        for (const auto& entry : sorted_fragments)
        {
            const tcu_packet& frag = entry.second;
            std::memcpy(full_data + offset, frag.payload, frag.header.length);
            offset += frag.header.length;
        }

        File file = File::from_buff(full_data);

        save_file(file);

        delete[] full_data;

        _pcb.window.clear();

        send_tcu_positive_ack(last_seq);
    }
    else
    {
        spdlog::error("[Node::process_tcu_last_frag_file] unexpected phase {}", _pcb.phase);
        exit(EXIT_FAILURE);
    }
}


void Node::process_tcu_negative_ack(tcu_packet packet)
{
    if (_pcb.phase >= TCU_PHASE_CONNECT && _pcb.phase <= TCU_PHASE_NETWORK)
    {
        uint24_t seq_number = packet.header.seq_number;
        spdlog::info("[Node::process_tcu_negative_ack] received tcu negative acknowledgment for fragment {}", seq_number);

        auto it = _pcb.window.find(seq_number);
        if (it != _pcb.window.end())
        {
            tcu_packet& frag = it->second;

            frag.header.flags = TCU_HDR_NO_FLAG;
            frag.calculate_crc();

            spdlog::info("[Node::process_tcu_negative_ack] resent fragment {}", seq_number);

            send_packet(frag.to_buff(), TCU_HDR_LEN + frag.header.length);
        }
        else
        {
            spdlog::error("[Node::process_tcu_negative_ack] fragment {} not found in window", seq_number);
        }
    }
    else
    {
        spdlog::error("[Node::process_tcu_negative_ack] unexpected phase {}", _pcb.phase);
        exit(EXIT_FAILURE);
    }
}

void Node::process_tcu_positive_ack(tcu_packet packet)
{
    if (_pcb.phase >= TCU_PHASE_CONNECT && _pcb.phase <= TCU_PHASE_NETWORK)
    {
        uint24_t seq_number = packet.header.seq_number;
        spdlog::info("[Node::process_tcu_positive_ack] received tcu positive acknowledgment for fragment {}", seq_number);

        _pcb.window.clear();
    }
    else
    {
        spdlog::error("[Node::process_tcu_positive_ack] unexpected phase {}", _pcb.phase);
        exit(EXIT_FAILURE);
    }
}

void Node::send_tcu_conn_req()
{
    if (_pcb.src_port == 0 || _pcb.dest_port == 0 || _pcb.dest_ip.s_addr == 0)
    {
        std::cout << "address and port not set" << std::endl;
        return;
    }

    if (_pcb.phase <= TCU_PHASE_INITIALIZE)
    {
        spdlog::info("[Node::send_tcu_conn_req] sending tcu connection request");

        // SYN
        tcu_packet packet{};
        packet.header.flags = TCU_HDR_FLAG_SYN;
        packet.header.length = 0;
        packet.header.seq_number = 0;
        packet.calculate_crc();

        send_packet(packet.to_buff(), TCU_HDR_LEN);

        _pcb.new_phase(TCU_PHASE_CONNECT);

        wait_for_ack();
    }
    else if (_pcb.phase >= TCU_PHASE_CONNECT && _pcb.phase <= TCU_PHASE_NETWORK)
    {
        std::cout << "already active connection" << std::endl;
    }
    else
    {
        spdlog::error("[Node::send_tcu_conn_req] unexpected phase {}", _pcb.phase);
        exit(EXIT_FAILURE);
    }
}

void Node::send_tcu_conn_ack()
{
    if (_pcb.phase == TCU_PHASE_CONNECT)
    {
        spdlog::info("[Node::send_tcu_conn_ack] sending tcu connection acknowledgment");

        // SYN + ACK
        tcu_packet packet{};
        packet.header.flags = TCU_HDR_FLAG_SYN | TCU_HDR_FLAG_ACK;
        packet.header.length = 0;
        packet.header.seq_number = 0;
        packet.calculate_crc();

        send_packet(packet.to_buff(), TCU_HDR_LEN);

        _pcb.new_phase(TCU_PHASE_NETWORK);
    }
    else
    {
        spdlog::error("[Node::send_tcu_conn_ack] unexpected phase {}", _pcb.phase);
        exit(EXIT_FAILURE);
    }
}

void Node::send_tcu_disconn_req()
{
    if (_pcb.phase >= TCU_PHASE_CONNECT && _pcb.phase <= TCU_PHASE_NETWORK) {
        spdlog::info("[Node::send_tcu_disconn_req] sending tcu disconnection request");

        // FIN
        tcu_packet packet{};
        packet.header.flags = TCU_HDR_FLAG_FIN;
        packet.header.length = 0;
        packet.header.seq_number = 0;
        packet.calculate_crc();

        send_packet(packet.to_buff(), TCU_HDR_LEN);

        _pcb.new_phase(TCU_PHASE_DISCONNECT);

        wait_for_ack();
    }
    else if (_pcb.phase <= TCU_PHASE_INITIALIZE)
    {
        std::cout << "no active connection" << std::endl;
    }
    else
    {
        std::cout << "connection not established" << std::endl;
    }
}

void Node::send_tcu_disconn_ack()
{
    if (_pcb.phase == TCU_PHASE_DISCONNECT)
    {
        spdlog::info("[Node::send_tcu_disconn_ack] sending tcu disconnection acknowledgment");

        // FIN + ACK
        tcu_packet packet{};
        packet.header.flags = TCU_HDR_FLAG_FIN | TCU_HDR_FLAG_ACK;
        packet.header.length = 0;
        packet.header.seq_number = 0;
        packet.calculate_crc();

        send_packet(packet.to_buff(), TCU_HDR_LEN);

        _pcb.new_phase(TCU_PHASE_HOLDOFF);
    }
    else
    {
        spdlog::error("[Node::send_tcu_disconn_ack] unexpected phase {}", _pcb.phase);
        exit(EXIT_FAILURE);
    }
}

void Node::send_keep_alive_req()
{
    // KA
    tcu_packet packet{};
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
        tcu_packet packet{};
        packet.header.flags = TCU_HDR_FLAG_KA | TCU_HDR_FLAG_ACK;
        packet.header.length = 0;
        packet.header.seq_number = 0;
        packet.calculate_crc();

        send_packet(packet.to_buff(), TCU_HDR_LEN);
    }
    else
    {
        spdlog::error("[Node::send_keep_alive_ack] unexpected phase {}", _pcb.phase);
        exit(EXIT_FAILURE);
    }

}

void Node::send_text(const std::string& message)
{
    if (_pcb.phase >= TCU_PHASE_CONNECT && _pcb.phase <= TCU_PHASE_NETWORK)
    {
        size_t message_length = message.size();
        size_t max_payload_size = _pcb.max_frag_size;

        /* If message fit maximum payload size */
        if (message_length <= max_payload_size)
        {
            // DF
            tcu_packet packet{};
            packet.header.flags = TCU_HDR_FLAG_DF;
            packet.header.length = message_length;
            packet.header.seq_number = 0;
            packet.payload = new unsigned char[message_length];
            std::memcpy(packet.payload, message.data(), message_length);

            packet.calculate_crc();

            _pcb.window[packet.header.seq_number] = packet;

            spdlog::info("[Node::send_text] sent tcu single message size {}", message_length);

            send_packet(packet.to_buff(), TCU_HDR_LEN + message_length);
        }
        else
        {
            /* Fragmented message */
            uint24_t total_fragments = (message_length + max_payload_size - 1) / max_payload_size;
            size_t offset = 0;

            for (uint24_t seq_number = 1; seq_number <= total_fragments; seq_number++)
            {
                size_t fragment_size = std::min(max_payload_size, message_length - offset);

                tcu_packet packet{};
                packet.header.length = fragment_size;
                packet.header.seq_number = seq_number;
                packet.payload = new unsigned char[fragment_size];
                std::memcpy(packet.payload, message.data() + offset, fragment_size);

                if (seq_number == total_fragments)
                {
                    packet.header.flags = TCU_HDR_NO_FLAG;
                }
                else
                {
                    packet.header.flags = TCU_HDR_FLAG_MF;
                }

                packet.calculate_crc();

                _pcb.window[packet.header.seq_number] = packet;

                spdlog::info("[Node::send_text] sent tcu fragment {} size {}", seq_number, fragment_size);

                /* Сorrupted packet simulation */
                if (seq_number == 2)
                {
                    packet.payload[0] ^= 0xFF;
                }

                send_packet(packet.to_buff(), TCU_HDR_LEN + fragment_size);

                offset += fragment_size;
            }
        }
    }
    else
    {
        std::cout << "connection not established" << std::endl;
    }
}

void Node::send_file(const std::string& file_path)
{
    if (_pcb.phase >= TCU_PHASE_CONNECT && _pcb.phase <= TCU_PHASE_NETWORK)
    {
        std::ifstream file_stream(file_path, std::ios::binary | std::ios::ate);
        if (!file_stream)
        {
            std::cout << "error file opening" << std::endl;
            return;
        }

        // File size
        std::streamsize file_size = file_stream.tellg();
        file_stream.seekg(0, std::ios::beg);

        // File data
        std::vector<unsigned char> file_data(static_cast<size_t>(file_size));
        if (!file_stream.read(reinterpret_cast<char*>(file_data.data()), file_size))
        {
            std::cout << "error file reading" << std::endl;
            return;
        }

        // File name
        std::string file_name = file_path.substr(file_path.find_last_of("/\\") + 1);

        // Creating file object
        File file(file_name.c_str(), file_data.data(), static_cast<uint32_t>(file_size));

        // File to buff
        unsigned char* file_buffer = file.to_buff();
        size_t total_size = sizeof(FileHeader) + file.get_size();

        size_t max_payload_size = _pcb.max_frag_size;

        if (total_size <= max_payload_size)
        {
            // DF + FL
            tcu_packet packet{};
            packet.header.flags = TCU_HDR_FLAG_DF | TCU_HDR_FLAG_FL;
            packet.header.length = static_cast<uint16_t>(total_size);
            packet.header.seq_number = 0;
            packet.payload = new unsigned char[total_size];
            std::memcpy(packet.payload, file_buffer, total_size);

            packet.calculate_crc();

            _pcb.window[packet.header.seq_number] = packet;

            spdlog::info("[Node::send_file] sent tcu single file size {}", total_size);

            send_packet(packet.to_buff(), TCU_HDR_LEN + total_size);
        }
        else
        {
            // Fragmented
            size_t offset = 0;
            uint24_t seq_number = 1;
            size_t remaining_size = total_size;

            while (remaining_size > 0)
            {
                size_t fragment_size = std::min(max_payload_size, remaining_size);

                tcu_packet packet{};
                packet.header.length = static_cast<uint16_t>(fragment_size);
                packet.header.seq_number = seq_number++;
                packet.payload = new unsigned char[fragment_size];
                std::memcpy(packet.payload, file_buffer + offset, fragment_size);

                if (remaining_size == fragment_size)
                {
                    packet.header.flags = TCU_HDR_FLAG_FL;
                }
                else
                {
                    packet.header.flags = TCU_HDR_FLAG_MF | TCU_HDR_FLAG_FL;
                }

                packet.calculate_crc();

                _pcb.window[packet.header.seq_number] = packet;

                spdlog::info("[Node::send_file] sent tcu file fragment {} size {}", packet.header.seq_number, fragment_size);

                send_packet(packet.to_buff(), TCU_HDR_LEN + fragment_size);

                offset += fragment_size;
                remaining_size -= fragment_size;
            }
        }

        delete[] file_buffer;
    }
    else
    {
        std::cout << "connection not established" << std::endl;
    }
}

void Node::send_tcu_negative_ack(uint24_t seq_number)
{
    if (_pcb.phase >= TCU_PHASE_CONNECT && _pcb.phase <= TCU_PHASE_NETWORK)
    {
        spdlog::info("[Node::send_tcu_negative_ack] send tcu negative acknowledgment for fragment {}", seq_number);

        tcu_packet packet{};
        packet.header.flags = TCU_HDR_FLAG_NACK;
        packet.header.length = 0;
        packet.header.seq_number = seq_number;
        packet.calculate_crc();

        send_packet(packet.to_buff(), TCU_HDR_LEN);
    }
    else
    {
        spdlog::error("[Node::send_tcu_negative_ack] unexpected phase {}", _pcb.phase);
        exit(EXIT_FAILURE);
    }
}

void Node::send_tcu_positive_ack(uint24_t seq_number)
{
    if (_pcb.phase >= TCU_PHASE_CONNECT && _pcb.phase <= TCU_PHASE_NETWORK)
    {
        spdlog::info("[Node::send_tcu_positive_ack] send tcu positive acknowledgment for fragment {}", seq_number);

        tcu_packet packet{};
        packet.header.flags = TCU_HDR_FLAG_ACK;
        packet.header.length = 0;
        packet.header.seq_number = seq_number;
        packet.calculate_crc();

        send_packet(packet.to_buff(), TCU_HDR_LEN);
    }
    else
    {
        spdlog::error("[Node::send_tcu_positive_ack] unexpected phase {}", _pcb.phase);
        exit(EXIT_FAILURE);
    }
}
