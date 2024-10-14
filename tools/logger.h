/*
 * logger.h
 */

#pragma once

#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/ostream_sink.h"
#include <vector>
#include <mutex>
#include <iostream>
#include <memory>
#include <sstream>

#define LOG_FILE_NAME ".logs"

class Logger {
public:
    Logger();
    ~Logger();

    static std::shared_ptr<Logger> get_instance();

    std::string get_logs();
    void clear_logs();
    static void set_level(spdlog::level::level_enum level);

private:
    void init_logger();

    std::shared_ptr<std::ostringstream> _log_stream;
    std::shared_ptr<spdlog::logger> _logger;

    static std::shared_ptr<Logger> _instance;
};