#pragma once

#include "scada/scada/DataPoint.h"

#include <cstddef>
#include <mutex>
#include <string>

namespace scada::scada {

class DataCache {
public:
    void configure(std::string path, std::size_t maxRecords);
    bool appendMeasurement(const ::scada::model::DataPointSnapshot& snapshot);
    bool appendAlarm(const ::scada::model::AlarmEvent& alarm);
    std::size_t recordCount() const;
    const std::string& path() const;

private:
    bool appendLine(const std::string& line);
    void trimIfNeeded();

    std::string path_ = "data/cache.log";
    std::size_t maxRecords_ = 10000;
    mutable std::mutex mutex_;
};

} // namespace scada::scada
