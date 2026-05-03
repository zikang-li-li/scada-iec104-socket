#include "scada/common/Config.h"
#include "scada/iec104/Iec104Frame.h"
#include "scada/scada/AlarmManager.h"

#include <cmath>
#include <iostream>

int main() {
    const auto asdu = scada::iec104::Iec104Frame::buildFloatMeasurementAsdu(1, 1001, 96.5F);
    const auto frame = scada::iec104::Iec104Frame::buildIFormat(7, 0, asdu);
    const auto objects = scada::iec104::Iec104Frame::parseInformationObjects(frame);

    if (objects.size() != 1 || objects[0].address != 1001 || std::fabs(objects[0].value - 96.5) > 0.01) {
        std::cerr << "IEC104 float frame parse failed\n";
        return 1;
    }

    const auto singlePointFrame = scada::iec104::Iec104Frame::buildIFormat(
        8,
        0,
        scada::iec104::Iec104Frame::buildSinglePointAsdu(1, 2001, true));
    const auto singlePoints = scada::iec104::Iec104Frame::parseInformationObjects(singlePointFrame);
    if (singlePoints.size() != 1 || !singlePoints[0].state || singlePoints[0].quality.overflow) {
        std::cerr << "IEC104 single point quality parse failed\n";
        return 1;
    }

    scada::common::PointDefinition point;
    point.address = 1001;
    point.name = "Main transformer load";
    point.type = "analog";
    point.highHigh = 95.0;
    point.high = 85.0;

    scada::model::DataPointSnapshot snapshot;
    snapshot.address = 1001;
    snapshot.name = point.name;
    snapshot.type = scada::model::PointType::Analog;
    snapshot.value = objects[0].value;

    scada::scada::AlarmManager alarms;
    const auto raised = alarms.evaluate(point, snapshot);
    if (raised.empty() || raised[0].rule != "HIGH_HIGH" || !raised[0].active) {
        std::cerr << "Alarm high-high raise failed\n";
        return 1;
    }

    snapshot.value = 70.0;
    const auto cleared = alarms.evaluate(point, snapshot);
    bool foundClear = false;
    for (const auto& event : cleared) {
        if (event.rule == "HIGH_HIGH" && !event.active) {
            foundClear = true;
        }
    }

    if (!foundClear) {
        std::cerr << "Alarm high-high clear failed\n";
        return 1;
    }

    std::cout << "selftest passed\n";
    return 0;
}
