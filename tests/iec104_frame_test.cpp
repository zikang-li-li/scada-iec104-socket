#include "scada/common/Config.h"
#include "scada/common/Logger.h"
#include "scada/iec104/Iec104Frame.h"
#include "scada/scada/AlarmManager.h"
#include "scada/scada/DataCache.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

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

    const auto badQualityFrame = scada::iec104::Iec104Frame::buildIFormat(
        9,
        0,
        scada::iec104::Iec104Frame::buildFloatMeasurementAsdu(1, 1001, 66.0F, 0x80));
    const auto asduInfo = scada::iec104::Iec104Frame::parseAsduInfo(badQualityFrame);
    if (!asduInfo || asduInfo->typeId != 13 || asduInfo->commonAddress != 1 ||
        asduInfo->objects.size() != 1 || !asduInfo->objects[0].quality.invalid) {
        std::cerr << "IEC104 ASDU diagnostic parse failed\n";
        return 1;
    }

    const auto description = scada::iec104::Iec104Frame::describe(badQualityFrame);
    if (description.find("M_ME_NC_1") == std::string::npos ||
        description.find("q=invalid") == std::string::npos) {
        std::cerr << "IEC104 frame description failed\n";
        return 1;
    }

    if (scada::iec104::Iec104Frame::toHex(scada::iec104::Iec104Frame::buildStartDtAct()) !=
        "68 04 07 00 00 00") {
        std::cerr << "IEC104 hex dump failed\n";
        return 1;
    }

    if (!scada::iec104::Iec104Frame::isTestFrAct(scada::iec104::Iec104Frame::buildTestFrAct()) ||
        !scada::iec104::Iec104Frame::isTestFrCon(scada::iec104::Iec104Frame::buildTestFrCon())) {
        std::cerr << "IEC104 heartbeat frame check failed\n";
        return 1;
    }

    const auto multiConfig = scada::common::Config::load("config/scada.conf");
    bool found110kv = false;
    bool foundWest = false;
    for (const auto& device : multiConfig.devices) {
        if (device.id == "rtu_110kv" &&
            device.client.port == 2404 &&
            device.client.heartbeatIntervalMs == 10000 &&
            device.client.heartbeatTimeoutMs == 3000 &&
            device.points.size() == 3) {
            found110kv = true;
        }
        if (device.id == "rtu_west" &&
            device.client.port == 2405 &&
            device.client.heartbeatIntervalMs == 10000 &&
            device.client.heartbeatTimeoutMs == 3000 &&
            device.points.size() == 3) {
            foundWest = true;
        }
    }
    if (!found110kv || !foundWest) {
        std::cerr << "Multi-RTU config parse failed\n";
        return 1;
    }
    if (multiConfig.log.path != "logs/scada_client.log" ||
        multiConfig.log.level != "info" ||
        !multiConfig.log.console ||
        !multiConfig.log.append) {
        std::cerr << "Log config parse failed\n";
        return 1;
    }

    scada::common::Logger::setConsoleEnabled(false);
    scada::common::Logger::setLevel(scada::common::LogLevel::Info);
    if (!scada::common::Logger::setOutputFile("data/selftest_logger.log", false)) {
        std::cerr << "Logger file open failed\n";
        return 1;
    }
    scada::common::Logger::info("logger info selftest");
    scada::common::Logger::warn("logger warn selftest");
    scada::common::Logger::error("logger error selftest");
    scada::common::Logger::closeOutputFile();
    scada::common::Logger::setConsoleEnabled(true);

    std::ifstream logInput("data/selftest_logger.log");
    const std::string logText(
        (std::istreambuf_iterator<char>(logInput)),
        std::istreambuf_iterator<char>());
    if (logText.find("[INFO] logger info selftest") == std::string::npos ||
        logText.find("[WARN] logger warn selftest") == std::string::npos ||
        logText.find("[ERROR] logger error selftest") == std::string::npos) {
        std::cerr << "Logger file content failed\n";
        return 1;
    }

    if (scada::model::pointTypeFromString("control") != scada::model::PointType::Control ||
        scada::model::businessTypeFromPointType(scada::model::PointType::Control) !=
            scada::model::BusinessDataType::Telecontrol) {
        std::cerr << "Business point type mapping failed\n";
        return 1;
    }

    scada::scada::DataCache cache;
    cache.configure("data/selftest_cache.log", 10);
    scada::model::DataPointSnapshot cachedSnapshot;
    cachedSnapshot.deviceId = "RTU1";
    cachedSnapshot.deviceName = "RTU1";
    cachedSnapshot.address = 1002;
    cachedSnapshot.name = "Voltage";
    cachedSnapshot.type = scada::model::PointType::Analog;
    cachedSnapshot.unit = "V";
    cachedSnapshot.value = 220.0;
    scada::model::BusinessDataRecord record;
    record.businessType = scada::model::BusinessDataType::Telemetry;
    record.snapshot = cachedSnapshot;
    record.displayValue = "220.00 V";
    if (!cache.appendBusinessData(record) ||
        !cache.latestMeasurement("RTU1", 1002) ||
        cache.recentBusinessData(1).size() != 1) {
        std::cerr << "Business data cache failed\n";
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
