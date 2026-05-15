#pragma once

#include "Arduino.h"

using NodeNum = uint32_t;

static constexpr NodeNum BROADCAST_ADDR = 0xffffffffu;

enum MeshPacket_DataType {
    MeshPacket_DataType_TEXT_MESSAGE = 0,
    MeshPacket_DataType_OPAQUE = 1,
};

struct PayloadBuffer {
    std::vector<uint8_t> storage;

    const uint8_t* getBuffer() const
    {
        return storage.empty() ? nullptr : storage.data();
    }

    std::size_t length() const
    {
        return storage.size();
    }
};

struct DecodedPacket {
    PayloadBuffer payload;
    int portnum = 0;
    int datatype = MeshPacket_DataType_TEXT_MESSAGE;
};

class MeshPacket {
public:
    DecodedPacket decoded;
    NodeNum from = 0;
    NodeNum to = BROADCAST_ADDR;
    bool wantAck = false;
    uint8_t hopLimit = 0;

    void set_payload(const uint8_t* data, std::size_t length)
    {
        decoded.payload.storage.assign(data, data + length);
    }

    void set_to(NodeNum value) { to = value; }
    void set_portnum(int value) { decoded.portnum = value; }
    void set_datatype(int value) { decoded.datatype = value; }
    void set_want_ack(bool value) { wantAck = value; }
    void set_hop_limit(uint8_t value) { hopLimit = value; }
};

class ReceivedPacket : public MeshPacket {};

class Meshtastic {
public:
    uint8_t hopLimit = 3;
    std::vector<MeshPacket> sentPackets;

    bool sendPacket(MeshPacket* packet)
    {
        if (!packet) {
            return false;
        }

        sentPackets.push_back(*packet);
        return true;
    }

    uint8_t getHopLimit() const
    {
        return hopLimit;
    }

    void clearSentPackets()
    {
        sentPackets.clear();
    }
};