/*
 * cli.cpp
 */

#include "cli.h"

#include <readline/readline.h>
#include <readline/history.h>

#include "logger.h"
#include "../version.h"

CLI::CLI(Node* node) : _node(node)
{
    read_history(CLI_HISTORY_FILE_NAME);
}

CLI::~CLI()
{
    write_history(CLI_HISTORY_FILE_NAME);
}

void CLI::run()
{
    display_header();

    char* input;
    while (true)
    {
        input = readline("> ");

        if (!input)
        {
            break;
        }

        std::string command(input);
        add_history(input);
        free(input);

        if (command.substr(0, 15) == "proc node port ")
        {
            int port = std::stoi(command.substr(15));
            _node->set_port(port);
        }

        else if (command.substr(0, 15) == "proc node dest ")
        {
            std::string ip_and_port = command.substr(15);
            size_t separator = ip_and_port.find(':');

            if (separator != std::string::npos)
            {
                std::string ip = ip_and_port.substr(0, separator);
                int port = std::stoi(ip_and_port.substr(separator + 1));

                in_addr dest_ip{};
                if (inet_pton(AF_INET, ip.c_str(), &dest_ip) == 1)
                {
                    _node->set_dest(dest_ip, port);
                }
                else
                {
                    std::cerr << "invalid ip addr format" << std::endl;
                    continue;
                }
            }
        }

        else if (command.substr(0, 20) == "proc node frag size ")
        {
            try {
                size_t size = std::stoul(command.substr(20));

                if (size > 0 && size < TCU_MAX_PAYLOAD_LEN)
                {
                    _node->get_pcb().set_max_frag_size(size);
                }
                else
                {
                    std::cout << "invalid fragment size" << std::endl;
                }
            }
            catch(std::out_of_range&)
            {
                std::cout << "invalid fragment size" << std::endl;
            }
        }

        else if (command == "proc node connect")
        {
            _node->send_tcu_conn_req();
        }

        else if (command == "proc node disconnect")
        {
            _node->send_tcu_disconn_req();
        }

        else if (command == "exit")
        {
            _node->stop_receiving();
            _node->stop_keep_alive();
            break;
        }

        else if (command == "help")
        {
            display_help();
        }

        else if (command == "show log")
        {
            std::string logs = Logger::get_instance()->get_logs();
            std::cout << logs << std::endl;
        }

        else if (command.substr(0, 14) == "set log level ")
        {
            std::string level_str = command.substr(14);
            spdlog::level::level_enum level;

            if (level_str == "trace")
                level = spdlog::level::trace;
            else if (level_str == "debug")
                level = spdlog::level::debug;
            else if (level_str == "info")
                level = spdlog::level::info;
            else if (level_str == "warn")
                level = spdlog::level::warn;
            else if (level_str == "error")
                level = spdlog::level::err;
            else if (level_str == "critical")
                level = spdlog::level::critical;
            else
            {
                std::cout << "unknown log level" << std::endl;
                continue;
            }

            Logger::set_level(level);
            spdlog::info("[CLI::run] changed log level {}", to_string_view(level));
        }

        else if (command.substr(0, 10) == "send text ")
        {
            std::string message = command.substr(10);
            _node->send_text(message);
        }

        else if (command.empty())
        {
            continue;
        }

        else
        {
            std::cout << "unknown command" << std::endl;
        }
    }
}

void CLI::display_help() {
    std::cout << "commands:\n"
              << " proc node port <port>        - set source node port will listen\n"
              << " proc node dest <ip>:<port>   - set destination node ip and port\n"
              << " proc node frag size <size>   - set maximum fragment size in bytes (0," << TCU_MAX_PAYLOAD_LEN << ")\n"
              << " proc node connect            - connect to destination node\n"
              << " proc node disconnect         - disconnect with destination node\n"
              << " send text <text>             - send text message to destination node\n"
              << " set log level <level>        - set log level (trace, debug, info, warn, error, critical)\n"
              << " show log                     - display current logs\n"
              << " exit                         - exit application\n";
}

void CLI::display_header()
{
    #ifdef _WIN32
        std::string user = getenv("USERNAME");      // For Windows
    #elif defined(__linux__) || defined(__APPLE__)
        std::string user = getenv("USER");    // For Linux/MacOS
    #else
        std::string user = "unknown";
    #endif

    #ifdef COMMIT_HASH
        std::string commit = COMMIT_HASH;
    #else
        std::string commit = "n/a";
    #endif

    std::cout << "\n"
              << " _  _     _ _       ___             _\n"
              << "| || |___| | |___  | _ \\___ ___ _ _| |\n"
              << "| __ / -_) | / _ \\ |  _/ -_) -_) '_|_|\n"
              << "|_||_\\___|_|_\\___/ |_| \\___\\___|_| (_)\n"
              << "\n"
              << "p2p application version " << VERSION << " built on " << __DATE__ << " " << __TIME__ << " by " << user << " commit " << commit << "\n"
              << "Copyright (c) 2024 Anton Dmitriev. Licensed under the MIT License.\n"
              << "type 'help' to see available commands\n"
              << "\n";
}
