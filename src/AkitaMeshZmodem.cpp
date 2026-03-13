/**
 * @file AkitaMeshZmodem.cpp
 * @brief Implementation using internal ZModemEngine.
 * @version 1.1.0
 */

#include "AkitaMeshZmodem.h"
#include "AkitaMeshZmodemConfig.h"

// --- MeshtasticZModemStream (Transport Layer) ---

class MeshtasticZModemStream : public Stream {
private:
    Meshtastic* _mesh;
    Stream* _debug;
    size_t _maxPacketSize;
    uint8_t _packetIdentifier;
    NodeNum _destinationNodeId = BROADCAST_ADDR;
    uint8_t _rxBuffer[AKZ_STREAM_RX_BUFFER_SIZE];
    uint16_t _rxBufferIndex = 0;
    uint16_t _rxBufferSize = 0;
    uint16_t _expectedPacketId = 0;
    uint8_t _txBuffer[AKZ_STREAM_TX_BUFFER_SIZE];
    uint16_t _txBufferIndex = 0;
    uint16_t _sentPacketId = 0;

    void _streamLog(const char* msg) { if(_debug) { _debug->print("MeshStream: "); _debug->println(msg); } }

    bool sendPacket() {
        if (_txBufferIndex == 0 || !_mesh) return true;
        if (_destinationNodeId == BROADCAST_ADDR) return false;

        // Use a fixed-size packet buffer (avoid VLA). Ensure we don't exceed
        // either the configured max packet size or the internal TX buffer size.
        uint8_t packet[AKZ_STREAM_TX_BUFFER_SIZE];
        // Ensure _maxPacketSize is reasonable to avoid underflow when subtracting header bytes
        size_t effectiveMaxPacket = (_maxPacketSize < 4) ? 4 : _maxPacketSize;
        size_t maxPayload = (effectiveMaxPacket < AKZ_STREAM_TX_BUFFER_SIZE) ? (effectiveMaxPacket - 3) : (AKZ_STREAM_TX_BUFFER_SIZE - 3);

        packet[0] = _packetIdentifier;
        packet[1] = (_sentPacketId >> 8) & 0xFF;
        packet[2] = _sentPacketId & 0xFF;

        size_t dataLen = _txBufferIndex;
        if (dataLen > maxPayload) dataLen = maxPayload;

        memcpy(packet + 3, _txBuffer, dataLen);

        MeshPacket genericPacket;
        genericPacket.set_payload(packet, dataLen + 3);
        genericPacket.set_to(_destinationNodeId);
        genericPacket.set_portnum(AKZ_ZMODEM_DATA_PORTNUM);
        genericPacket.set_want_ack(false);
        genericPacket.set_hop_limit(3);

        bool success = _mesh->sendPacket(&genericPacket);
        if (success) {
            _sentPacketId++;
            _txBufferIndex = 0;
        }
        return success;
    }

public:
    MeshtasticZModemStream(Meshtastic* m, Stream* d, size_t s, uint8_t i) 
        : _mesh(m), _debug(d), _maxPacketSize(s), _packetIdentifier(i) {}
    
    void setDestination(NodeNum d) { _destinationNodeId = d; }
    
    void pushPacket(MeshPacket& packet) {
        if (_rxBufferIndex < _rxBufferSize) return;
        const uint8_t* p = packet.decoded.payload.getBuffer();
        if (packet.decoded.payload.length() < 3 || p[0] != _packetIdentifier) return;
        
        uint16_t pid = (p[1] << 8) | p[2];
        if (pid == _expectedPacketId) {
            _rxBufferSize = packet.decoded.payload.length() - 3;
            if(_rxBufferSize > AKZ_STREAM_RX_BUFFER_SIZE) _rxBufferSize = 0;
            else {
                memcpy(_rxBuffer, p + 3, _rxBufferSize);
                _rxBufferIndex = 0;
                _expectedPacketId++;
            }
        }
    }

    virtual int available() override { return _rxBufferSize - _rxBufferIndex; }
    virtual int read() override { return available() ? _rxBuffer[_rxBufferIndex++] : -1; }
    virtual int peek() override { return available() ? _rxBuffer[_rxBufferIndex] : -1; }
    virtual size_t write(uint8_t val) override {
        if (!_mesh || _destinationNodeId == BROADCAST_ADDR) return 0;

        // Prevent overflow of the internal TX buffer. If full, try to flush
        // the pending packet first; if still full, fail the write.
        if (_txBufferIndex >= AKZ_STREAM_TX_BUFFER_SIZE) {
            flush();
            if (_txBufferIndex >= AKZ_STREAM_TX_BUFFER_SIZE) return 0;
        }

        _txBuffer[_txBufferIndex++] = val;
        // Guard against underflow and ensure we don't exceed the max packet payload
        size_t effectiveMaxPacket2 = (_maxPacketSize < 4) ? 4 : _maxPacketSize;
        if (_txBufferIndex >= effectiveMaxPacket2 - 3) flush();
        return 1;
    }
    virtual void flush() override { sendPacket(); }
    void reset() { _rxBufferIndex=0; _rxBufferSize=0; _txBufferIndex=0; _expectedPacketId=0; _sentPacketId=0; _destinationNodeId=BROADCAST_ADDR; }
};

// --- AkitaMeshZmodem Implementation ---

AkitaMeshZmodem::AkitaMeshZmodem() {}
AkitaMeshZmodem::~AkitaMeshZmodem() { delete _meshStream; }

void AkitaMeshZmodem::begin(Meshtastic& meshInstance, FS& filesystem, Stream* debugStream) {
    _mesh = &meshInstance;
    _fs = &filesystem;
    _debug = debugStream;
    
    if (!_fs) { _logError("FS Invalid"); return; }
    
    delete _meshStream;
    _meshStream = new MeshtasticZModemStream(_mesh, _debug, _maxPacketSize, AKZ_PACKET_IDENTIFIER);
    
    _zmodem.begin(*_meshStream);
    _log("Akita ZModem Initialized (Internal Engine)");
    _resetTransferState();
}

void AkitaMeshZmodem::_resetTransferState() {
    if (_transferFile) _transferFile.close();
    _currentState = TransferState::IDLE;
    _bytesTransferred = 0;
    _totalFileSize = 0;
    _filename = "";
    _destinationNodeId = BROADCAST_ADDR;
    if(_meshStream) _meshStream->reset();
    _zmodem.abort(); // Reset engine state
}

void AkitaMeshZmodem::processDataPacket(MeshPacket& packet) {
    if(_meshStream && (_currentState == TransferState::RECEIVING || _currentState == TransferState::SENDING)) {
        _meshStream->pushPacket(packet);
    }
}

bool AkitaMeshZmodem::startSend(const String& filePath, NodeNum dest) {
    if (_currentState != TransferState::IDLE || dest == BROADCAST_ADDR) return false;
    _resetTransferState();
    
    _transferFile = _fs->open(filePath, FILE_READ);
    if (!_transferFile || _transferFile.isDirectory()) return false;
    
    _filename = filePath;
    _totalFileSize = _transferFile.size();
    _destinationNodeId = dest;
    _meshStream->setDestination(dest);
    
    _zmodem.setFileStream(&_transferFile, _filename, _totalFileSize);
    if(_zmodem.send(_zmodemTimeout)) {
        _currentState = TransferState::SENDING;
        _transferStartTime = millis();
        {
            char buf[160];
            snprintf(buf, sizeof(buf), "Starting Send to 0x%lX for: %s", (unsigned long)dest, filePath.c_str());
            _log(buf);
        }
        return true;
    }
    return false;
}

bool AkitaMeshZmodem::startReceive(const String& filePath) {
    if (_currentState != TransferState::IDLE) return false;
    _resetTransferState();
    
    _transferFile = _fs->open(filePath, FILE_WRITE);
    if (!_transferFile) return false;
    
    _filename = filePath;
    _zmodem.setFileStream(&_transferFile, _filename, 0);
    
    if(_zmodem.receive(_zmodemTimeout)) {
        _currentState = TransferState::RECEIVING;
        _transferStartTime = millis();
        {
            char buf[160];
            snprintf(buf, sizeof(buf), "Starting Receive to: %s", filePath.c_str());
            _log(buf);
        }
        return true;
    }
    return false;
}

// C-string overloads (convenience wrappers to avoid callers allocating Arduino Strings)
bool AkitaMeshZmodem::startSend(const char* filePath, NodeNum dest) {
    if (!filePath) return false;
    return startSend(String(filePath), dest);
}

bool AkitaMeshZmodem::startReceive(const char* filePath) {
    if (!filePath) return false;
    return startReceive(String(filePath));
}

void AkitaMeshZmodem::abortTransfer() {
    _zmodem.abort();
    _resetTransferState();
}

AkitaMeshZmodem::TransferState AkitaMeshZmodem::loop() {
    if (_currentState == TransferState::IDLE || _currentState == TransferState::COMPLETE || _currentState == TransferState::ERROR) return _currentState;

    int res = _zmodem.loop();

    // Mirror ZModem engine state into our public TransferState for better observability
    _handleZmodemState((int)_zmodem.getState());

    // Update progress markers
    _bytesTransferred = _zmodem.getBytesTransferred();
    _totalFileSize = _zmodem.getFileSize(); // Try to get size from receiver status
    _updateProgress();

    if (res == 1) {
        _currentState = TransferState::COMPLETE;
        _log("Transfer Complete!");
        _transferFile.close();
    } else if (res == -1) {
        _currentState = TransferState::ERROR;
        _logError("Transfer Error (ZModem Engine reported failure)");
        _transferFile.close();
    }

    return _currentState;
}


// Map internal ZModem engine states to the public TransferState and log transitions
void AkitaMeshZmodem::_handleZmodemState(int zState) {
    // Don't override terminal states (COMPLETE/ERROR) here — loop() handles those.
    switch(static_cast<ZModemEngine::State>(zState)) {
        case ZModemEngine::STATE_SEND_ZRQINIT:
        case ZModemEngine::STATE_SEND_ZFILE:
        case ZModemEngine::STATE_SEND_ZDATA:
        case ZModemEngine::STATE_SEND_ZEOF:
        case ZModemEngine::STATE_SEND_ZFIN:
        case ZModemEngine::STATE_AWAIT_ZRINIT:
        case ZModemEngine::STATE_AWAIT_ZRPOS:
        case ZModemEngine::STATE_AWAIT_ZFIN:
            _currentState = TransferState::SENDING;
            break;

        default:
            // leave _currentState unchanged for other intermediary states
            break;
    }
}

// Getters & Setters
AkitaMeshZmodem::TransferState AkitaMeshZmodem::getCurrentState() const { return _currentState; }
size_t AkitaMeshZmodem::getBytesTransferred() const { return _bytesTransferred; }
size_t AkitaMeshZmodem::getTotalFileSize() const { return _totalFileSize > 0 ? _totalFileSize : _zmodem.getFileSize(); }
String AkitaMeshZmodem::getFilename() const { return _filename; }
void AkitaMeshZmodem::setTimeout(unsigned long t) { _zmodemTimeout = t; }
void AkitaMeshZmodem::setMaxPacketSize(size_t s) { _maxPacketSize = s; }
void AkitaMeshZmodem::setProgressUpdateInterval(unsigned long i) { _progressUpdateInterval = i; }

void AkitaMeshZmodem::_updateProgress() {
    if (_progressUpdateInterval > 0 && millis() - _lastProgressUpdate > _progressUpdateInterval) {
        char buf[128];
        if (getTotalFileSize() > 0) {
            float percent = (float)_bytesTransferred / getTotalFileSize() * 100.0f;
            snprintf(buf, sizeof(buf), "Progress: %lu bytes (%.1f%%)", (unsigned long)_bytesTransferred, percent);
        } else {
            snprintf(buf, sizeof(buf), "Progress: %lu bytes", (unsigned long)_bytesTransferred);
        }
        _log(buf);
        _lastProgressUpdate = millis();
    }
}

void AkitaMeshZmodem::_log(const char* msg) { if(_debug) { _debug->print("[Akita] "); _debug->println(msg); } }
void AkitaMeshZmodem::_logError(const char* msg) { if(_debug) { _debug->print("[Akita ERR] "); _debug->println(msg); } }
