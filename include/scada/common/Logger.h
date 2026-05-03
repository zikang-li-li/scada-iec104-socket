#pragma once

#include <fstream>
#include <mutex>
#include <string>

namespace scada::common {

enum class LogLevel {
    Debug = 0,
    Info,
    Warn,
    Error
};

class Logger {
public:
    static void setLevel(LogLevel level);
    static LogLevel levelFromString(const std::string& value, LogLevel fallback = LogLevel::Info);
    static void setConsoleEnabled(bool enabled);
    static bool setOutputFile(const std::string& path, bool append = true);
    static void closeOutputFile();

    static void debug(const std::string& message);
    static void info(const std::string& message);
    static void warn(const std::string& message);
    static void error(const std::string& message);

private:
    static void log(LogLevel level, const std::string& message);
    static const char* levelName(LogLevel level);
    static std::string now();

    static std::mutex mutex_;
    static LogLevel minLevel_;
    static bool consoleEnabled_;
    static std::ofstream file_;
};

} // namespace scada::common
