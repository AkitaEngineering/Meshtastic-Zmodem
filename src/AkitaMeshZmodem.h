/**
 * @file AkitaMeshZmodem.h
 * @brief Main header file for Akita Meshtastic Zmodem Library.
 * Uses internal ZModemEngine for zero external dependencies.
 * @version 1.1.0
 */

#ifndef AKITA_MESH_ZMODEM_H
#define AKITA_MESH_ZMODEM_H

#include <Arduino.h>
#include <Meshtastic.h>
#include <StreamUtils.h>
#include <FS.h>
#include "AkitaMeshZmodemConfig.h"
#include "utility/ZModemEngine.h" // Use internal engine

class MeshtasticZModemStream;

class AkitaMeshZmodem {
public:
    enum class TransferState {
        IDLE, RECEIVING, SENDING, COMPLETE, ERROR
    };

    AkitaMeshZmodem();
    ~AkitaMeshZmodem();

    void begin(Meshtastic& meshInstance, FS& filesystem = SPIFFS, Stream* debugStream = nullptr);
    TransferState loop();
    void processDataPacket(MeshPacket& packet);

    bool startSend(const String& filePath, NodeNum destinationNodeId);
    bool startReceive(const String& filePath);
    void abortTransfer();

    TransferState getCurrentState() const;
    size_t getBytesTransferred() const;
    size_t getTotalFileSize() const;
    String getFilename() const;

    // Config setters
    void setTimeout(unsigned long timeoutMs);
    void setProgressUpdateInterval(unsigned long intervalMs);
    void setMaxPacketSize(size_t maxSize);

private:
    Meshtastic* _mesh = nullptr;
    FS* _fs = nullptr;
    Stream* _debug = nullptr;
    MeshtasticZModemStream* _meshStream = nullptr;
    ZModemEngine _zmodem; // Internal engine instance

    File _transferFile;
    String _filename = "";
    TransferState _currentState = TransferState::IDLE;
    NodeNum _destinationNodeId = BROADCAST_ADDR;

    size_t _totalFileSize = 0;
    size_t _bytesTransferred = 0;

    unsigned long _zmodemTimeout = AKZ_DEFAULT_ZMODEM_TIMEOUT;
    unsigned long _progressUpdateInterval = AKZ_DEFAULT_PROGRESS_UPDATE_INTERVAL;
    size_t _maxPacketSize = AKZ_DEFAULT_MAX_PACKET_SIZE;
    unsigned long _lastProgressUpdate = 0;
    unsigned long _transferStartTime = 0;

    void _resetTransferState();
    void _updateProgress();
    void _handleZmodemState(int zState); // Adjusted signature
    void _log(const String& message);
    void _logError(const String& message);
};

#endif // AKITA_MESH_ZMODEM_H
