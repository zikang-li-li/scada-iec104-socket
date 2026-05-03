#pragma once

#include "scada/common/Config.h"
#include "scada/iec104/Iec104Client.h"
#include "scada/scada/AlarmManager.h"
#include "scada/scada/DataCache.h"
#include "scada/scada/DataPoint.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace scada::scada {

class ScadaSystem {
public:
    explicit ScadaSystem(::scada::common::AppConfig config);
    ~ScadaSystem();

    void start();
    void stop();

private:
    struct DeviceContext;

    void onDataObject(DeviceContext& context, const ::scada::iec104::Iec104Object& object);
    void onConnectionChanged(DeviceContext& context, bool connected);
    ::scada::model::BusinessDataRecord buildBusinessData(
        const ::scada::common::PointDefinition& definition,
        const ::scada::model::DataPointSnapshot& snapshot) const;
    void logBusinessData(const ::scada::model::BusinessDataRecord& record) const;
    void monitorLoop();
    ::scada::common::PointDefinition pointDefinitionFor(const DeviceContext& context, int address) const;
    static std::string snapshotKey(const std::string& deviceId, int address);
    static std::size_t pointCount(const std::vector<std::unique_ptr<DeviceContext>>& devices);

    ::scada::common::AppConfig config_;
    std::vector<std::unique_ptr<DeviceContext>> devices_;
    DataCache cache_;

    mutable std::mutex mutex_;
    std::map<std::string, ::scada::model::DataPointSnapshot> snapshots_;
    std::atomic<bool> running_{false};
    std::thread monitor_;
};

} // namespace scada::scada
