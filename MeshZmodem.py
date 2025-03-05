#include "Meshtastic.h"
#include <ZModem.h>
#include <StreamUtils.h>

// Configuration
#define ZMODEM_BUFFER_SIZE 1024
#define ZMODEM_TIMEOUT 10000 // Increased timeout for LoRa
#define MAX_PACKET_SIZE 230 // Meshtastic payload limit (adjust if needed)

// ZModem Instance
ZModem zmodem;
uint8_t zmodemBuffer[ZMODEM_BUFFER_SIZE];
StreamBuffer zmodemStream(zmodemBuffer, ZMODEM_BUFFER_SIZE);

// Transfer State
enum TransferState {
    IDLE,
    RECEIVING,
    SENDING
};

TransferState currentState = IDLE;
String filename = "";
File transferFile;

// Meshtastic Stream Class
class MeshtasticZModemStream : public Stream {
public:
    MeshtasticZModemStream() : bufferIndex(0), bufferSize(0), packetId(0) {}

    virtual int available() {
        if (bufferIndex < bufferSize) {
            return bufferSize - bufferIndex;
        }
        if (mesh.available()) {
            ReceivedPacket packet = mesh.receive();
            if (packet.decoded.payload.length() > 0) {
                if (packet.decoded.payload[0] == 0xFF) { // Check for ZModem packet identifier
                    uint16_t receivedPacketId = (packet.decoded.payload[1] << 8) | packet.decoded.payload[2];
                    if (receivedPacketId == packetId) { // Check packet ID
                        bufferSize = packet.decoded.payload.length() - 3;
                        memcpy(buffer, packet.decoded.payload.getBuffer() + 3, bufferSize);
                        bufferIndex = 0;
                        packetId++;
                        return bufferSize;
                    }
                }
            }
        }
        return 0;
    }

    virtual int read() {
        if (available() > 0) {
            return buffer[bufferIndex++];
        }
        return -1;
    }

    virtual size_t write(uint8_t val) {
        sendBuffer[sendBufferIndex++] = val;
        if (sendBufferIndex >= MAX_PACKET_SIZE - 3) {
            sendPacket();
        }
        return 1;
    }

    virtual int peek() {
        if (available() > 0) {
            return buffer[bufferIndex];
        }
        return -1;
    }

    virtual void flush() {
        if (sendBufferIndex > 0) {
            sendPacket();
        }
    }

private:
    void sendPacket() {
        uint8_t packet[MAX_PACKET_SIZE];
        packet[0] = 0xFF; // ZModem packet identifier
        packet[1] = (sendPacketId >> 8) & 0xFF; // Packet ID MSB
        packet[2] = sendPacketId & 0xFF; // Packet ID LSB
        memcpy(packet + 3, sendBuffer, sendBufferIndex);
        mesh.sendData(packet, sendBufferIndex + 3);
        sendBufferIndex = 0;
        sendPacketId++;
    }

    uint8_t buffer[MAX_PACKET_SIZE];
    uint16_t bufferIndex;
    uint16_t bufferSize;
    uint16_t packetId = 0;

    uint8_t sendBuffer[MAX_PACKET_SIZE];
    uint16_t sendBufferIndex = 0;
    uint16_t sendPacketId = 0;
};

MeshtasticZModemStream meshtasticStream;

// Function Prototypes
void handleZModemReceive();
void handleZModemSend(const String& filename);

void setup() {
    Serial.begin(115200);
    delay(1000);

    mesh.init();
    mesh.setDebugOutputStream(&Serial);
    mesh.setNodeInfo("ZModem Bridge", 0);

    if (!SPIFFS.begin()) {
        Serial.println("Failed to mount SPIFFS");
        return;
    }

    zmodem.begin(&zmodemStream, &meshtasticStream);
}

void loop() {
    if (mesh.available()) {
        ReceivedPacket packet = mesh.receive();
        if (packet.decoded.payload.length() > 0) {
            String message = packet.decoded.payload.toString();

            if (message.startsWith("ZMODEM_RECEIVE:")) {
                filename = message.substring(16);
                handleZModemReceive();
            } else if (message.startsWith("ZMODEM_SEND:")) {
                filename = message.substring(13);
                handleZModemSend(filename);
            }
        }
    }

    if (currentState == RECEIVING) {
        if (zmodem.loop() == ZModem::TransferState::COMPLETE) {
            Serial.println("ZModem receive complete.");
            currentState = IDLE;
            transferFile.close();
        } else if (zmodem.loop() == ZModem::TransferState::ERROR) {
            Serial.println("Zmodem receive error.");
            currentState = IDLE;
            transferFile.close();
        }
    }

    if (currentState == SENDING) {
        if (zmodem.loop() == ZModem::TransferState::COMPLETE) {
            Serial.println("ZModem send complete.");
            currentState = IDLE;
            transferFile.close();
        } else if (zmodem.loop() == ZModem::TransferState::ERROR) {
            Serial.println("Zmodem send error.");
            currentState = IDLE;
            transferFile.close();
        }
    }

    delay(100);
}

void handleZModemReceive() {
    Serial.println("Starting ZModem receive...");
    transferFile = SPIFFS.open(filename, FILE_WRITE);

    if (!transferFile) {
        Serial.println("Failed to open file for writing.");
        return;
    }

    zmodem.setTransferStream(&transferFile);
    currentState = RECEIVING;
    zmodem.startReceive(ZMODEM_TIMEOUT);
}

void handleZModemSend(const String& filename) {
    Serial.println("Starting ZModem send...");
    transferFile = SPIFFS.open(filename, FILE_READ);

    if (!transferFile) {
        Serial.println("Failed to open file for reading.");
        return;
    }

    zmodem.setTransferStream(&transferFile);
    currentState = SENDING;
    zmodem.startSend(ZMODEM_TIMEOUT);
}
