#ifndef MESHTASTIC_H
#define MESHTASTIC_H

#include <Arduino.h>

using NodeNum = uint32_t;
constexpr NodeNum BROADCAST_ADDR = 0;

#define MeshPacket_DataType_OPAQUE 0
#define MeshPacket_DataType_TEXT_MESSAGE 1
#define PortNum_TEXT_MESSAGE_APP 1

struct Payload {
    uint8_t* _buf = nullptr;
    size_t _len = 0;
    const uint8_t* getBuffer() const { return _buf; }
    size_t length() const { return _len; }
};

struct DecodedPacket {
    Payload payload;
    int portnum = 0;
    int datatype = 0;
};

class MeshPacket {
public:
    DecodedPacket decoded;
    NodeNum from = 0;
    void set_payload(const uint8_t* data, size_t len) { (void)data; (void)len; }
    void set_to(NodeNum) {}
    void set_from(NodeNum) {}
    void set_portnum(int) {}
    void set_datatype(int) {}
    void set_want_ack(bool) {}
    void set_hop_limit(int) {}
};

struct ReceivedPacket : public MeshPacket {
    bool isValid = true;
};

class Meshtastic {
public:
    bool sendPacket(MeshPacket*) { return true; }
    NodeNum getNodeNum() const { return 0; }
    int getHopLimit() const { return 3; }
    bool available() { return false; }
    ReceivedPacket receive() { return ReceivedPacket(); }
    void releaseReceiveBuffer() {}
};

#endif // MESHTASTIC_H
