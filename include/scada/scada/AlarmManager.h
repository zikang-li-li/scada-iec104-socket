#pragma once

#include "scada/common/Config.h"
#include "scada/scada/DataPoint.h"

#include <map>
#include <string>
#include <vector>

namespace scada::scada {

class AlarmManager {
public:
    std::vector<::scada::model::AlarmEvent> evaluate(
        const ::scada::common::PointDefinition& definition,
        const ::scada::model::DataPointSnapshot& snapshot);

    std::vector<::scada::model::AlarmEvent> evaluateStale(
        const ::scada::common::PointDefinition& definition,
        bool stale);

private:
    std::vector<::scada::model::AlarmEvent> transition(
        const ::scada::common::PointDefinition& definition,
        const std::string& rule,
        bool active,
        ::scada::model::AlarmSeverity severity,
        double value,
        const std::string& message);

    std::map<std::string, bool> activeRules_;
};

} // namespace scada::scada
