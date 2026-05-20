#include "Logger.h"
#include "../util/Strings.h"
#include "../util/WinHelpers.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <vector>
#include <memory>
#include <filesystem>

namespace onekey::log {

void Init(const std::wstring& log_dir) {
    std::filesystem::create_directories(log_dir);

    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console->set_level(spdlog::level::info);

    std::string log_path = util::WideToUtf8(log_dir) + "/core.log";
    auto file = std::make_shared<spdlog::sinks::daily_file_sink_mt>(log_path, 0, 0);
    file->set_level(spdlog::level::trace);

    std::vector<spdlog::sink_ptr> sinks{ console, file };
    auto logger = std::make_shared<spdlog::logger>("vi", sinks.begin(), sinks.end());
    logger->set_level(spdlog::level::trace);
    logger->set_pattern("%H:%M:%S.%e [%^%l%$] %v");
    logger->flush_on(spdlog::level::info);

    spdlog::set_default_logger(logger);
}

void Shutdown() {
    spdlog::shutdown();
}

}  // namespace onekey::log
