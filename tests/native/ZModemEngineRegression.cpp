#include <array>
#include <deque>
#include <iostream>
#include <string>
#include <vector>

#include "utility/ZModemEngine.h"

namespace {

unsigned long fakeMillis = 0;

class FakeStream : public Stream {
public:
    int available() override { return static_cast<int>(input.size()); }

    int read() override
    {
        if (input.empty()) {
            return -1;
        }
        const uint8_t value = input.front();
        input.pop_front();
        return value;
    }

    int peek() override { return input.empty() ? -1 : input.front(); }

    std::size_t write(uint8_t value) override
    {
        output.push_back(value);
        return 1;
    }

    void pushInput(const std::vector<uint8_t>& bytes)
    {
        input.insert(input.end(), bytes.begin(), bytes.end());
    }

    void clearOutput() { output.clear(); }

    const std::vector<uint8_t>& getOutput() const { return output; }

private:
    std::deque<uint8_t> input;
    std::vector<uint8_t> output;
};

void advanceMillis(unsigned long delta)
{
    fakeMillis += delta;
}

uint16_t updcrc(uint8_t value, uint16_t crc)
{
    crc = crc ^ (static_cast<uint16_t>(value) << 8);
    for (int count = 0; count < 8; ++count) {
        if (crc & 0x8000) {
            crc = static_cast<uint16_t>((crc << 1) ^ 0x1021);
        } else {
            crc = static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

std::vector<uint8_t> makeHexHeader(uint8_t type, const std::array<uint8_t, 4>& flags)
{
    static const char* hexChars = "0123456789ABCDEF";
    std::vector<uint8_t> bytes = {ZPAD, ZPAD, ZDLE, ZHEX};
    auto appendHexByte = [&](uint8_t value) {
        bytes.push_back(static_cast<uint8_t>(hexChars[value >> 4]));
        bytes.push_back(static_cast<uint8_t>(hexChars[value & 0x0F]));
    };

    uint16_t crc = 0;
    crc = updcrc(type, crc);
    appendHexByte(type);
    for (uint8_t flag : flags) {
        crc = updcrc(flag, crc);
        appendHexByte(flag);
    }
    appendHexByte(static_cast<uint8_t>((crc >> 8) & 0xFF));
    appendHexByte(static_cast<uint8_t>(crc & 0xFF));
    bytes.push_back('\r');
    bytes.push_back('\n');
    if (type != ZFIN && type != ZACK) {
        bytes.push_back(0x11);
    }
    return bytes;
}

std::vector<uint8_t> makeXmodemBlock(uint8_t blockNumber, const std::vector<uint8_t>& payload)
{
    const uint8_t XSOH_LOCAL = 0x01;
    std::vector<uint8_t> bytes;
    bytes.reserve(1 + 2 + payload.size() + 2);
    bytes.push_back(XSOH_LOCAL);
    bytes.push_back(blockNumber);
    bytes.push_back(static_cast<uint8_t>(255 - blockNumber));
    bytes.insert(bytes.end(), payload.begin(), payload.end());

    uint16_t crc = 0;
    for (uint8_t value : payload) {
        crc = updcrc(value, crc);
    }
    bytes.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
    bytes.push_back(static_cast<uint8_t>(crc & 0xFF));
    return bytes;
}

bool expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << std::endl;
        return false;
    }
    return true;
}

bool testSenderRecoversFromRposAfterEof()
{
    fakeMillis = 0;
    FakeStream stream;
    File inputFile(std::vector<uint8_t>{'O', 'K', '!'});
    ZModemEngine engine;

    engine.begin(stream);
    engine.setFileStream(&inputFile, "/payload.bin", inputFile.size());
    if (!expect(engine.send(10000), "sender should start")) {
        return false;
    }

    advanceMillis(1001);
    engine.loop();
    stream.clearOutput();

    stream.pushInput(makeHexHeader(ZRINIT, {0, 0, 0, 0}));
    engine.loop();
    advanceMillis(1001);
    engine.loop();
    stream.clearOutput();

    stream.pushInput(makeHexHeader(ZRPOS, {0, 0, 0, 0}));
    engine.loop();
    if (!expect(engine.getState() == ZModemEngine::STATE_SEND_ZEOF,
                "sender should move to SEND_ZEOF after sending the last data chunk")) {
        return false;
    }

    stream.pushInput(makeHexHeader(ZRPOS, {0, 0, 0, 0}));
    engine.loop();
    return expect(engine.getState() == ZModemEngine::STATE_SEND_ZDATA,
                  "sender should return to SEND_ZDATA when ZRPOS arrives after EOF" );
}

bool testXmodemReceiverHandlesFragmentedBlock()
{
    const uint8_t XEOT_LOCAL = 0x04;

    fakeMillis = 0;
    FakeStream stream;
    File outputFile;
    ZModemEngine engine;

    engine.begin(stream);
    engine.setFileStream(&outputFile, "/received.bin", 128);
    engine.setXmodemCompatibility(true);
    if (!expect(engine.receive(10000), "receiver should start")) {
        return false;
    }
    stream.clearOutput();

    std::vector<uint8_t> payload(128);
    for (std::size_t index = 0; index < payload.size(); ++index) {
        payload[index] = static_cast<uint8_t>(index);
    }
    const std::vector<uint8_t> block = makeXmodemBlock(1, payload);

    stream.pushInput(std::vector<uint8_t>(block.begin(), block.begin() + 3));
    engine.loop();
    if (!expect(stream.getOutput().empty(), "receiver should not ACK before the full fragmented block arrives")) {
        return false;
    }

    stream.pushInput(std::vector<uint8_t>(block.begin() + 3, block.begin() + 67));
    engine.loop();
    if (!expect(stream.getOutput().empty(), "receiver should keep waiting when only part of the block payload is available")) {
        return false;
    }

    stream.pushInput(std::vector<uint8_t>(block.begin() + 67, block.end()));
    engine.loop();
    if (!expect(!stream.getOutput().empty() && stream.getOutput().back() == 0x06,
                "receiver should ACK once the complete XMODEM block arrives")) {
        return false;
    }

    stream.clearOutput();
    stream.pushInput({XEOT_LOCAL});
    engine.loop();
    if (!expect(engine.getState() == ZModemEngine::STATE_COMPLETE,
                "receiver should complete after receiving XMODEM EOT")) {
        return false;
    }

    return expect(outputFile.data() == payload, "receiver should preserve the fragmented XMODEM payload exactly");
}

} // namespace

unsigned long millis()
{
    return fakeMillis;
}

int main()
{
    const bool senderOk = testSenderRecoversFromRposAfterEof();
    const bool xmodemOk = testXmodemReceiverHandlesFragmentedBlock();

    if (!senderOk || !xmodemOk) {
        return 1;
    }

    std::cout << "All ZModemEngine regression tests passed." << std::endl;
    return 0;
}