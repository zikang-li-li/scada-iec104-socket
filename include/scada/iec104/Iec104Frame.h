#pragma once

#include "scada/scada/DataPoint.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace scada::iec104 {

enum class FrameFormat {
    I,
    S,
    U,
    Unknown
};

struct Iec104Object {
    int address = 0;
    std::string iecType;
    double value = 0.0;
    bool state = false;
    scada::model::DataQuality quality;
};

struct Iec104AsduInfo {
    std::uint8_t typeId = 0;
    std::string typeName;
    bool sequence = false;
    int objectCount = 0;
    int causeOfTransmission = 0;
    bool negativeConfirm = false;
    bool test = false;
    std::uint16_t commonAddress = 0;
    std::vector<Iec104Object> objects;
};

class Iec104Frame {
public:
    static constexpr std::uint8_t StartByte = 0x68;
    static constexpr std::size_t MaxApduLength = 255;

    static bool isValidApdu(const std::vector<std::uint8_t>& frame);
    static FrameFormat format(const std::vector<std::uint8_t>& frame);
    static std::string frameFormatName(FrameFormat format);
    static std::string typeName(std::uint8_t typeId);
    static std::string causeName(int causeOfTransmission);

    static bool isStartDtAct(const std::vector<std::uint8_t>& frame);
    static bool isStartDtCon(const std::vector<std::uint8_t>& frame);
    static bool isTestFrAct(const std::vector<std::uint8_t>& frame);
    static bool isTestFrCon(const std::vector<std::uint8_t>& frame);

    static std::uint16_t sendSequence(const std::vector<std::uint8_t>& frame);
    static std::uint16_t receiveSequence(const std::vector<std::uint8_t>& frame);

    static std::vector<std::uint8_t> buildStartDtAct();
    static std::vector<std::uint8_t> buildStartDtCon();
    static std::vector<std::uint8_t> buildTestFrAct();
    static std::vector<std::uint8_t> buildTestFrCon();
    static std::vector<std::uint8_t> buildSFormat(std::uint16_t receiveSequence);
    static std::vector<std::uint8_t> buildIFormat(
        std::uint16_t sendSequence,
        std::uint16_t receiveSequence,
        const std::vector<std::uint8_t>& asdu);

    static std::vector<std::uint8_t> buildSinglePointAsdu(
        std::uint16_t commonAddress,
        int informationObjectAddress,
        bool state,
        std::uint8_t quality = 0);
    static std::vector<std::uint8_t> buildFloatMeasurementAsdu(
        std::uint16_t commonAddress,
        int informationObjectAddress,
        float value,
        std::uint8_t quality = 0);
    static std::vector<std::uint8_t> buildScaledMeasurementAsdu(
        std::uint16_t commonAddress,
        int informationObjectAddress,
        std::int16_t value,
        std::uint8_t quality = 0);

    static std::optional<Iec104AsduInfo> parseAsduInfo(
        const std::vector<std::uint8_t>& frame);
    static std::vector<Iec104Object> parseInformationObjects(
        const std::vector<std::uint8_t>& frame);
    static std::string toHex(const std::vector<std::uint8_t>& frame);
    static std::string describe(const std::vector<std::uint8_t>& frame);
};

} // namespace scada::iec104
