/**
 * @file AkitaMeshZmodem.h
 * @author Akita Engineering
 * @brief Main header file for the Akita Meshtastic Zmodem Arduino Library.
 * Provides ZModem file transfer capabilities over Meshtastic networks.
 * @version 1.0.0
 * @date 2025-04-26 // Adjust date as needed
 *
 * @copyright Copyright (c) 2025 Akita Engineering
 *
 */

#ifndef AKITA_MESH_ZMODEM_H
#define AKITA_MESH_ZMODEM_H

#include <Arduino.h>
#include <Meshtastic.h> // Use the actual Meshtastic library header(s) as needed
#include <ZModem.h>
#include <StreamUtils.h>
#include <FS.h> // Use FS for filesystem abstraction (SPIFFS, SD, etc.)

// Optional: Include default configuration values
#include "AkitaMeshZmodemConfig.h"

// Forward declaration for the internal stream class
class MeshtasticZModemStream;

/**
 * @brief Main class for handling ZModem transfers over Meshtastic.
 */
class AkitaMeshZmodem {
public:
    /**
     * @brief Represents the current state of the ZModem transfer.
     */
    enum class TransferState {
        IDLE,       ///< No transfer active.
        RECEIVING,  ///< Actively waiting for or receiving a file.
        SENDING,    ///< Actively sending a file.
        COMPLETE,   ///< The last transfer completed successfully.
        ERROR       ///< The last transfer failed or was aborted due to errors.
    };

    /**
     * @brief Constructor.
     */
    AkitaMeshZmodem();

    /**
     * @brief Destructor. Cleans up allocated resources.
     */
    ~AkitaMeshZmodem();

    /**
     * @brief Initializes the library. Must be called before other methods.
     *
     * @param meshInstance A reference to the initialized Meshtastic instance.
     * @param filesystem A reference to the filesystem to use (e.g., SPIFFS). Defaults to SPIFFS.
     * @param debugStream An optional Stream pointer for debug output (e.g., &Serial). Defaults to nullptr (no debug output).
     */
    void begin(Meshtastic& meshInstance, FS& filesystem = SPIFFS, Stream* debugStream = nullptr);

    /**
     * @brief Main processing loop. Must be called repeatedly in the Arduino loop() function.
     * Handles ZModem protocol state, packet processing, and timeouts.
     *
     * @return TransferState The current state of the transfer process.
     */
    TransferState loop();

    /**
     * @brief Initiates sending a file.
     *
     * @param filePath The full path to the file on the specified filesystem.
     * @return true If the send operation was successfully initiated.
     * @return false If initiation failed (e.g., file not found, transfer already active).
     */
    bool startSend(const String& filePath);

    /**
     * @brief Initiates receiving a file.
     *
     * @param filePath The full path where the received file should be saved on the specified filesystem.
     * @return true If the receive operation was successfully initiated.
     * @return false If initiation failed (e.g., cannot create file, transfer already active).
     */
    bool startReceive(const String& filePath);

    /**
     * @brief Aborts the current transfer immediately.
     */
    void abortTransfer();

    /**
     * @brief Gets the current state of the transfer.
     * @return TransferState The current state.
     */
    TransferState getCurrentState() const;

    /**
     * @brief Gets the number of bytes transferred in the current or last session.
     * @return size_t Number of bytes transferred.
     */
    size_t getBytesTransferred() const;

    /**
     * @brief Gets the total size of the file being transferred.
     * Note: For receiving, this is only known after the ZModem header is processed.
     * @return size_t Total file size in bytes, or 0 if unknown.
     */
    size_t getTotalFileSize() const;

    /**
     * @brief Gets the filename of the file being transferred.
     * @return String The filename.
     */
    String getFilename() const;

    // --- Configuration Setters ---

    /**
     * @brief Sets the maximum number of retries on ZModem errors.
     * @param retries Maximum retry count.
     */
    void setMaxRetries(uint16_t retries);

    /**
     * @brief Sets the timeout for ZModem operations.
     * @param timeoutMs Timeout duration in milliseconds.
     */
    void setTimeout(unsigned long timeoutMs);

    /**
     * @brief Sets the interval for displaying progress updates via the debug stream.
     * @param intervalMs Update interval in milliseconds. Set to 0 to disable periodic updates.
     */
    void setProgressUpdateInterval(unsigned long intervalMs);

    /**
     * @brief Sets the maximum packet payload size for Meshtastic packets.
     * This should generally match the Meshtastic network/radio limits.
     * @param maxSize Maximum payload size in bytes.
     */
    void setMaxPacketSize(size_t maxSize);


private:
    // --- Private Members ---
    Meshtastic* _mesh = nullptr;            ///< Pointer to the Meshtastic instance.
    FS* _fs = nullptr;                      ///< Pointer to the filesystem instance.
    Stream* _debug = nullptr;               ///< Pointer to the debug output stream.
    MeshtasticZModemStream* _meshStream = nullptr; ///< Pointer to the custom Stream wrapper for Meshtastic.
    ZModem _zmodem;                         ///< The underlying ZModem library instance.

    File _transferFile;                     ///< File handle for the current transfer.
    String _filename = "";                  ///< Filename for the current transfer.
    TransferState _currentState = TransferState::IDLE; ///< Current operational state.

    size_t _totalFileSize = 0;              ///< Total size of the file being transferred.
    size_t _bytesTransferred = 0;           ///< Bytes transferred so far.
    uint16_t _retryCount = 0;               ///< Current retry attempt count.

    // --- Configuration ---
    uint16_t _maxRetryCount = AKZ_DEFAULT_MAX_RETRY_COUNT; ///< Max ZModem retries on error.
    unsigned long _zmodemTimeout = AKZ_DEFAULT_ZMODEM_TIMEOUT; ///< ZModem operation timeout (ms).
    unsigned long _progressUpdateInterval = AKZ_DEFAULT_PROGRESS_UPDATE_INTERVAL; ///< Progress update interval (ms).
    size_t _maxPacketSize = AKZ_DEFAULT_MAX_PACKET_SIZE; ///< Max Meshtastic payload size.

    unsigned long _lastProgressUpdate = 0;  ///< Timestamp of the last progress update.
    unsigned long _transferStartTime = 0;   ///< Timestamp when the transfer started.


    // --- Private Helper Methods ---

    /**
     * @brief Resets the transfer state machine and associated variables to IDLE.
     * Closes any open file handles.
     */
    void _resetTransferState();

    /**
     * @brief Prints progress information to the debug stream if enabled and interval has passed.
     */
    void _updateProgress();

    /**
     * @brief Processes the state returned by the ZModem library's loop() method.
     * Handles transitions between states (COMPLETE, ERROR, TRANSFERRING).
     * @param zState The state returned by _zmodem.loop().
     */
    void _handleZmodemState(ZModem::TransferState zState);

    /**
     * @brief Logs a message to the debug stream, if configured.
     * @param message The message string to log.
     */
    void _log(const char* message);
    void _log(const String& message);

    /**
     * @brief Logs an error message to the debug stream, if configured.
     * @param message The error message string to log.
     */
    void _logError(const char* message);
    void _logError(const String& message);
};

#endif // AKITA_MESH_ZMODEM_H
