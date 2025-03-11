#include "Meshtastic.h"
#include <ZModem.h>
#include <StreamUtils.h>

// Configuration
#define ZMODEM_BUFFER_SIZE 1024
#define ZMODEM_TIMEOUT 30000 // Increased timeout for LoRa (milliseconds)
#define MAX_PACKET_SIZE 230 // Meshtastic payload limit (adjust if needed)
#define PROGRESS_UPDATE_INTERVAL 5000 // Milliseconds

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
unsigned long lastProgressUpdate = 0;
size_t totalFileSize = 0;
size_t bytesTransferred = 0;
uint16_t retryCount = 0;
uint16_t maxRetryCount = 3;

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
                    } else {
                        Serial.println("Packet ID mismatch.");
                        return 0;
                    }
                } else {
                    Serial.println("Not a ZModem packet.");
                    return 0;
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
void displayProgress(size_t transferred, size_t total);

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
        ZModem::TransferState state = zmodem.loop();
        if (state == ZModem::TransferState::COMPLETE) {
            Serial.println("ZModem receive complete.");
            currentState = IDLE;
            transferFile.close();
            retryCount = 0;
        } else if (state == ZModem::TransferState::ERROR) {
            Serial.println("Zmodem receive error.");
            if (retryCount < maxRetryCount) {
                Serial.print("Retrying... (Attempt ");
                Serial.print(retryCount + 1);
                Serial.println(")");
                zmodem.startReceive(ZMODEM_TIMEOUT);
                retryCount++;
            } else {
                Serial.println("Max retry count reached. Aborting.");
                currentState = IDLE;
                transferFile.close();
                retryCount = 0;
            }
        } else if (state == ZModem::TransferState::TRANSFERRING) {
            bytesTransferred = transferFile.position();
            displayProgress(bytesTransferred, totalFileSize);
        }
    }

    if (currentState == SENDING) {
        ZModem::TransferState state = zmodem.loop();
        if (state == ZModem::TransferState::COMPLETE) {
            Serial.println("ZModem send complete.");
            currentState = IDLE;
            transferFile.close();
            retryCount = 0;
        } else if (state == ZModem::TransferState::ERROR) {
            Serial.println("Zmodem send error.");
            if (retryCount < maxRetryCount) {
                Serial.print("Retrying... (Attempt ");
                Serial.print(retryCount + 1);
                Serial.println(")");
                zmodem.startSend(ZMODEM_TIMEOUT);
                retryCount++;
            } else {
                Serial.println("Max retry count reached. Aborting.");
                currentState = IDLE;
                transferFile.close();
                retryCount = 0;
            }
        } else if (state == ZModem::TransferState::TRANSFERRING) {
            bytesTransferred = transferFile.position();
            displayProgress(bytesTransferred, totalFileSize);
        }
    }

    delay(100);
}

void handleZModemReceive() {
    Serial.print("Starting ZModem receive for: ");
    Serial.println(filename);
    transferFile = SPIFFS.open(filename, FILE_WRITE);

    if (!transferFile) {
        Serial.println("Failed to open file for writing.");
        return;
    }

    zmodem.setTransferStream(&transferFile);
    currentState = RECEIVING;
    zmodem.startReceive(ZMODEM_TIMEOUT);
    totalFileSize = 0; // Unknown for receive
    bytesTransferred = 0;
}

void handleZModemSend(const String& filename) {
    Serial.print("Starting ZModem send for: ");
    Serial.println(filename);
    transferFile = SPIFFS.open(filename, FILE_READ);

    if (!transferFile) {
        Serial.println("Failed to open file for reading.");
        return;
    }

    zmodem.setTransferStream(&transferFile);
    currentState = SENDING;#include "Meshtastic.h"
#include <ZModem.h>
#include <StreamUtils.h>

// Configuration
#define ZMODEM_BUFFER_SIZE 1024
#define ZMODEM_TIMEOUT 30000 // Increased timeout for LoRa (milliseconds)
#define MAX_PACKET_SIZE 230 // Meshtastic payload limit (adjust if needed)
#define PROGRESS_UPDATE_INTERVAL 5000 // Milliseconds

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
unsigned long lastProgressUpdate = 0;
size_t totalFileSize = 0;
size_t bytesTransferred = 0;
uint16_t retryCount = 0;
uint16_t maxRetryCount = 3;

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
                    } else {
                        Serial.println("Packet ID mismatch.");
                        return 0;
                    }
                } else {
                    Serial.println("Not a ZModem packet.");
                    return 0;
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
void displayProgress(size_t transferred, size_t total);

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
        ZModem::TransferState state = zmodem.loop();
        if (state == ZModem::TransferState::COMPLETE) {
            Serial.println("ZModem receive complete.");
            currentState = IDLE;
            transferFile.close();
            retryCount = 0;
        } else if (state == ZModem::TransferState::ERROR) {
            Serial.println("Zmodem receive error.");
            if (retryCount < maxRetryCount) {
                Serial.print("Retrying... (Attempt ");
                Serial.print(retryCount + 1);
                Serial.println(")");
                zmodem.startReceive(ZMODEM_TIMEOUT);
                retryCount++;
            } else {
                Serial.println("Max retry count reached. Aborting.");
                currentState = IDLE;
                transferFile.close();
                retryCount = 0;
            }
        } else if (state == ZModem::TransferState::TRANSFERRING) {
            bytesTransferred = transferFile.position();
            displayProgress(bytesTransferred, totalFileSize);
        }
    }

    if (currentState == SENDING) {
        ZModem::TransferState state = zmodem.loop();
        if (state == ZModem::TransferState::COMPLETE) {
            Serial.println("ZModem send complete.");
            currentState = IDLE;
            transferFile.close();
            retryCount = 0;
        } else if (state == ZModem::TransferState::ERROR) {
            Serial.println("Zmodem send error.");
            if (retryCount < maxRetryCount) {
                Serial.print("Retrying... (Attempt ");
                Serial.print(retryCount + 1);
                Serial.println(")");
                zmodem.startSend(ZMODEM_TIMEOUT);
                retryCount++;
            } else {
                Serial.println("Max retry count reached. Aborting.");
                currentState = IDLE;
                transferFile.close();
                retryCount = 0;
            }
        } else if (state == ZModem::TransferState::TRANSFERRING) {
            bytesTransferred = transferFile.position();
            displayProgress(bytesTransferred, totalFileSize);
        }
    }

    delay(100);
}

void handleZModemReceive() {
    Serial.print("Starting ZModem receive for: ");
    Serial.println(filename);
    transferFile = SPIFFS.open(filename, FILE_WRITE);

    if (!transferFile) {
        Serial.println("Failed to open file for writing.");
        return;
    }

    zmodem.setTransferStream(&transferFile);
    currentState = RECEIVING;
    zmodem.startReceive(ZMODEM_TIMEOUT);
    totalFileSize = 0; // Unknown for receive
    bytesTransferred = 0;
}

void handleZModemSend(const String& filename) {
    Serial.print("Starting ZModem send for: ");
    Serial.println(filename);
    transferFile = SPIFFS.open(filename, FILE_READ);

    if (!transferFile) {
        Serial.println("Failed to open file for reading.");
        return;
    }

    zmodem.setTransferStream(&transferFile);
    currentState = SENDING;#include "Meshtastic.h"
#include <ZModem.h>
#include <StreamUtils.h>

// Configuration
#define ZMODEM_BUFFER_SIZE 1024
#define ZMODEM_TIMEOUT 30000 // Increased timeout for LoRa (milliseconds)
#define MAX_PACKET_SIZE 230 // Meshtastic payload limit (adjust if needed)
#define PROGRESS_UPDATE_INTERVAL 5000 // Milliseconds

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
unsigned long lastProgressUpdate = 0;
size_t totalFileSize = 0;
size_t bytesTransferred = 0;
uint16_t retryCount = 0;
uint16_t maxRetryCount = 3;

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
                    } else {
                        Serial.println("Packet ID mismatch.");
                        return 0;
                    }
                } else {
                    Serial.println("Not a ZModem packet.");
                    return 0;
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
void displayProgress(size_t transferred, size_t total);

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
        ZModem::TransferState state = zmodem.loop();
        if (state == ZModem::TransferState::COMPLETE) {
            Serial.println("ZModem receive complete.");
            currentState = IDLE;
            transferFile.close();
            retryCount = 0;
        } else if (state == ZModem::TransferState::ERROR) {
            Serial.println("Zmodem receive error.");
            if (retryCount < maxRetryCount) {
                Serial.print("Retrying... (Attempt ");
                Serial.print(retryCount + 1);
                Serial.println(")");
                zmodem.startReceive(ZMODEM_TIMEOUT);
                retryCount++;
            } else {
                Serial.println("Max retry count reached. Aborting.");
                currentState = IDLE;
                transferFile.close();
                retryCount = 0;
            }
        } else if (state == ZModem::TransferState::TRANSFERRING) {
            bytesTransferred = transferFile.position();
            displayProgress(bytesTransferred, totalFileSize);
        }
    }

    if (currentState == SENDING) {
        ZModem::TransferState state = zmodem.loop();
        if (state == ZModem::TransferState::COMPLETE) {
            Serial.println("ZModem send complete.");
            currentState = IDLE;
            transferFile.close();
            retryCount = 0;
        } else if (state == ZModem::TransferState::ERROR) {
            Serial.println("Zmodem send error.");
            if (retryCount < maxRetryCount) {
                Serial.print("Retrying... (Attempt ");
                Serial.print(retryCount + 1);
                Serial.println(")");
                zmodem.startSend(ZMODEM_TIMEOUT);
                retryCount++;
            } else {
                Serial.println("Max retry count reached. Aborting.");
                currentState = IDLE;
                transferFile.close();
                retryCount = 0;
            }
        } else if (state == ZModem::TransferState::TRANSFERRING) {
            bytesTransferred = transferFile.position();
            displayProgress(bytesTransferred, totalFileSize);
        }
    }

    delay(100);
}

void handleZModemReceive() {
    Serial.print("Starting ZModem receive for: ");
    Serial.println(filename);
    transferFile = SPIFFS.open(filename, FILE_WRITE);

    if (!transferFile) {
        Serial.println("Failed to open file for writing.");
        return;
    }

    zmodem.setTransferStream(&transferFile);
    currentState = RECEIVING;
    zmodem.startReceive(ZMODEM_TIMEOUT);
    totalFileSize = 0; // Unknown for receive
    bytesTransferred = 0;
}

void handleZModemSend(const String& filename) {
    Serial.print("Starting ZModem send for: ");
    Serial.println(filename);
    transferFile = SPIFFS.open(filename, FILE_READ);

    if (!transferFile) {
        Serial.println("Failed to open file for reading.");
        return;
    }

    zmodem.setTransferStream(&transferFile);
    currentState = SENDING;zmodem.startSend(ZMODEM_TIMEOUT);
    totalFileSize = transferFile.size();
    bytesTransferred = 0;
}

void displayProgress(size_t transferred, size_t total) {
    unsigned long currentTime = millis();
    if (currentTime - lastProgressUpdate >= PROGRESS_UPDATE_INTERVAL) {
        if (total > 0) {
            float progress = (float)transferred / total * 100.0f;
            Serial.print("Progress: ");
            Serial.print(progress);
            Serial.println("%");
        } else {
            Serial.print("Bytes transferred: ");
            Serial.println(transferred);
        }
        lastProgressUpdate = currentTime;
    }
}
