/**
 * @file AkitaMeshZmodem.cpp
 * @author Akita Engineering
 * @brief Implementation file for the Akita Meshtastic Zmodem Arduino Library.
 * @version 1.0.0
 * @date 2025-04-26
 *
 * @copyright Copyright (c) 2025 Akita Engineering
 *
 */

#include "AkitaMeshZmodem.h"
#include "AkitaMeshZmodemConfig.h" // Include defaults

// ==========================================================================
// ==                     MeshtasticZModemStream Class                     ==
// ==========================================================================
// This internal class wraps the Meshtastic communication channel in a Stream
// interface suitable for the ZModem library. It handles packetization,
// packet ID checking, and buffering.

class MeshtasticZModemStream : public Stream {
private:
    Meshtastic* _mesh;          // Pointer to the Meshtastic instance
    Stream* _debug;             // Optional debug stream
    size_t _maxPacketSize;      // Max payload size per Meshtastic packet
    uint8_t _packetIdentifier;  // Byte to identify our packets

    // Receive Buffer
    uint8_t _rxBuffer[AKZ_STREAM_RX_BUFFER_SIZE]; // Buffer for incoming packet data
    uint16_t _rxBufferIndex = 0; // Current read position in _rxBuffer
    uint16_t _rxBufferSize = 0;  // Number of valid bytes currently in _rxBuffer
    uint16_t _expectedPacketId = 0; // ID expected for the next incoming packet

    // Transmit Buffer
    uint8_t _txBuffer[AKZ_STREAM_TX_BUFFER_SIZE]; // Buffer for outgoing data before packetization
    uint16_t _txBufferIndex = 0; // Current write position in _txBuffer
    uint16_t _sentPacketId = 0;  // ID for the next outgoing packet

    /**
     * @brief Internal logging helper for the stream class.
     */
    void _streamLog(const char* msg) {
        if (_debug) {
            _debug->print("MeshStream: ");
            _debug->println(msg);
        }
    }
    void _streamLog(const String& msg) {
        if (_debug) {
            _debug->print("MeshStream: ");
            _debug->println(msg);
        }
    }

    /**
     * @brief Sends the contents of the transmit buffer (_txBuffer) as a Meshtastic packet.
     * Prepends the packet identifier and current send packet ID.
     * @return true if the packet was successfully queued for sending by Meshtastic.
     * @return false if sending failed.
     */
    bool sendPacket() {
        if (_txBufferIndex == 0 || !_mesh) {
            return true; // Nothing to send or no mesh connection
        }

        // Ensure packet doesn't exceed max size (3 bytes header + data)
        if (_txBufferIndex + 3 > _maxPacketSize) {
            _streamLog("Error: TX buffer data exceeds max packet size! Truncating.");
            _txBufferIndex = _maxPacketSize - 3; // Adjust index to fit
        }

        // Prepare packet buffer
        uint8_t packet[_maxPacketSize]; // Use stack allocation for the packet buffer
        packet[0] = _packetIdentifier;           // ZModem packet identifier
        packet[1] = (_sentPacketId >> 8) & 0xFF; // Packet ID MSB
        packet[2] = _sentPacketId & 0xFF;        // Packet ID LSB
        memcpy(packet + 3, _txBuffer, _txBufferIndex); // Copy data from TX buffer

        size_t packetSize = _txBufferIndex + 3; // Total size including header

        // Send the data via Meshtastic
        // NOTE: This uses sendData which might be simpler but less flexible than sendPacket.
        // Adjust if you need more control (ACKs, specific destination, channel, etc.)
        // bool success = _mesh->sendData(packet, packetSize); // Simple version

        // Example using sendPacket for more control (adjust as needed):
        MeshPacket genericPacket;
        genericPacket.set_payload(packet, packetSize);
        genericPacket.set_to(BROADCAST_ADDR); // Send to everyone on the mesh for simplicity
        // genericPacket.set_to(destinationNodeId); // Or send to a specific node if known
        genericPacket.set_portnum(PortNum_APP_MAX); // Use a high port number (less likely to conflict)
                                                    // Consider defining a dedicated PortNum for ZModem?
        genericPacket.set_want_ack(false);          // ZModem protocol handles its own reliability
        genericPacket.set_hop_limit(3);             // Default Meshtastic hop limit
        // genericPacket.set_channel(0);            // Specify channel index if not using primary

        bool success = _mesh->sendPacket(&genericPacket);


        if (success) {
            // String logMsg = "Sent Packet ID: "; logMsg += _sentPacketId; logMsg += " Size: "; logMsg += packetSize;
            // _streamLog(logMsg);
            _sentPacketId++;          // Increment ID for the next packet
            _txBufferIndex = 0;       // Reset buffer index, ready for new data
        } else {
            _streamLog("Error: Failed to send ZModem packet via Meshtastic.");
            // Don't increment packet ID, so the same data will be retried on the next write/flush
        }
        return success;
    }


public:
    /**
     * @brief Constructor for MeshtasticZModemStream.
     *
     * @param mesh Pointer to the Meshtastic instance.
     * @param debug Pointer to the debug stream (can be nullptr).
     * @param maxPacket Max payload size for Meshtastic packets.
     * @param identifier Byte used to identify ZModem packets.
     */
    MeshtasticZModemStream(Meshtastic* mesh, Stream* debug, size_t maxPacket, uint8_t identifier)
        : _mesh(mesh), _debug(debug), _maxPacketSize(maxPacket), _packetIdentifier(identifier) {}

    /**
     * @brief Checks for available bytes to read from the stream.
     * Reads from the internal buffer first, then attempts to receive and process
     * a new packet from the Meshtastic mesh if the buffer is empty.
     * @return int Number of bytes available to read.
     */
    virtual int available() override {
        // 1. Check if data is already in the receive buffer
        if (_rxBufferIndex < _rxBufferSize) {
            return _rxBufferSize - _rxBufferIndex;
        }

        // 2. If buffer empty, check for new Meshtastic packets
        if (!_mesh || !_mesh->available()) {
            return 0; // No mesh connection or no packets waiting
        }

        // 3. Process the next available Meshtastic packet
        ReceivedPacket packet = _mesh->receive();

        // Basic validation
        if (!packet.isValid || packet.decoded.payload.length() < 3) { // Need at least identifier + 2 ID bytes
             _mesh->releaseReceiveBuffer(); // IMPORTANT: Always release buffer
             return 0; // Invalid or too short
        }

        const uint8_t* payload = packet.decoded.payload.getBuffer();
        size_t payloadLen = packet.decoded.payload.length();

        // 4. Check if it's a ZModem packet for us
        if (payload[0] == _packetIdentifier) {
            uint16_t receivedPacketId = (payload[1] << 8) | payload[2];

            // 5. Check if the Packet ID is the one we expect
            if (receivedPacketId == _expectedPacketId) {
                // Correct packet ID received
                _rxBufferSize = payloadLen - 3; // Size of actual ZModem data

                // Prevent buffer overflow
                if (_rxBufferSize > sizeof(_rxBuffer)) {
                    _streamLog("Error: Received packet data larger than RX buffer!");
                    _rxBufferSize = 0; // Discard data
                } else {
                    // Copy data into our buffer
                    memcpy(_rxBuffer, payload + 3, _rxBufferSize);
                    _rxBufferIndex = 0; // Reset read index
                    _expectedPacketId++; // Increment expected ID for the next packet
                    _mesh->releaseReceiveBuffer(); // Release Meshtastic buffer
                    return _rxBufferSize; // Report available bytes
                }
            } else if (receivedPacketId < _expectedPacketId) {
                 // Received an old packet (likely a duplicate due to mesh retransmit) - ignore it
                 // String logMsg = "Received duplicate/old Packet ID: "; logMsg += receivedPacketId; logMsg += " (Expected: "; logMsg += _expectedPacketId; logMsg += ")";
                 // _streamLog(logMsg);
                 // Do not increment expected ID
            } else {
                // Packet ID mismatch (gap detected) - potential packet loss
                String logMsg = "Packet ID mismatch (potential loss). Expected: ";
                logMsg += _expectedPacketId;
                logMsg += ", Got: ";
                logMsg += receivedPacketId;
                _streamLog(logMsg);
                // Do not increment expected ID. ZModem protocol should handle the retry/error.
                // Discard this packet's data for now.
                _rxBufferSize = 0;
                _rxBufferIndex = 0;
            }
        } else {
            // Not a ZModem packet (or uses a different identifier) - ignore it
            // _streamLog("Received non-ZModem packet.");
        }

        // 6. Release Meshtastic buffer if not already done
        _mesh->releaseReceiveBuffer();
        return 0; // No usable ZModem data found in this packet
    }

    /**
     * @brief Reads a single byte from the stream.
     * @return int The byte read, or -1 if no data is available.
     */
    virtual int read() override {
         if (available() > 0) { // available() handles receiving new data if needed
            return _rxBuffer[_rxBufferIndex++];
        }
        return -1;
    }

    /**
     * @brief Returns the next byte without consuming it.
     * @return int The next byte, or -1 if none is available.
     */
    virtual int peek() override {
         if (available() > 0) {
            return _rxBuffer[_rxBufferIndex];
        }
        return -1;
    }

    /**
     * @brief Writes a single byte to the stream.
     * Adds the byte to the transmit buffer. If the buffer becomes full
     * enough (considering header space), it calls flush() to send a packet.
     * @param val The byte to write.
     * @return size_t 1 if successful, 0 on failure (e.g., buffer full after flush).
     */
    virtual size_t write(uint8_t val) override {
        if (!_mesh) return 0; // No mesh connection

        // Check if buffer has space (leave 3 bytes for header)
        if (_txBufferIndex >= sizeof(_txBuffer)) {
             _streamLog("Error: TX buffer full before write.");
             // Try flushing existing data first
             if (!flush()) {
                 _streamLog("Error: Flush failed, cannot write byte.");
                 return 0; // Cannot write if flush fails and buffer is full
             }
             // Check again after flush
             if (_txBufferIndex >= sizeof(_txBuffer)) {
                  _streamLog("Error: TX buffer still full after flush!");
                  return 0;
             }
        }

        // Add byte to buffer
        _txBuffer[_txBufferIndex++] = val;

        // Send packet if buffer is nearly full (leave room for header)
        // Send when buffer reaches MaxPacketSize - HeaderSize
        if (_txBufferIndex >= _maxPacketSize - 3) {
            if (!flush()) {
                // Write succeeded in adding to buffer, but flush failed.
                // Return 1 because the byte *was* buffered, but signal potential issue.
                 _streamLog("Warning: Byte buffered, but immediate flush failed.");
            }
        }
        return 1; // Byte successfully added to buffer
    }

    /**
     * @brief Writes multiple bytes to the stream.
     * @param buffer Pointer to the buffer containing data to write.
     * @param size Number of bytes to write.
     * @return size_t The number of bytes successfully written (buffered).
     */
     virtual size_t write(const uint8_t *buffer, size_t size) override {
        size_t written = 0;
        for (size_t i = 0; i < size; ++i) {
            if (write(buffer[i]) == 1) {
                written++;
            } else {
                // Write failed, likely due to buffer issues or mesh send error
                _streamLog("Error: Failed writing byte in bulk write.");
                break; // Stop writing if one byte fails
            }
        }
        return written;
    }

    /**
     * @brief Sends any data currently waiting in the transmit buffer as a packet.
     * @return bool true if the flush operation succeeded or there was nothing to flush,
     * false if sending the packet failed.
     */
    virtual bool flush() override {
        return sendPacket();
    }

    /**
     * @brief Resets the stream state for a new transfer.
     * Clears buffers and resets packet IDs.
     */
    void reset() {
        _rxBufferIndex = 0;
        _rxBufferSize = 0;
        _txBufferIndex = 0;
        _expectedPacketId = 0;
        _sentPacketId = 0;
        // Optionally clear buffer contents?
        // memset(_rxBuffer, 0, sizeof(_rxBuffer));
        // memset(_txBuffer, 0, sizeof(_txBuffer));
        _streamLog("Stream reset.");
    }
};
// ==========================================================================
// ==                   End MeshtasticZModemStream Class                   ==
// ==========================================================================


// ==========================================================================
// ==                      AkitaMeshZmodem Class Impl                      ==
// ==========================================================================

AkitaMeshZmodem::AkitaMeshZmodem() {
    // Initialize pointers to null in constructor
    _mesh = nullptr;
    _fs = nullptr;
    _debug = nullptr;
    _meshStream = nullptr;
}

AkitaMeshZmodem::~AkitaMeshZmodem() {
    // Destructor: Clean up dynamically allocated memory and close files
    _log("AkitaMeshZmodem shutting down...");
    if (_transferFile) {
        _transferFile.close();
    }
    delete _meshStream; // Delete the custom stream object
    _meshStream = nullptr; // Avoid dangling pointer
}

void AkitaMeshZmodem::begin(Meshtastic& meshInstance, FS& filesystem, Stream* debugStream) {
    _mesh = &meshInstance;
    _fs = &filesystem;
    _debug = debugStream;

    _log("Initializing AkitaMeshZmodem...");

    // Check if filesystem is available
    // Note: SPIFFS.begin() is usually called in setup(), but we check the pointer validity.
    if (!_fs) {
        _logError("Filesystem reference is invalid!");
        // Handle error appropriately, maybe prevent further operation?
        return;
    }

    // Create the custom stream implementation
    delete _meshStream; // Delete previous instance if begin() is called again
    _meshStream = new MeshtasticZModemStream(_mesh, _debug, _maxPacketSize, AKZ_PACKET_IDENTIFIER);
    if (!_meshStream) {
         _logError("Failed to allocate MeshtasticZModemStream!");
         return;
    }
     _log("MeshtasticZModemStream created.");

    // Initialize ZModem library
    // It needs the communication stream (_meshStream) for protocol messages.
    // The stream for actual file data (_transferFile) is set via setTransferStream().
    _zmodem.begin(*_meshStream); // Pass the communication stream
    _log("ZModem library initialized.");

    _resetTransferState(); // Ensure starting state is clean
    _log("AkitaMeshZmodem initialization complete.");
}

void AkitaMeshZmodem::_resetTransferState() {
     if (_transferFile) {
        _transferFile.close(); // Ensure file is closed
        // _log("Closed transfer file.");
    }
    _filename = "";
    _currentState = TransferState::IDLE;
    _totalFileSize = 0;
    _bytesTransferred = 0;
    _retryCount = 0;
    _lastProgressUpdate = 0;
    _transferStartTime = 0;

    if (_meshStream) {
        _meshStream->reset(); // Reset packet IDs and buffers in the stream wrapper
    }
    _zmodem.abort(); // Ensure ZModem library state is reset internally
    // _log("Transfer state reset to IDLE.");
}

void AkitaMeshZmodem::abortTransfer() {
    _logError("Transfer aborted by user.");
    _resetTransferState();
    // Optionally send a ZModem abort sequence if the library supports it?
    // _zmodem.abort(); // Already called in _resetTransferState
}


bool AkitaMeshZmodem::startSend(const String& filePath) {
    if (_currentState != TransferState::IDLE) {
        _logError("Cannot start send: Transfer already in progress (State: " + String((int)_currentState) + ")");
        return false;
    }
     _resetTransferState(); // Ensure clean state before starting
    _filename = filePath;

    _log("Attempting to start SEND for: " + _filename);

    if (!_fs) {
        _logError("Filesystem not available.");
        return false;
    }

    _transferFile = _fs->open(_filename, FILE_READ);
    if (!_transferFile) {
        _logError("Failed to open file for reading: " + _filename);
        _resetTransferState(); // Go back to idle
        return false;
    }

    if (_transferFile.isDirectory()) {
         _logError("Cannot send a directory: " + _filename);
         _transferFile.close();
         _resetTransferState();
         return false;
    }

    _totalFileSize = _transferFile.size();
    _log("File opened. Size: " + String(_totalFileSize) + " bytes.");

    // Tell ZModem library where the file data comes from
    _zmodem.setTransferStream(_transferFile);

    // Initiate the ZModem send process
    if (_zmodem.send(_filename, _totalFileSize, _zmodemTimeout)) { // Pass filename and size
         _log("ZModem send initiated successfully.");
         _currentState = TransferState::SENDING;
         _bytesTransferred = 0; // Reset counters
         _retryCount = 0;
         _lastProgressUpdate = millis(); // Start progress timing
         _transferStartTime = _lastProgressUpdate;
         return true;
    } else {
         _logError("ZModem library failed to start send operation.");
         _resetTransferState(); // Clean up
         return false;
    }
}

 bool AkitaMeshZmodem::startReceive(const String& filePath) {
     if (_currentState != TransferState::IDLE) {
        _logError("Cannot start receive: Transfer already in progress (State: " + String((int)_currentState) + ")");
        return false;
    }
    _resetTransferState();
    _filename = filePath; // Store the intended save path

    _log("Attempting to start RECEIVE to: " + _filename);

     if (!_fs) {
        _logError("Filesystem not available.");
        return false;
    }

    // ZModem typically receives the filename from the sender.
    // We open the file for writing using the provided path *after* ZModem confirms reception.
    // For now, just prepare the state.

    // Tell ZModem library where to write the incoming file data.
    // We need to open the file *inside* the ZModem callback or just before starting receive,
    // potentially using the filename provided by the sender.
    // Let's try opening the file here for simplicity, assuming the provided path is desired.

    _transferFile = _fs->open(_filename, FILE_WRITE);
    if (!_transferFile) {
       _logError("Failed to open file for writing: " + _filename);
        _resetTransferState();
       return false;
    }
     _log("Output file opened for writing.");
     _zmodem.setTransferStream(_transferFile); // Tell ZModem where to write data


    // Initiate the ZModem receive process
    if (_zmodem.receive(_zmodemTimeout)) {
       _log("ZModem receive initiated. Waiting for sender...");
       _currentState = TransferState::RECEIVING;
       _totalFileSize = 0; // Will be updated when ZModem receives the header
       _bytesTransferred = 0;
       _retryCount = 0;
        _lastProgressUpdate = millis();
        _transferStartTime = _lastProgressUpdate;
       return true;
    } else {
       _logError("ZModem library failed to start receive operation.");
       _resetTransferState(); // Clean up (closes file)
       return false;
    }
 }

AkitaMeshZmodem::TransferState AkitaMeshZmodem::loop() {
    // If idle or already finished, nothing to do
    if (_currentState == TransferState::IDLE || _currentState == TransferState::COMPLETE || _currentState == TransferState::ERROR) {
        return _currentState;
    }

    // Ensure mesh stream is valid
    if (!_meshStream) {
        _logError("Mesh stream is null in loop!");
        _currentState = TransferState::ERROR;
        _resetTransferState();
        return _currentState;
    }

    // Let the ZModem library process incoming/outgoing data via the mesh stream
    // loop() handles timeouts, retries, and state transitions internally.
    ZModem::TransferState zState = _zmodem.loop();

    // Update our internal state based on ZModem's state
    _handleZmodemState(zState);

    // Update transferred bytes based on file position (more reliable than ZModem internal count sometimes)
    if (_transferFile && (_currentState == TransferState::SENDING || _currentState == TransferState::RECEIVING)) {
        _bytesTransferred = _transferFile.position();
    }

    // Display progress periodically if enabled
    _updateProgress();

    // Check for overall timeout? (Optional, ZModem lib handles its own timeouts)

    return _currentState;
}

void AkitaMeshZmodem::_handleZmodemState(ZModem::TransferState zState) {
     switch (zState) {
        case ZModem::TransferState::COMPLETE:
            if (_currentState == TransferState::SENDING || _currentState == TransferState::RECEIVING) {
                unsigned long duration = millis() - _transferStartTime;
                _log("-------------------------------");
                _log(">>> ZModem Transfer COMPLETE!");
                _log("Filename: " + getFilename()); // Use getter which might get name from ZModem
                _log("Size: " + String(getBytesTransferred()) + " bytes"); // Use getter
                _log("Duration: " + String(duration / 1000.0, 2) + " s");
                 if (duration > 0) {
                     float rate = (float)getBytesTransferred() / (duration / 1000.0);
                     _log("Rate: " + String(rate, 2) + " bytes/s");
                 }
                _log("-------------------------------");

                 if (_transferFile) _transferFile.close(); // Ensure file is saved and closed
                _currentState = TransferState::COMPLETE; // Set final state
                // Don't reset counters here, user might want to check getBytesTransferred() etc.
                // Reset happens on next startSend/startReceive or abort.
            }
            break;

        case ZModem::TransferState::TRANSFERRING:
            // This state is normal during active transfer.
            _retryCount = 0; // Reset retry count on successful progress
            // Update filename and size if receiving and header just arrived
            if (_currentState == TransferState::RECEIVING) {
                if (_totalFileSize == 0) {
                    _totalFileSize = _zmodem.getFileSize(); // Try to get size from ZModem
                }
                // Optionally update _filename if ZModem provides it and differs from initial path
                // String receivedFilename = _zmodem.getFilename();
                // if (receivedFilename.length() > 0 && _filename != receivedFilename) {
                //     _log("Note: Received filename '" + receivedFilename + "' differs from requested save path '" + _filename + "'. Saving to requested path.");
                //     // Decide if you want to rename the open file handle or stick to the original path.
                // }
            }
            break;

        case ZModem::TransferState::ERROR:
             if (_currentState == TransferState::SENDING || _currentState == TransferState::RECEIVING) {
                 _logError("ZModem transfer error occurred!");
                 // ZModem library might handle retries internally based on its config.
                 // If it returns ERROR, it usually means retries failed or a fatal error occurred.
                 _logError("Aborting transfer due to ZModem error.");
                 _currentState = TransferState::ERROR; // Set final error state
                 _resetTransferState(); // Clean up
             }
            break;

        case ZModem::TransferState::IDLE:
            // This might happen if aborted or after completion/error.
            // If we are in SENDING/RECEIVING state, this indicates an unexpected stop.
             if (_currentState == TransferState::SENDING || _currentState == TransferState::RECEIVING) {
                  _logError("ZModem returned to IDLE state unexpectedly during transfer!");
                  _currentState = TransferState::ERROR;
                  _resetTransferState();
             }
            break;
    }
}


void AkitaMeshZmodem::_updateProgress() {
     // Check if progress updates are enabled and we are in a transfer state
     if (_progressUpdateInterval == 0 || (_currentState != TransferState::SENDING && _currentState != TransferState::RECEIVING)) {
         return;
     }

    unsigned long now = millis();
    if (now - _lastProgressUpdate >= _progressUpdateInterval) {
        String progressMsg;
        if (_totalFileSize > 0) {
            // Calculate percentage if total size is known
            float progress = (_bytesTransferred > _totalFileSize) ? 100.0f : ((float)_bytesTransferred / _totalFileSize * 100.0f);
            progressMsg = "Progress: ";
            progressMsg += String(progress, 1); // 1 decimal place
            progressMsg += "% (";
            progressMsg += _bytesTransferred;
            progressMsg += "/";
            progressMsg += _totalFileSize;
            progressMsg += " bytes)";
        } else {
            // Total size unknown (likely during receive before header)
             progressMsg = "Transferred: ";
             progressMsg += _bytesTransferred;
             progressMsg += " bytes";
        }
        _log(progressMsg); // Log the progress message
        _lastProgressUpdate = now; // Update timestamp
    }
}

// --- Status Getters ---
AkitaMeshZmodem::TransferState AkitaMeshZmodem::getCurrentState() const { return _currentState; }
size_t AkitaMeshZmodem::getBytesTransferred() const { return _bytesTransferred; }
size_t AkitaMeshZmodem::getTotalFileSize() const { return _totalFileSize; }
String AkitaMeshZmodem::getFilename() const {
    // Return the filename known to this class.
    // Could potentially query _zmodem.getFilename() if needed, especially for receive.
    return _filename;
}


// --- Configuration Setters ---
void AkitaMeshZmodem::setMaxRetries(uint16_t retries) {
    _maxRetryCount = retries;
    // Note: The underlying ZModem library might have its own retry setting.
    // This class's retry logic might be redundant if the lib handles it well.
    _log("Max retries set to: " + String(_maxRetryCount));
}

void AkitaMeshZmodem::setTimeout(unsigned long timeoutMs) {
    _zmodemTimeout = timeoutMs;
    // Pass timeout to ZModem library when starting send/receive
    _log("ZModem timeout set to: " + String(_zmodemTimeout) + " ms");
}

void AkitaMeshZmodem::setProgressUpdateInterval(unsigned long intervalMs) {
    _progressUpdateInterval = intervalMs;
     _log("Progress update interval set to: " + String(_progressUpdateInterval) + " ms");
}

void AkitaMeshZmodem::setMaxPacketSize(size_t maxSize) {
    // Update the max size and potentially update the meshStream if it's already created
    if (maxSize < 10) { // Sanity check minimum size
        _logError("Max packet size too small: " + String(maxSize));
        return;
    }
    _maxPacketSize = maxSize;
     _log("Max packet size set to: " + String(_maxPacketSize));
     // If begin() was already called, we might need to recreate or update _meshStream
     // This is complex, simpler to require setting before begin() or calling begin() again.
     if (_meshStream) {
         _log("Warning: Max packet size changed after begin(). Re-run begin() or restart for change to fully take effect in stream handler.");
         // Or attempt to update the existing stream object if it has a setter method.
         // _meshStream->updateMaxPacketSize(_maxPacketSize); // Example if stream class supports it
     }
}


// --- Logging Helpers ---
 void AkitaMeshZmodem::_log(const char* message) {
     if (_debug) {
         _debug->print("[AkitaZModem] "); // Add a prefix
         _debug->println(message);
     }
 }
 void AkitaMeshZmodem::_log(const String& message) {
     if (_debug) {
         _debug->print("[AkitaZModem] ");
         _debug->println(message);
     }
 }

 void AkitaMeshZmodem::_logError(const char* message) {
      if (_debug) {
         _debug->print("[AkitaZModem ERROR] "); // Add ERROR prefix
         _debug->println(message);
     }
 }
 void AkitaMeshZmodem::_logError(const String& message) {
      if (_debug) {
         _debug->print("[AkitaZModem ERROR] ");
         _debug->println(message);
     }
 }

// ==========================================================================
// ==                    End AkitaMeshZmodem Class Impl                    ==
// ==========================================================================
