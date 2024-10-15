/*
 * logger.cpp
 */

#include "logger.h"

std::shared_ptr<Logger> Logger::_instance = nullptr;
std::mutex logger_mutex;

Logger::Logger() {
    init_logger();
}

Logger::~Logger() {
    spdlog::shutdown();
}

void Logger::init_logger()
{
    _log_stream = std::make_shared<std::ostringstream>();

    try {
        // Creating sink for file
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(LOG_FILE_NAME, true);

        // Create sink for application
        auto stream_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(*_log_stream);

        // Merging sinks
        std::vector<spdlog::sink_ptr> sinks{file_sink, stream_sink};

        // Creating logger
        _logger = std::make_shared<spdlog::logger>("main_logger", sinks.begin(), sinks.end());

        spdlog::set_default_logger(_logger);
        spdlog::set_pattern("%Y-%m-%d %H:%M:%S [%l] %v");
        spdlog::set_level(spdlog::level::info);

    }
    catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "error logger " << ex.what() << std::endl;
        exit(1);
    }
}

std::shared_ptr<Logger> Logger::get_instance()
{
    std::lock_guard<std::mutex> lock(logger_mutex);
    if (!_instance)
    {
        _instance = std::make_shared<Logger>();
    }
    return _instance;
}

std::string Logger::get_logs()
{
    return _log_stream->str();
}

void Logger::clear_logs()
{
    _log_stream->str("");
    _log_stream->clear();
}

void Logger::set_level(spdlog::level::level_enum level)
{
    spdlog::set_level(level);
}
