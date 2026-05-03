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

std::string cacheKey(const std::string& deviceId, int address) {
    return deviceId + ":" + std::to_string(address);
}

} // namespace

void DataCache::configure(std::string path, std::size_t maxRecords) {
    std::lock_guard<std::mutex> lock(mutex_);
    path_ = std::move(path);
    maxRecords_ = maxRecords == 0 ? 10000 : maxRecords;
    ensureParentDirectory(path_);
}

bool DataCache::appendMeasurement(const model::DataPointSnapshot& snapshot) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        latest_[cacheKey(snapshot.deviceId, snapshot.address)] = snapshot;
    }

    std::ostringstream line;
    line << timestamp(snapshot.timestamp)
         << "|MEASURE"
         << "|" << clean(snapshot.deviceId)
         << "|" << clean(snapshot.deviceName)
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

bool DataCache::appendBusinessData(const model::BusinessDataRecord& record) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        latest_[cacheKey(record.snapshot.deviceId, record.snapshot.address)] = record.snapshot;
        recentBusiness_.push_back(record);
        if (recentBusiness_.size() > maxRecords_) {
            const auto first = recentBusiness_.end() - static_cast<std::ptrdiff_t>(maxRecords_);
            recentBusiness_.erase(recentBusiness_.begin(), first);
        }
    }

    std::ostringstream line;
    line << timestamp(record.snapshot.timestamp)
         << "|BUSINESS"
         << "|" << clean(record.snapshot.deviceId)
         << "|" << clean(record.snapshot.deviceName)
         << "|" << model::businessTypeName(record.businessType)
         << "|" << record.snapshot.address
         << "|" << clean(record.snapshot.name)
         << "|" << clean(record.displayValue)
         << "|" << clean(record.snapshot.unit)
         << "|" << model::qualityText(record.snapshot.quality);

    return appendLine(line.str());
}

bool DataCache::appendAlarm(const model::AlarmEvent& alarm) {
    std::ostringstream line;
    line << timestamp(alarm.timestamp)
         << "|ALARM"
         << "|" << clean(alarm.deviceId)
         << "|" << clean(alarm.deviceName)
         << "|" << alarm.address
         << "|" << clean(alarm.pointName)
         << "|" << clean(alarm.rule)
         << "|" << model::severityName(alarm.severity)
         << "|" << (alarm.active ? "RAISED" : "CLEARED")
         << "|" << alarm.value
         << "|" << clean(alarm.message);

    return appendLine(line.str());
}

std::optional<model::DataPointSnapshot> DataCache::latestMeasurement(
    const std::string& deviceId,
    int address) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = latest_.find(cacheKey(deviceId, address));
    if (found == latest_.end()) {
        return std::nullopt;
    }
    return found->second;
}

std::vector<model::BusinessDataRecord> DataCache::recentBusinessData(std::size_t limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (limit == 0 || recentBusiness_.empty()) {
        return {};
    }

    const auto count = std::min(limit, recentBusiness_.size());
    return std::vector<model::BusinessDataRecord>(
        recentBusiness_.end() - static_cast<std::ptrdiff_t>(count),
        recentBusiness_.end());
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
