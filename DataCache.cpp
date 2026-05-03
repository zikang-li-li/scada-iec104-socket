#include "scada/scada/DataCache.h"

#include "scada/scada/DataPoint.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <vector>

namespace scada::scada {
namespace {

std::string timestamp(std::chrono::system_clock::time_point timePoint) {
    const auto time = std::chrono::system_clock::to_time_t(timePoint);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif

    std::ostringstream stream;
    stream << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return stream.str();
}

std::string clean(std::string value) {
    std::replace(value.begin(), value.end(), '|', '/');
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

void DataCache::configure(std::string path, std::size_t maxRecords) {
    std::lock_guard<std::mutex> lock(mutex_);
    path_ = std::move(path);
    maxRecords_ = maxRecords == 0 ? 10000 : maxRecords;
    ensureParentDirectory(path_);
}

bool DataCache::appendMeasurement(const model::DataPointSnapshot& snapshot) {
    std::ostringstream line;
    line << timestamp(snapshot.timestamp)
         << "|MEASURE"
         << "|" << snapshot.address
         << "|" << clean(snapshot.name)
         << "|" << model::pointTypeName(snapshot.type)
         << "|" << clean(snapshot.iecType)
         << "|" << snapshot.value
         << "|" << (snapshot.state ? 1 : 0)
         << "|" << clean(snapshot.unit)
         << "|" << model::qualityText(snapshot.quality);

    return appendLine(line.str());
}

bool DataCache::appendAlarm(const model::AlarmEvent& alarm) {
    std::ostringstream line;
    line << timestamp(alarm.timestamp)
         << "|ALARM"
         << "|" << alarm.address
         << "|" << clean(alarm.pointName)
         << "|" << clean(alarm.rule)
         << "|" << model::severityName(alarm.severity)
         << "|" << (alarm.active ? "RAISED" : "CLEARED")
         << "|" << alarm.value
         << "|" << clean(alarm.message);

    return appendLine(line.str());
}

std::size_t DataCache::recordCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ifstream input(path_);
    std::size_t count = 0;
    std::string line;
    while (std::getline(input, line)) {
        ++count;
    }
    return count;
}

const std::string& DataCache::path() const {
    return path_;
}

bool DataCache::appendLine(const std::string& line) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!ensureParentDirectory(path_)) {
        return false;
    }

    std::ofstream output(path_, std::ios::app);
    if (!output) {
        return false;
    }

    output << line << '\n';
    trimIfNeeded();
    return true;
}

void DataCache::trimIfNeeded() {
    std::ifstream input(path_);
    if (!input) {
        return;
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        lines.push_back(line);
    }

    if (lines.size() <= maxRecords_) {
        return;
    }

    const auto first = lines.end() - static_cast<std::ptrdiff_t>(maxRecords_);
    std::ofstream output(path_, std::ios::trunc);
    for (auto iterator = first; iterator != lines.end(); ++iterator) {
        output << *iterator << '\n';
    }
}

} // namespace scada::scada
