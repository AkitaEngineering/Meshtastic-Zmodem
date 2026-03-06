#ifndef MESHTASTIC_H
#define MESHTASTIC_H

#include <Arduino.h>

using NodeNum = uint32_t;
constexpr NodeNum BROADCAST_ADDR = 0;

#define MeshPacket_DataType_OPAQUE 0
#define MeshPacket_DataType_TEXT_MESSAGE 1

struct Payload {
    uint8_t* _buf = nullptr;
    size_t _len = 0;
    const uint8_t* getBuffer() const { return _buf; }
    size_t length() const { return _len; }
};

struct DecodedPacket {
    Payload payload;
    int datatype = 0;
};

class MeshPacket {
public:
    DecodedPacket decoded;
    void set_payload(const uint8_t* data, size_t len) {
        // no-op stub
    }
    void set_to(NodeNum) {}
    void set_portnum(int) {}
    void set_want_ack(bool) {}
    void set_hop_limit(int) {}
};

class Meshtastic {
public:
    bool sendPacket(MeshPacket*) { return true; }
};

#endif // MESHTASTIC_H
