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

class CoupledStream : public Stream {
public:
    std::deque<uint8_t>* incoming = nullptr;
    std::deque<uint8_t>* outgoing = nullptr;

    int available() override { return incoming ? static_cast<int>(incoming->size()) : 0; }
    int read() override
    {
        if (!incoming || incoming->empty()) return -1;
        const uint8_t value = incoming->front();
        incoming->pop_front();
        return value;
    }
    int peek() override { return (!incoming || incoming->empty()) ? -1 : incoming->front(); }
    std::size_t write(uint8_t value) override
    {
        if (!outgoing) return 0;
        outgoing->push_back(value);
        return 1;
    }
};

bool runCoupledTransfer(const std::vector<uint8_t>& payload, const char* name)
{
    fakeMillis = 0;
    std::deque<uint8_t> a2b;
    std::deque<uint8_t> b2a;
    CoupledStream senderIO;
    CoupledStream receiverIO;
    senderIO.incoming = &b2a;
    senderIO.outgoing = &a2b;
    receiverIO.incoming = &a2b;
    receiverIO.outgoing = &b2a;

    File src(payload);
    File dst;
    ZModemEngine sender;
    ZModemEngine receiver;
    sender.begin(senderIO);
    receiver.begin(receiverIO);
    sender.setFileStream(&src, "/payload.bin", src.size());
    receiver.setFileStream(&dst, "/received.bin", 0);
    if (!expect(receiver.receive(20000), std::string(name) + ": receiver should start") ||
        !expect(sender.send(20000), std::string(name) + ": sender should start")) {
        return false;
    }

    int complete = 0;
    for (int i = 0; i < 4000 && complete != 3; ++i) {
        fakeMillis += 25;
        const int senderResult = sender.loop();
        const int receiverResult = receiver.loop();
        if (senderResult < 0) {
            std::cerr << "FAIL: " << name << " sender error at t=" << fakeMillis << std::endl;
            return false;
        }
        if (receiverResult < 0) {
            std::cerr << "FAIL: " << name << " receiver error at t=" << fakeMillis << std::endl;
            return false;
        }
        if (senderResult == 1) complete |= 1;
        if (receiverResult == 1) complete |= 2;
    }

    return expect(complete == 3, std::string(name) + ": both sides should complete") &&
           expect(dst.data() == payload, std::string(name) + ": received payload should match");
}

bool testEngineTransfersCommonPayloads()
{
    std::vector<uint8_t> empty;
    std::vector<uint8_t> oneByte = {0x18};
    std::vector<uint8_t> controls(300);
    for (std::size_t i = 0; i < controls.size(); ++i) {
        controls[i] = static_cast<uint8_t>(i);
    }
    std::vector<uint8_t> twoK(2048);
    for (std::size_t i = 0; i < twoK.size(); ++i) {
        twoK[i] = static_cast<uint8_t>(i * 13);
    }

    return runCoupledTransfer(empty, "empty") &&
           runCoupledTransfer(oneByte, "zdle-byte") &&
           runCoupledTransfer(controls, "control-bytes") &&
           runCoupledTransfer(twoK, "2k");
}

bool testReceiverStaysInReceivingState()
{
    fakeMillis = 0;
    FakeStream stream;
    File outputFile;
    ZModemEngine engine;
    engine.begin(stream);
    engine.setFileStream(&outputFile, "/received.bin", 0);
    if (!expect(engine.receive(10000), "receiver should start")) {
        return false;
    }
    engine.loop();
    return expect(engine.getState() == ZModemEngine::STATE_RECEIVING,
                  "receiver should stay in STATE_RECEIVING until the transfer finishes");
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
    const bool transferOk = testEngineTransfersCommonPayloads();
    const bool receiveStateOk = testReceiverStaysInReceivingState();

    if (!senderOk || !xmodemOk || !transferOk || !receiveStateOk) {
        return 1;
    }

    std::cout << "All ZModemEngine regression tests passed." << std::endl;
    return 0;
}