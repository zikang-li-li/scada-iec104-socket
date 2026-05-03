#pragma once

#include "scada/common/Config.h"
#include "scada/iec104/Iec104Client.h"
#include "scada/scada/AlarmManager.h"
#include "scada/scada/DataCache.h"
#include "scada/scada/DataPoint.h"

#include <atomic>
#include <map>
#include <mutex>
#include <thread>

namespace scada::scada {

class ScadaSystem {
public:
    explicit ScadaSystem(::scada::common::AppConfig config);
    ~ScadaSystem();

    void start();
    void stop();

private:
    void onDataObject(const ::scada::iec104::Iec104Object& object);
    void onConnectionChanged(bool connected);
    void monitorLoop();
    ::scada::common::PointDefinition pointDefinitionFor(int address) const;

    ::scada::common::AppConfig config_;
    ::scada::iec104::Iec104Client client_;
    AlarmManager alarms_;
    DataCache cache_;

    mutable std::mutex mutex_;
    std::map<int, ::scada::model::DataPointSnapshot> snapshots_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::thread monitor_;
};

} // namespace scada::scada
