#pragma once

#include "scada/scada/DataPoint.h"

#include <cstddef>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace scada::scada {

class DataCache {
public:
    void configure(std::string path, std::size_t maxRecords);
    bool appendMeasurement(const ::scada::model::DataPointSnapshot& snapshot);
    bool appendBusinessData(const ::scada::model::BusinessDataRecord& record);
    bool appendAlarm(const ::scada::model::AlarmEvent& alarm);
    std::optional<::scada::model::DataPointSnapshot> latestMeasurement(
        const std::string& deviceId,
        int address) const;
    std::vector<::scada::model::BusinessDataRecord> recentBusinessData(std::size_t limit) const;
    std::size_t recordCount() const;
    const std::string& path() const;

private:
    bool appendLine(const std::string& line);
    void trimIfNeeded();

    std::string path_ = "data/cache.log";
    std::size_t maxRecords_ = 10000;
    std::map<std::string, ::scada::model::DataPointSnapshot> latest_;
    std::vector<::scada::model::BusinessDataRecord> recentBusiness_;
    mutable std::mutex mutex_;
};

} // namespace scada::scada
