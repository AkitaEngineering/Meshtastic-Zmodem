/**
 * @file AkitaMeshZmodem.cpp
 * @author Akita Engineering
 * @brief Implementation file for the Akita Meshtastic Zmodem Arduino Library.
 * @version 1.1.0
 * @date 2025-11-17 // Updated date
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
//
// **DESIGN:**
// - SENDING (write): Data from ZModem is buffered here and sent as packets
//   to the destination via `_mesh->sendPacket()`.
// - RECEIVING (read): This class does *not* poll for packets. Instead,
//   the main module/sketch receives packets on the DATA port and *pushes*
//   them into this class via `pushPacket()`. `available()` and `read()`
//   just consume the internal buffer filled by `pushPacket`.

class MeshtasticZModemStream : public Stream {
private:
    Meshtastic* _mesh;          // Pointer to the Meshtastic instance (for SENDING only)
    Stream* _debug;             // Optional debug stream
    size_t _maxPacketSize;      // Max payload size per Meshtastic packet
    uint8_t _packetIdentifier;  // Byte to identify our packets
    NodeNum _destinationNodeId = BROADCAST_ADDR; // Destination for sent packets

    // Receive Buffer (filled by pushPacket)
    uint8_t _rxBuffer[AKZ_STREAM_RX_BUFFER_SIZE]; // Buffer for incoming packet data
    uint16_t _rxBufferIndex = 0; // Current read position in _rxBuffer
    uint16_t _rxBufferSize = 0;  // Number of valid bytes currently in _rxBuffer
    uint16_t _expectedPacketId = 0; // ID expected for the next incoming packet

    // Transmit Buffer (filled by write, sent by sendPacket)
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
        
        if (_destinationNodeId == BROADCAST_ADDR) {
            _streamLog("Error: No destination set, cannot send packet.");
            return false;
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
        MeshPacket genericPacket;
        genericPacket.set_payload(packet, packetSize);
        genericPacket.set_to(_destinationNodeId);           // Send to the specific destination
        genericPacket.set_portnum(AKZ_ZMODEM_DATA_PORTNUM); // Send on the dedicated DATA port
        genericPacket.set_want_ack(false);                  // ZModem protocol handles its own reliability
        genericPacket.set_hop_limit(3);                     // Default Meshtastic hop limit
        // genericPacket.set_channel(0);                    // Specify channel index if not using primary

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
     * @param mesh Pointer to the Meshtastic instance (for sending).
     * @param debug Pointer to the debug stream (can be nullptr).
     * @param maxPacket Max payload size for Meshtastic packets.
     * @param identifier Byte used to identify ZModem packets.
     */
    MeshtasticZModemStream(Meshtastic* mesh, Stream* debug, size_t maxPacket, uint8_t identifier)
        : _mesh(mesh), _debug(debug), _maxPacketSize(maxPacket), _packetIdentifier(identifier) {}

    /**
     * @brief Sets the destination NodeNum for outgoing packets.
     */
    void setDestination(NodeNum dest) {
        _destinationNodeId = dest;
    }

    /**
     * @brief Feeds a received packet into the stream's processing logic.
     * This is called by the main library/module, not by ZModem.
     * @param packet The packet received from the mesh.
     */
    void pushPacket(MeshPacket& packet) {
        // This function is called by AkitaMeshZmodem::processDataPacket
        // We assume the portnum is already correct (AKZ_ZMODEM_DATA_PORTNUM)
        // because the caller (ZmodemModule) already filtered it.

        // If data is already waiting in the buffer, don't overwrite it.
        // ZModem will consume it first, then call available() again, which will be 0.
        // ZModem's loop() should then call read() until buffer is empty.
        // This push logic assumes ZModem calls read() until available() is 0
        // before this function gets called again. This might be a bad assumption.
        //
        // Let's rethink: What if pushPacket is called multiple times before ZModem reads?
        // _rxBuffer would be overwritten.
        //
        // **Correction:** Only process a new packet if the buffer is currently empty.
        if (_rxBufferIndex < _rxBufferSize) {
             // _streamLog("RX buffer not empty, skipping new packet push.");
             return; // Don't process new packet until buffer is empty
        }

        // Buffer is empty, process the new packet
        if (packet.decoded.payload.length() < 3) {
             _streamLog("Error: ZModem data packet too short.");
             return; // Too short
        }

        const uint8_t* payload = packet.decoded.payload.getBuffer();
        size_t payloadLen = packet.decoded.payload.length();

        // Check if it's a ZModem packet for us
        if (payload[0] == _packetIdentifier) {
            uint16_t receivedPacketId = (payload[1] << 8) | payload[2];

            // Check if the Packet ID is the one we expect
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
                    // _streamLog("Pushed " + String(_rxBufferSize) + " bytes to RX buffer.");
                }
            } else if (receivedPacketId < _expectedPacketId) {
                 // Received an old packet (likely a duplicate due to mesh retransmit) - ignore it
                 String logMsg = "Received duplicate/old Packet ID: "; logMsg += receivedPacketId; logMsg += " (Expected: "; logMsg += _expectedPacketId; logMsg += ")";
                 _streamLog(logMsg);
                 // Do not increment expected ID, do not fill buffer
                 _rxBufferSize = 0;
                 _rxBufferIndex = 0;
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
             _streamLog("Received packet on data port, but wrong identifier byte.");
             _rxBufferSize = 0;
             _rxBufferIndex = 0;
        }
    }


    /**
     * @brief Checks for available bytes to read from the stream.
     * Reads *only* from the internal buffer, which is filled by `pushPacket()`.
     * @return int Number of bytes available to read.
     */
    virtual int available() override {
        // 1. Check if data is already in the receive buffer
        return _rxBufferSize - _rxBufferIndex;
    }

    /**
     * @brief Reads a single byte from the stream.
     * @return int The byte read, or -1 if no data is available.
     */
    virtual int read() override {
         if (available() > 0) { // available() only checks buffer
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
        _destinationNodeId = BROADCAST_ADDR; // Reset destination
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
    if (!_fs) {
        _logError("Filesystem reference is invalid!");
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
    _zmodem.begin(*_meshStream); // Pass the communication stream
    _log("ZModem library initialized.");

    _resetTransferState(); // Ensure starting state is clean
    _log("AkitaMeshZmodem initialization complete.");
}

void AkitaMeshZmodem::_resetTransferState() {
     if (_transferFile) {
        _transferFile.close(); // Ensure file is closed
    }
    _filename = "";
    _currentState = TransferState::IDLE;
    _totalFileSize = 0;
    _bytesTransferred = 0;
    _lastProgressUpdate = 0;
    _transferStartTime = 0;
    _destinationNodeId = BROADCAST_ADDR;

    if (_meshStream) {
        _meshStream->reset(); // Reset packet IDs, buffers, and destination
    }
    _zmodem.abort(); // Ensure ZModem library state is reset internally
    // _log("Transfer state reset to IDLE.");
}

void AkitaMeshZmodem::abortTransfer() {
    _logError("Transfer aborted by user.");
    _resetTransferState();
}

/**
 * @brief Feeds a packet (from data port) into the stream.
 */
void AkitaMeshZmodem::processDataPacket(MeshPacket& packet) {
    if (_meshStream && _currentState == TransferState::RECEIVING) {
        _meshStream->pushPacket(packet);
    }
    // else: Log warning? Silently drop if not receiving or stream not init?
}


bool AkitaMeshZmodem::startSend(const String& filePath, NodeNum destinationNodeId) {
    if (_currentState != TransferState::IDLE) {
        _logError("Cannot start send: Transfer already in progress (State: " + String((int)_currentState) + ")");
        return false;
    }
    if (destinationNodeId == 0 || destinationNodeId == BROADCAST_ADDR) {
        _logError("Cannot start send: Invalid destination Node ID.");
        return false;
    }

     _resetTransferState(); // Ensure clean state before starting
    _filename = filePath;
    _destinationNodeId = destinationNodeId;

    _log("Attempting to start SEND for: " + _filename + " to Node 0x" + String(destinationNodeId, HEX));

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

    // Set destination in the stream
    if(!_meshStream) {
        _logError("Mesh stream not initialized.");
        _resetTransferState();
        return false;
    }
    _meshStream->setDestination(_destinationNodeId);

    // Tell ZModem library where the file data comes from
    _zmodem.setTransferStream(_transferFile);

    // Initiate the ZModem send process
    if (_zmodem.send(_filename, _totalFileSize, _zmodemTimeout)) { // Pass filename and size
         _log("ZModem send initiated successfully.");
         _currentState = TransferState::SENDING;
         _bytesTransferred = 0; // Reset counters
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

    // Let the ZModem library process protocol logic
    // This will call _meshStream->available(), read(), write(), flush()
    ZModem::TransferState zState = _zmodem.loop();

    // Update our internal state based on ZModem's state
    _handleZmodemState(zState);

    // Update transferred bytes based on file position
    if (_transferFile && (_currentState == TransferState::SENDING || _currentState == TransferState::RECEIVING)) {
        _bytesTransferred = _transferFile.position();
    }

    // Display progress periodically if enabled
    _updateProgress();

    return _currentState;
}

void AkitaMeshZmodem::_handleZmodemState(ZModem::TransferState zState) {
     switch (zState) {
        case ZModem::TransferState::COMPLETE:
            if (_currentState == TransferState::SENDING || _currentState == TransferState::RECEIVING) {
                unsigned long duration = millis() - _transferStartTime;
                _log("-------------------------------");
                _log(">>> ZModem Transfer COMPLETE!");
                // Try to get filename from ZModem lib, fallback to our stored one
                String finalName = _zmodem.getFilename();
                if (finalName.length() == 0) finalName = getFilename();
                
                _log("Filename: " + finalName);
                _log("Size: " + String(getBytesTransferred()) + " bytes");
                _log("Duration: " + String(duration / 1000.0, 2) + " s");
                 if (duration > 0) {
                     float rate = (float)getBytesTransferred() / (duration / 1000.0);
                     _log("Rate: " + String(rate, 2) + " bytes/s");
                 }
                _log("-------------------------------");

                 if (_transferFile) _transferFile.close(); // Ensure file is saved and closed
                _currentState = TransferState::COMPLETE; // Set final state
            }
            break;

        case ZModem::TransferState::TRANSFERRING:
            // This state is normal during active transfer.
            // Update filename and size if receiving and header just arrived
            if (_currentState == TransferState::RECEIVING) {
                if (_totalFileSize == 0) {
                    _totalFileSize = _zmodem.getFileSize(); // Try to get size from ZModem
                    if(_totalFileSize > 0) _log("Receiving file, size: " + String(_totalFileSize) + " bytes");
                }
            }
            break;

        case ZModem::TransferState::ERROR:
             if (_currentState == TransferState::SENDING || _currentState == TransferState::RECEIVING) {
                 _logError("ZModem transfer error occurred! (Library reported fatal error)");
                 _logError("Aborting transfer.");
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
        
        // Ensure total size is updated if it just arrived
        if (_currentState == TransferState::RECEIVING && _totalFileSize == 0) {
             _totalFileSize = _zmodem.getFileSize();
        }

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
size_t AkitaMeshZmodem::getTotalFileSize() const { 
    if(_totalFileSize > 0) return _totalFileSize;
    if(_currentState == TransferState::RECEIVING) return _zmodem.getFileSize(); // Check lib again
    return 0;
}
String AkitaMeshZmodem::getFilename() const {
    String libName = _zmodem.getFilename();
    if(libName.length() > 0) return libName;
    return _filename; // Fallback to our stored name
}


// --- Configuration Setters ---
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
     if (_meshStream) {
         _log("Warning: Max packet size changed after begin(). Re-run begin() or restart for change to fully take effect in stream handler.");
         // This is complex, simpler to require setting before begin() or calling begin() again.
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
