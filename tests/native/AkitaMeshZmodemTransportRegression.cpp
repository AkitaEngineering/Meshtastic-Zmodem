#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "AkitaMeshZmodem.h"

namespace {

unsigned long fakeMillis = 0;
FS SPIFFS;

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

std::array<uint8_t, 4> posFlags(uint32_t position)
{
    return {
        static_cast<uint8_t>(position & 0xFF),
        static_cast<uint8_t>((position >> 8) & 0xFF),
        static_cast<uint8_t>((position >> 16) & 0xFF),
        static_cast<uint8_t>((position >> 24) & 0xFF),
    };
}

MeshPacket makeIncomingPacket(NodeNum from, uint16_t sequence, const std::vector<uint8_t>& zmodemBytes)
{
    std::vector<uint8_t> wrapped;
    wrapped.reserve(zmodemBytes.size() + 3);
    wrapped.push_back(AKZ_PACKET_IDENTIFIER);
    wrapped.push_back(static_cast<uint8_t>((sequence >> 8) & 0xFF));
    wrapped.push_back(static_cast<uint8_t>(sequence & 0xFF));
    wrapped.insert(wrapped.end(), zmodemBytes.begin(), zmodemBytes.end());

    MeshPacket packet;
    packet.from = from;
    packet.set_payload(wrapped.data(), wrapped.size());
    packet.set_portnum(AKZ_ZMODEM_DATA_PORTNUM);
    packet.set_datatype(MeshPacket_DataType_OPAQUE);
    return packet;
}

bool expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << std::endl;
        return false;
    }
    return true;
}

bool testReceiverLearnsSenderAndFlushesBufferedZrinit()
{
    fakeMillis = 0;
    SPIFFS = FS();
    Meshtastic mesh;
    mesh.hopLimit = 5;

    AkitaMeshZmodem transfer;
    transfer.begin(mesh, SPIFFS, nullptr);
    if (!expect(transfer.startReceive("/rx.bin"), "receiver should start")) {
        return false;
    }

    if (!expect(mesh.sentPackets.empty(), "receiver should not emit a mesh packet before it learns the sender")) {
        return false;
    }

    MeshPacket probe = makeIncomingPacket(0x10203040u, 0, {0x00, 0x01, 0x02});
    transfer.processDataPacket(probe);

    if (!expect(mesh.sentPackets.size() == 1, "receiver should flush the buffered ZRINIT once the sender is known")) {
        return false;
    }

    const MeshPacket& reply = mesh.sentPackets.back();
    return expect(reply.to == 0x10203040u, "receiver reply should target the sender node") &&
           expect(reply.decoded.portnum == AKZ_ZMODEM_DATA_PORTNUM, "receiver reply should stay on the ZMODEM data port") &&
           expect(reply.decoded.datatype == MeshPacket_DataType_OPAQUE, "receiver reply should be marked as opaque binary") &&
           expect(reply.hopLimit == mesh.getHopLimit(), "receiver reply should inherit the Meshtastic hop limit") &&
           expect(reply.decoded.payload.length() <= AKZ_DEFAULT_MAX_PACKET_SIZE, "receiver reply should respect the Meshtastic payload limit");
}

bool testSenderUsesOpaqueMeshtasticPackets()
{
    fakeMillis = 0;
    SPIFFS = FS();
    std::vector<uint8_t> fileData(64, 0x5A);
    SPIFFS.setFileData("/tx.bin", fileData);

    Meshtastic mesh;
    mesh.hopLimit = 7;

    AkitaMeshZmodem transfer;
    transfer.begin(mesh, SPIFFS, nullptr);
    if (!expect(transfer.startSend("/tx.bin", 0x42u), "sender should start")) {
        return false;
    }

    advanceMillis(1001);
    transfer.loop();
    if (!expect(mesh.sentPackets.size() == 1, "sender should emit ZRQINIT on the first loop")) {
        return false;
    }

    const MeshPacket& packet = mesh.sentPackets.back();
    return expect(packet.to == 0x42u, "sender should target the requested destination") &&
           expect(packet.decoded.portnum == AKZ_ZMODEM_DATA_PORTNUM, "sender should use the ZMODEM data port") &&
           expect(packet.decoded.datatype == MeshPacket_DataType_OPAQUE, "sender should mark ZMODEM packets as opaque") &&
           expect(!packet.wantAck, "sender should leave mesh link ACKs disabled by default") &&
           expect(packet.hopLimit == mesh.getHopLimit(), "sender should inherit the Meshtastic hop limit") &&
           expect(packet.decoded.payload.length() <= AKZ_DEFAULT_MAX_PACKET_SIZE, "sender should stay within the Meshtastic payload budget");
}

bool testDefaultPacketBudgetMatchesMeshtasticPayload()
{
    return expect(AKZ_DEFAULT_MAX_PACKET_SIZE == AKZ_MESHTASTIC_MAX_PAYLOAD_LEN,
                  "default packet budget should match the Meshtastic decoded payload limit");
}

} // namespace

unsigned long millis()
{
    return fakeMillis;
}

int main()
{
    const bool receiverOk = testReceiverLearnsSenderAndFlushesBufferedZrinit();
    const bool senderOk = testSenderUsesOpaqueMeshtasticPackets();
    const bool budgetOk = testDefaultPacketBudgetMatchesMeshtasticPayload();

    if (!receiverOk || !senderOk || !budgetOk) {
        return 1;
    }

    std::cout << "All AkitaMeshZmodem transport regression tests passed." << std::endl;
    return 0;
}