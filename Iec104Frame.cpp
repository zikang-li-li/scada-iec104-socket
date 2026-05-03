#include "scada/iec104/Iec104Frame.h"

#include <cstring>

namespace scada::iec104 {
namespace {

constexpr std::uint8_t TypeSinglePoint = 1;      // M_SP_NA_1
constexpr std::uint8_t TypeScaledValue = 11;     // M_ME_NB_1
constexpr std::uint8_t TypeShortFloat = 13;      // M_ME_NC_1

int readIoa(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<int>(data[offset]) |
           (static_cast<int>(data[offset + 1]) << 8) |
           (static_cast<int>(data[offset + 2]) << 16);
}

void writeIoa(std::vector<std::uint8_t>& data, int address) {
    data.push_back(static_cast<std::uint8_t>(address & 0xff));
    data.push_back(static_cast<std::uint8_t>((address >> 8) & 0xff));
    data.push_back(static_cast<std::uint8_t>((address >> 16) & 0xff));
}

std::int16_t readInt16(const std::vector<std::uint8_t>& data, std::size_t offset) {
    const auto raw = static_cast<std::uint16_t>(data[offset]) |
                     (static_cast<std::uint16_t>(data[offset + 1]) << 8);
    return static_cast<std::int16_t>(raw);
}

void writeUInt16(std::vector<std::uint8_t>& data, std::uint16_t value) {
    data.push_back(static_cast<std::uint8_t>(value & 0xff));
    data.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
}

void writeInt16(std::vector<std::uint8_t>& data, std::int16_t value) {
    writeUInt16(data, static_cast<std::uint16_t>(value));
}

model::DataQuality parseQuality(std::uint8_t qualityByte, bool overflowBit) {
    model::DataQuality quality;
    quality.invalid = (qualityByte & 0x80) != 0;
    quality.notTopical = (qualityByte & 0x40) != 0;
    quality.substituted = (qualityByte & 0x20) != 0;
    quality.blocked = (qualityByte & 0x10) != 0;
    quality.overflow = overflowBit && (qualityByte & 0x01) != 0;
    return quality;
}

std::vector<std::uint8_t> buildAsduHeader(
    std::uint8_t typeId,
    std::uint16_t commonAddress,
    int informationObjectAddress) {
    std::vector<std::uint8_t> asdu;
    asdu.reserve(16);
    asdu.push_back(typeId);
    asdu.push_back(0x01); // one information object, SQ=0
    asdu.push_back(0x03); // spontaneous
    asdu.push_back(0x00);
    writeUInt16(asdu, commonAddress);
    writeIoa(asdu, informationObjectAddress);
    return asdu;
}

} // namespace

bool Iec104Frame::isValidApdu(const std::vector<std::uint8_t>& frame) {
    if (frame.size() < 6 || frame.size() > MaxApduLength) {
        return false;
    }
    if (frame[0] != StartByte) {
        return false;
    }
    const auto payloadLength = static_cast<std::size_t>(frame[1]);
    return payloadLength >= 4 && payloadLength + 2 == frame.size();
}

FrameFormat Iec104Frame::format(const std::vector<std::uint8_t>& frame) {
    if (!isValidApdu(frame)) {
        return FrameFormat::Unknown;
    }

    const auto control = frame[2];
    if ((control & 0x01) == 0) {
        return FrameFormat::I;
    }
    if ((control & 0x03) == 0x01) {
        return FrameFormat::S;
    }
    if ((control & 0x03) == 0x03) {
        return FrameFormat::U;
    }
    return FrameFormat::Unknown;
}

bool Iec104Frame::isStartDtAct(const std::vector<std::uint8_t>& frame) {
    return isValidApdu(frame) && frame[1] == 4 && frame[2] == 0x07 && frame[3] == 0x00 && frame[4] == 0x00 && frame[5] == 0x00;
}

bool Iec104Frame::isStartDtCon(const std::vector<std::uint8_t>& frame) {
    return isValidApdu(frame) && frame[1] == 4 && frame[2] == 0x0b && frame[3] == 0x00 && frame[4] == 0x00 && frame[5] == 0x00;
}

bool Iec104Frame::isTestFrAct(const std::vector<std::uint8_t>& frame) {
    return isValidApdu(frame) && frame[1] == 4 && frame[2] == 0x43 && frame[3] == 0x00 && frame[4] == 0x00 && frame[5] == 0x00;
}

std::uint16_t Iec104Frame::sendSequence(const std::vector<std::uint8_t>& frame) {
    if (format(frame) != FrameFormat::I) {
        return 0;
    }

    const auto encoded = static_cast<std::uint16_t>(frame[2]) |
                         (static_cast<std::uint16_t>(frame[3]) << 8);
    return static_cast<std::uint16_t>(encoded >> 1);
}

std::uint16_t Iec104Frame::receiveSequence(const std::vector<std::uint8_t>& frame) {
    if (!isValidApdu(frame)) {
        return 0;
    }

    const auto encoded = static_cast<std::uint16_t>(frame[4]) |
                         (static_cast<std::uint16_t>(frame[5]) << 8);
    return static_cast<std::uint16_t>(encoded >> 1);
}

std::vector<std::uint8_t> Iec104Frame::buildStartDtAct() {
    return {StartByte, 0x04, 0x07, 0x00, 0x00, 0x00};
}

std::vector<std::uint8_t> Iec104Frame::buildStartDtCon() {
    return {StartByte, 0x04, 0x0b, 0x00, 0x00, 0x00};
}

std::vector<std::uint8_t> Iec104Frame::buildTestFrCon() {
    return {StartByte, 0x04, 0x83, 0x00, 0x00, 0x00};
}

std::vector<std::uint8_t> Iec104Frame::buildSFormat(std::uint16_t receiveSequence) {
    const auto receive = static_cast<std::uint16_t>(receiveSequence << 1);
    return {
        StartByte,
        0x04,
        0x01,
        0x00,
        static_cast<std::uint8_t>(receive & 0xff),
        static_cast<std::uint8_t>((receive >> 8) & 0xff),
    };
}

std::vector<std::uint8_t> Iec104Frame::buildIFormat(
    std::uint16_t sendSequence,
    std::uint16_t receiveSequence,
    const std::vector<std::uint8_t>& asdu) {
    std::vector<std::uint8_t> frame;
    frame.reserve(asdu.size() + 6);
    frame.push_back(StartByte);
    frame.push_back(static_cast<std::uint8_t>(asdu.size() + 4));

    const auto send = static_cast<std::uint16_t>(sendSequence << 1);
    const auto receive = static_cast<std::uint16_t>(receiveSequence << 1);
    frame.push_back(static_cast<std::uint8_t>(send & 0xff));
    frame.push_back(static_cast<std::uint8_t>((send >> 8) & 0xff));
    frame.push_back(static_cast<std::uint8_t>(receive & 0xff));
    frame.push_back(static_cast<std::uint8_t>((receive >> 8) & 0xff));
    frame.insert(frame.end(), asdu.begin(), asdu.end());
    return frame;
}

std::vector<std::uint8_t> Iec104Frame::buildSinglePointAsdu(
    std::uint16_t commonAddress,
    int informationObjectAddress,
    bool state) {
    auto asdu = buildAsduHeader(TypeSinglePoint, commonAddress, informationObjectAddress);
    asdu.push_back(state ? 0x01 : 0x00);
    return asdu;
}

std::vector<std::uint8_t> Iec104Frame::buildFloatMeasurementAsdu(
    std::uint16_t commonAddress,
    int informationObjectAddress,
    float value) {
    auto asdu = buildAsduHeader(TypeShortFloat, commonAddress, informationObjectAddress);
    std::uint8_t raw[sizeof(float)]{};
    std::memcpy(raw, &value, sizeof(float));
    asdu.insert(asdu.end(), raw, raw + sizeof(float));
    asdu.push_back(0x00);
    return asdu;
}

std::vector<std::uint8_t> Iec104Frame::buildScaledMeasurementAsdu(
    std::uint16_t commonAddress,
    int informationObjectAddress,
    std::int16_t value) {
    auto asdu = buildAsduHeader(TypeScaledValue, commonAddress, informationObjectAddress);
    writeInt16(asdu, value);
    asdu.push_back(0x00);
    return asdu;
}

std::vector<Iec104Object> Iec104Frame::parseInformationObjects(
    const std::vector<std::uint8_t>& frame) {
    std::vector<Iec104Object> objects;
    if (format(frame) != FrameFormat::I || frame.size() < 12) {
        return objects;
    }

    const std::vector<std::uint8_t> asdu(frame.begin() + 6, frame.end());
    if (asdu.size() < 6) {
        return objects;
    }

    const auto typeId = asdu[0];
    const auto vsq = asdu[1];
    const bool sequence = (vsq & 0x80) != 0;
    const auto count = static_cast<int>(vsq & 0x7f);
    std::size_t offset = 6;

    int baseAddress = 0;
    if (sequence) {
        if (offset + 3 > asdu.size()) {
            return objects;
        }
        baseAddress = readIoa(asdu, offset);
        offset += 3;
    }

    for (int index = 0; index < count; ++index) {
        int address = baseAddress + index;
        if (!sequence) {
            if (offset + 3 > asdu.size()) {
                break;
            }
            address = readIoa(asdu, offset);
            offset += 3;
        }

        Iec104Object object;
        object.address = address;

        if (typeId == TypeSinglePoint) {
            if (offset + 1 > asdu.size()) {
                break;
            }
            const auto siq = asdu[offset++];
            object.iecType = "M_SP_NA_1";
            object.state = (siq & 0x01) != 0;
            object.value = object.state ? 1.0 : 0.0;
            object.quality = parseQuality(siq, false);
            objects.push_back(object);
        } else if (typeId == TypeScaledValue) {
            if (offset + 3 > asdu.size()) {
                break;
            }
            object.iecType = "M_ME_NB_1";
            object.value = static_cast<double>(readInt16(asdu, offset));
            offset += 2;
            object.quality = parseQuality(asdu[offset++], true);
            objects.push_back(object);
        } else if (typeId == TypeShortFloat) {
            if (offset + 5 > asdu.size()) {
                break;
            }
            float value = 0.0F;
            std::memcpy(&value, asdu.data() + offset, sizeof(float));
            offset += sizeof(float);
            object.iecType = "M_ME_NC_1";
            object.value = value;
            object.quality = parseQuality(asdu[offset++], true);
            objects.push_back(object);
        } else {
            break;
        }
    }

    return objects;
}

} // namespace scada::iec104
