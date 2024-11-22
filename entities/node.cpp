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

    int buff_size = 3000000;
    if (setsockopt(_socket.get_socket(), SOL_SOCKET, SO_RCVBUF, &buff_size, sizeof(buff_size)) < 0)
    {
        perror("setsockopt SO_RCVBUF");
    }
    if (setsockopt(_socket.get_socket(), SOL_SOCKET, SO_SNDBUF, &buff_size, sizeof(buff_size)) < 0)
    {
        perror("setsockopt SO_SNDBUF");
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

    _max_frag_size = TCU_MAX_PAYLOAD_LEN;
    _seq_num = 1;
    _ack_received = false;

    _window_size = 0;
    _dynamic_window = true;
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
    struct stat info{};

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
    spdlog::info("[Node::set_path] set file saving path {}", path);
}

void Node::set_max_frag_size(size_t size)
{
    _max_frag_size = size;
    spdlog::info("[Node::set_max_frag_size] set max fragment size {}", size);
}


void Node::set_window_size(uint24_t size)
{
    _window_size = size;
    _dynamic_window = false;
    spdlog::info("[Node::set_window_size] set manual window size {}", _window_size);

}

void Node::set_dynamic_window()
{
    _dynamic_window = true;
    spdlog::info("[Node::set_dynamic_window] set dynamic window sizing");
}

void Node::dynamic_window_size()
{
    _window_size = std::max(uint24_t(1), _total_num / uint24_t(5)); // 20 %
    spdlog::info("[Node::set_window_size] set dynamic window size {}", _window_size);
}

void Node::start_receiving()
{
    if (!_receive_running)
    {
        _receive_running = true;
        _receive_thread = std::thread(&Node::receive_loop, this);

        std::thread::native_handle_type handle = _receive_thread.native_handle();
        sched_param sch_params{};
        sch_params.sched_priority = 10;
        if (pthread_setschedparam(handle, SCHED_FIFO, &sch_params) != 0)
        {
            spdlog::warn("[Node::start_receiving] error set thread priority", strerror(errno));
        }
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
    timeout.tv_usec = 50000;

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

void Node::wait_for_conn_ack()
{
    spdlog::info("[Node::wait_for_conn_ack] waiting for tcu connection acknowledgment");

    _ack_received = false;

    auto start_time = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(TCU_CONNECTION_TIMEOUT_INTERVAL))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (_ack_received)
        {
            _ack_received = false;
            return;
        }
    }

    spdlog::info("[Node::wait_for_conn_ack] no tcu acknowledgment, closing connection");
    _pcb.new_phase(TCU_PHASE_HOLDOFF);
    std::cout << "destination node down, connection closed" << std::endl;
}

void Node::wait_for_recv_ack()
{
    spdlog::info("[Node::wait_for_recv_ack] waiting for tcu receive acknowledgment");

    int retry_count = 1;
    int max_retries = TCU_ACTIVITY_ATTEMPT_COUNT;

    while (retry_count <= max_retries)
    {
        auto start_time = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(TCU_RECEIVE_TIMEOUT_INTERVAL))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            if (_ack_received)
            {
                _ack_received = false;
                return;
            }
        }
        retry_count++;

        spdlog::info("[Node::wait_for_recv_ack] no tcu receive acknowledgment, resending window {}/{}", retry_count, max_retries);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        send_window();
    }

    spdlog::error("[Node::wait_for_recv_ack] no tcu receive acknowledgment, closing connection");
    _pcb.new_phase(TCU_PHASE_HOLDOFF);
    std::cout << "destination node down, connection closed" << std::endl;
}


void Node::assemble_text()
{
    std::vector<uint24_t> seq_numbers;
    for (auto& entry : _received_packets)
    {
        seq_numbers.push_back(entry.first);
    }
    std::sort(seq_numbers.begin(), seq_numbers.end());

    std::string message;
    for (uint24_t seq : seq_numbers)
    {
        tcu_packet& pkt = _received_packets[seq];
        message.append(reinterpret_cast<char*>(pkt.payload), pkt.header.length);
    }

    _received_packets.clear();
    _error_packets.clear();

    // Compute duration
    auto receive_end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(receive_end_time - _receive_start_time_text).count();

    // Log information
    spdlog::info("[Node::assemble_text] received text message size {} time {}", message.size(), duration);

    std::cout << "received text " << message << std::endl;
}

void Node::assemble_file()
{
    std::vector<uint24_t> seq_numbers;
    for (auto& entry : _received_packets)
    {
        seq_numbers.push_back(entry.first);
    }
    std::sort(seq_numbers.begin(), seq_numbers.end());

    std::vector<unsigned char> file_data;
    for (uint24_t seq : seq_numbers)
    {
        tcu_packet& pkt = _received_packets[seq];
        file_data.insert(file_data.end(), pkt.payload, pkt.payload + pkt.header.length);
    }

    _received_packets.clear();
    _error_packets.clear();

    // Compute duration
    auto receive_end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(receive_end_time - _receive_start_time_file).count();

    File file = File::from_buff(file_data.data());

    // Log information
    spdlog::info("[Node::assemble_file] received file message size {} time {}", file.get_size(), duration);

    save_file(file);
}

void Node::save_file(const File& file)
{
    struct stat info{};

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
        case (TCU_HDR_FLAG_DF | TCU_HDR_FLAG_FL):
            process_tcu_single_file(packet);
            break;

        case (TCU_HDR_FLAG_MF):
            process_tcu_more_frag_text(packet);
            break;
        case (TCU_HDR_FLAG_MF | TCU_HDR_FLAG_FIN):
            process_tcu_last_wind_frag_text(packet);
            break;
        case (TCU_HDR_NO_FLAG):
            process_tcu_last_frag_text(packet);
            break;

        case (TCU_HDR_FLAG_MF | TCU_HDR_FLAG_FL):
            process_tcu_more_frag_file(packet);
            break;
        case (TCU_HDR_FLAG_MF | TCU_HDR_FLAG_FIN | TCU_HDR_FLAG_FL):
            process_tcu_last_wind_frag_file(packet);
            break;
        case (TCU_HDR_FLAG_FL):
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
        _ack_received = true;

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
        _ack_received = true;

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

void Node::process_tcu_more_frag_text(tcu_packet packet)
{
    if (_pcb.phase >= TCU_PHASE_CONNECT && _pcb.phase <= TCU_PHASE_NETWORK)
    {
        spdlog::info("[Node::process_tcu_more_frag_text] received tcu text packet {}", packet.header.seq_number);

        if (_received_packets.empty() && _error_packets.empty())
        {
            // Start timer when first fragment received
            _receive_start_time_text = std::chrono::steady_clock::now();
            std::cout << "receiving text..." << std::endl;
        }

        if (!packet.validate_crc())
        {
            spdlog::warn("[Node::process_tcu_more_frag_text] invalid checksum for packet {}", packet.header.seq_number);
            _error_packets[packet.header.seq_number] = packet;
        }
        else
        {
            _received_packets[packet.header.seq_number] = packet;
        }
    }
    else
    {
        spdlog::error("[Node::process_tcu_more_frag_text] unexpected phase {}", _pcb.phase);
        exit(EXIT_FAILURE);
    }
}

void Node::process_tcu_last_wind_frag_text(tcu_packet packet)
{
    if (_pcb.phase >= TCU_PHASE_CONNECT && _pcb.phase <= TCU_PHASE_NETWORK)
    {
        spdlog::info("[Node::process_tcu_more_frag_text] received tcu last window text packet {}", packet.header.seq_number);

        if (!packet.validate_crc())
        {
            spdlog::warn("[Node::process_tcu_last_wind_frag_text] invalid checksum for packet {}", packet.header.seq_number);
            _error_packets[packet.header.seq_number] = packet;
            send_tcu_negative_ack(packet.header.seq_number);
        }
        else
        {
            _received_packets[packet.header.seq_number] = packet;
        }

        if (_error_packets.empty())
        {
            send_tcu_positive_ack(packet.header.seq_number);
        }
        else
        {
            auto it = _error_packets.begin();
            send_tcu_negative_ack(it->first);
        }
    }
    else
    {
        spdlog::error("[Node::process_tcu_last_wind_frag_text] unexpected phase {}", _pcb.phase);
        exit(EXIT_FAILURE);
    }

}

void Node::process_tcu_last_frag_text(tcu_packet packet)
{
    if (_pcb.phase >= TCU_PHASE_CONNECT && _pcb.phase <= TCU_PHASE_NETWORK)
    {
        spdlog::info("[Node::process_tcu_more_frag_text] received tcu last text packet {}", packet.header.seq_number);

        if (!packet.validate_crc())
        {
            spdlog::warn("[Node::process_tcu_last_frag_text] invalid checksum for packet {}", packet.header.seq_number);
            _error_packets[packet.header.seq_number] = packet;
            send_tcu_negative_ack(packet.header.seq_number);
        }
        else
        {
            _received_packets[packet.header.seq_number] = packet;
        }

        if (_error_packets.empty())
        {
            send_tcu_positive_ack(packet.header.seq_number);

            assemble_text();

            _received_packets.clear();
            _error_packets.clear();

        }
        else
        {
            auto it = _error_packets.begin();
            send_tcu_negative_ack(it->first);
        }
    }
    else
    {
        spdlog::error("[Node::process_tcu_last_frag_text] unexpected phase {}", _pcb.phase);
        exit(EXIT_FAILURE);
    }
}

void Node::process_tcu_more_frag_file(tcu_packet packet)
{
    if (_pcb.phase >= TCU_PHASE_CONNECT && _pcb.phase <= TCU_PHASE_NETWORK)
    {
        spdlog::info("[Node::process_tcu_more_frag_text] received tcu file packet {}", packet.header.seq_number);

        if (_received_packets.empty() && _error_packets.empty())
        {
            // Start timer when first fragment received
            _receive_start_time_file = std::chrono::steady_clock::now();
            std::cout << "receiving file..." << std::endl;
        }

        if (!packet.validate_crc())
        {
            spdlog::warn("[Node::process_tcu_more_frag_file] invalid checksum for packet {}", packet.header.seq_number);
            _error_packets[packet.header.seq_number] = packet;
        }
        else
        {
            _received_packets[packet.header.seq_number] = packet;
        }
    }
    else
    {
        spdlog::error("[Node::process_tcu_more_frag_file] unexpected phase {}", _pcb.phase);
        exit(EXIT_FAILURE);
    }
}

void Node::process_tcu_last_wind_frag_file(tcu_packet packet)
{
    if (_pcb.phase >= TCU_PHASE_CONNECT && _pcb.phase <= TCU_PHASE_NETWORK)
    {
        spdlog::info("[Node::process_tcu_more_frag_text] received tcu last window file packet {}", packet.header.seq_number);

        if (!packet.validate_crc())
        {
            spdlog::warn("[Node::process_tcu_last_wind_frag_file] invalid checksum for packet {}", packet.header.seq_number);
            _error_packets[packet.header.seq_number] = packet;
            send_tcu_negative_ack(packet.header.seq_number);
        }
        else
        {
            _received_packets[packet.header.seq_number] = packet;
        }

        if (_error_packets.empty())
        {
            send_tcu_positive_ack(packet.header.seq_number);
        }
        else
        {
            auto it = _error_packets.begin();
            send_tcu_negative_ack(it->first);
        }
    }
    else
    {
        spdlog::error("[Node::process_tcu_last_wind_frag_file] unexpected phase {}", _pcb.phase);
        exit(EXIT_FAILURE);
    }
}

void Node::process_tcu_last_frag_file(tcu_packet packet)
{
    if (_pcb.phase >= TCU_PHASE_CONNECT && _pcb.phase <= TCU_PHASE_NETWORK)
    {
        spdlog::info("[Node::process_tcu_more_frag_text] received tcu last file packet {}", packet.header.seq_number);

        if (!packet.validate_crc())
        {
            spdlog::warn("[Node::process_tcu_last_frag_file] invalid checksum for packet {}", packet.header.seq_number);
            _error_packets[packet.header.seq_number] = packet;
            send_tcu_negative_ack(packet.header.seq_number);
        }
        else
        {
            _received_packets[packet.header.seq_number] = packet;
        }

        if (_error_packets.empty())
        {
            send_tcu_positive_ack(packet.header.seq_number);

            assemble_file();

            _received_packets.clear();
            _error_packets.clear();
        }
        else
        {
            auto it = _error_packets.begin();
            send_tcu_negative_ack(it->first);
        }
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
        spdlog::info("[Node::process_tcu_negative_ack] received tcu negative acknowledgment packet {}", packet.header.seq_number);

        uint24_t nack_seq = packet.header.seq_number;

        auto it = _send_packets.find(nack_seq);
        if (it != _send_packets.end())
        {
            tcu_packet& error_packet = it->second;
            error_packet.header.flags |= TCU_HDR_FLAG_FIN;
            error_packet.calculate_crc();

            send_packet(error_packet.to_buff(), TCU_HDR_LEN + error_packet.header.length);
            spdlog::info("[Node::process_tcu_negative_ack] recent packet {}", nack_seq);
        }
        else
        {
            spdlog::warn("[Node::process_tcu_negative_ack] unknown packet {}", nack_seq);
            exit(EXIT_FAILURE);
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
        spdlog::info("[Node::process_tcu_positive_ack] received tcu positive acknowledgment packet {}", packet.header.seq_number);

        uint24_t ack_seq = packet.header.seq_number;

        if (ack_seq == _total_num)
        {
            _seq_num = ack_seq + uint24_t(1);
            spdlog::info("[Node::process_tcu_positive_ack] all packets successfully sent");
            _ack_received = true;
        }
        else
        {
            _seq_num = ack_seq + uint24_t(1);
            spdlog::info("[Node::process_tcu_positive_ack] move to next window starting {}", _seq_num);
            _ack_received = true;
        }
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

        _ack_received = false;
        send_packet(packet.to_buff(), TCU_HDR_LEN);
        wait_for_conn_ack();

        _pcb.new_phase(TCU_PHASE_CONNECT);
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

        _ack_received = false;
        send_packet(packet.to_buff(), TCU_HDR_LEN);
        wait_for_conn_ack();

        _pcb.new_phase(TCU_PHASE_DISCONNECT);
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

void Node::send_window()
{
    spdlog::info("[Node::send_window] sending window range [{},{}]", _seq_num, std::min(_seq_num + _window_size - uint24_t(1), _total_num));

    // Send all fragments for the current window
    for (uint24_t seq = _seq_num; seq < _seq_num + _window_size && seq <= _total_num; seq++)
    {
        auto it = _send_packets.find(seq);
        if (it != _send_packets.end())
        {
            spdlog::info("[Node::send_window] sending tcp fragment {}", it->first);

            tcu_packet& packet = it->second;
            send_packet(packet.to_buff(), TCU_HDR_LEN + packet.header.length);
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    }
}

void Node::send_text(const std::string& message)
{
    if (_pcb.phase >= TCU_PHASE_CONNECT && _pcb.phase <= TCU_PHASE_NETWORK)
    {

        _send_packets.clear();
        _seq_num = 1;

        size_t message_length = message.size();
        size_t max_payload_size = _max_frag_size;

        if (message_length <= max_payload_size)
        {
            // DF
            tcu_packet packet{};
            packet.header.flags = TCU_HDR_FLAG_DF;
            packet.header.length = static_cast<uint16_t>(message_length);
            packet.header.seq_number = 1;
            packet.payload = new unsigned char[message_length];
            std::memcpy(packet.payload, message.data(), message_length);

            packet.calculate_crc();

            _send_packets[packet.header.seq_number] = packet;

            spdlog::info("[Node::send_file] sent tcu single text size {}", message_length);

            send_packet(packet.to_buff(), TCU_HDR_LEN + message_length);
        }
        else
        {
            // Fragmented
            _total_num = (message_length + max_payload_size - 1) / max_payload_size;
            if (_dynamic_window)
            {
                dynamic_window_size();
            }

            size_t offset = 0;
            for (uint24_t seq = 1; seq <= _total_num; seq++)
            {
                size_t fragment_size = std::min(max_payload_size, message_length - offset);

                tcu_packet packet{};
                packet.header.seq_number = seq;
                packet.header.length = static_cast<uint16_t>(fragment_size);
                packet.payload = new unsigned char[fragment_size];
                std::memcpy(packet.payload, message.data() + offset, fragment_size);

                if (seq == _total_num)
                {
                    packet.header.flags = TCU_HDR_NO_FLAG;
                }
                else if (seq % _window_size == 0)
                {
                    packet.header.flags = TCU_HDR_FLAG_FIN | TCU_HDR_FLAG_MF;
                }
                else
                {
                    packet.header.flags = TCU_HDR_FLAG_MF;
                }

                packet.calculate_crc();

                _send_packets[seq] = packet;

                offset += fragment_size;
            }

            spdlog::info("[Node::send_text] sent tcu fragmented text size {} fragments {} fragment size {}", message_length, _total_num, max_payload_size);
            std::cout << "sending text..." << std::endl;

            while (_seq_num <= _total_num)
            {
                _ack_received = false;
                send_window();
                wait_for_recv_ack();
            }

            spdlog::info("[Node::send_file] file transmission completed");
            std::cout << "complete" << std::endl;
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
        _send_packets.clear();
        _seq_num = 1;

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
        size_t total_size = sizeof(file.get_header().name_length) + file.get_header().name_length + sizeof(file.get_header().file_size) + file.get_size();

        size_t max_payload_size = _max_frag_size;

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

            _send_packets[packet.header.seq_number] = packet;

            spdlog::info("[Node::send_file] sent tcu single file name {} size {}", file_name, total_size);

            send_packet(packet.to_buff(), TCU_HDR_LEN + total_size);
        }
        else
        {
            // Fragmented
            _total_num = (total_size + max_payload_size - 1) / max_payload_size;
            if (_dynamic_window)
            {
                dynamic_window_size();
            }

            size_t offset = 0;
            for (uint24_t seq = 1; seq <= _total_num; seq++) {
                size_t fragment_size = std::min(max_payload_size, total_size - offset);

                tcu_packet packet{};
                packet.header.seq_number = seq;
                packet.header.length = static_cast<uint16_t>(fragment_size);
                packet.payload = new unsigned char[fragment_size];
                std::memcpy(packet.payload, file_buffer + offset, fragment_size);

                if (seq == _total_num)
                {
                    packet.header.flags = TCU_HDR_FLAG_FL;
                }
                else if (seq % _window_size == 0)
                {
                    packet.header.flags = TCU_HDR_FLAG_FIN | TCU_HDR_FLAG_MF | TCU_HDR_FLAG_FL;
                }
                else
                {
                    packet.header.flags = TCU_HDR_FLAG_MF | TCU_HDR_FLAG_FL;
                }

                packet.calculate_crc();

                _send_packets[seq] = packet;

                offset += fragment_size;
            }

            spdlog::info("[Node::send_file] sent tcu fragmented file name {} size {} fragments {} fragment size {}", file_name, total_size, _total_num, max_payload_size);
            std::cout << "sending file..." << std::endl;

            while (_seq_num < _total_num)
            {
                _ack_received = false;
                send_window();
                wait_for_recv_ack();
            }

            spdlog::info("[Node::send_file] file transmission completed");
            std::cout << "complete" << std::endl;
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
