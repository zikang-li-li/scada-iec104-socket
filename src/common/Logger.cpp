#include "scada/common/Logger.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <system_error>

namespace scada::common {
namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool ensureParentDirectory(const std::string& path) {
    const std::filesystem::path filePath(path);
    if (!filePath.has_parent_path()) {
        return true;
    }

    std::error_code error;
    std::filesystem::create_directories(filePath.parent_path(), error);
    return !error;
}

} // namespace

std::mutex Logger::mutex_;
LogLevel Logger::minLevel_ = LogLevel::Info;
bool Logger::consoleEnabled_ = true;
std::ofstream Logger::file_;

void Logger::setLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    minLevel_ = level;
}

LogLevel Logger::levelFromString(const std::string& value, LogLevel fallback) {
    const auto normalized = lower(value);
    if (normalized == "debug") {
        return LogLevel::Debug;
    }
    if (normalized == "info") {
        return LogLevel::Info;
    }
    if (normalized == "warn" || normalized == "warning") {
        return LogLevel::Warn;
    }
    if (normalized == "error") {
        return LogLevel::Error;
    }
    return fallback;
}

void Logger::setConsoleEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    consoleEnabled_ = enabled;
}

bool Logger::setOutputFile(const std::string& path, bool append) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) {
        file_.close();
    }

    if (path.empty()) {
        return true;
    }
    if (!ensureParentDirectory(path)) {
        return false;
    }

    const auto mode = append ? (std::ios::out | std::ios::app) : (std::ios::out | std::ios::trunc);
    file_.open(path, mode);
    return file_.is_open();
}

void Logger::closeOutputFile() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) {
        file_.close();
    }
}

void Logger::debug(const std::string& message) {
    log(LogLevel::Debug, message);
}

void Logger::info(const std::string& message) {
    log(LogLevel::Info, message);
}

void Logger::warn(const std::string& message) {
    log(LogLevel::Warn, message);
}

void Logger::error(const std::string& message) {
    log(LogLevel::Error, message);
}

void Logger::log(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (static_cast<int>(level) < static_cast<int>(minLevel_)) {
        return;
    }

    std::ostringstream line;
    line << "[" << now() << "] [" << levelName(level) << "] " << message;

    if (consoleEnabled_) {
        std::ostream& output = level == LogLevel::Error ? std::cerr : std::cout;
        output << line.str() << std::endl;
    }

    if (file_.is_open()) {
        file_ << line.str() << '\n';
        file_.flush();
    }
}

const char* Logger::levelName(LogLevel level) {
    switch (level) {
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warn:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    }
    return "UNKNOWN";
}

std::string Logger::now() {
    const auto current = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(current);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                            current.time_since_epoch()) %
                        1000;

    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif

    std::ostringstream stream;
    stream << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
           << "." << std::setw(3) << std::setfill('0') << millis.count();
    return stream.str();
}

} // namespace scada::common
