/**
 * @file ZModemEngine.h
 * @author Akita Engineering
 * @brief Internal, non-blocking ZModem Protocol Engine.
 * Replaces external dependencies to ensure stability and availability.
 * Implements Core ZModem logic for Node-to-Node binary transfer.
 * @version 1.1.0
 */

#ifndef ZMODEM_ENGINE_H
#define ZMODEM_ENGINE_H

#include <Arduino.h>
#include <Stream.h>
#include <FS.h>

// ZModem Control Characters
#define ZPAD  0x2A // '*'
#define ZDLE  0x18 // CAN
#define ZDLEE 0x58 // 'X' (Escaped ZDLE)
#define ZBIN  0x41 // 'A'
#define ZHEX  0x42 // 'B'
#define ZBIN32 0x43 // 'C'

// Frame Types
#define ZRQINIT 0
#define ZRINIT  1
#define ZSINIT  2
#define ZACK    3
#define ZFILE   4
#define ZSKIP   5
#define ZNAK    6
#define ZABORT  7
#define ZFIN    8
#define ZRPOS   9
#define ZDATA   10
#define ZEOF    11
#define ZFERR   12
#define ZCRC    13
#define ZCHALLENGE 14
#define ZCOMPL  15
#define ZCAN    16
#define ZFREECNT 17
#define ZCOMMAND 18

class ZModemEngine {
public:
    enum State {
        STATE_IDLE,
        STATE_SEND_ZRQINIT,
        STATE_AWAIT_ZRINIT,
        STATE_SEND_ZFILE,
        STATE_AWAIT_ZRPOS,
        STATE_SEND_ZDATA,
        STATE_SEND_ZEOF,
        STATE_AWAIT_ZFIN,
        STATE_SEND_ZFIN,
        STATE_COMPLETE,
        STATE_ERROR
    };

    enum ReceiveState {
        RSTATE_IDLE,
        RSTATE_AWAIT_HEADER,
        RSTATE_READ_ZFILE,
        RSTATE_AWAIT_ZDATA,
        RSTATE_READ_ZDATA,
        RSTATE_COMPLETE,
        RSTATE_ERROR
    };

    ZModemEngine();
    
    // Setup the IO channels
    void begin(Stream& ioStream);
    
    // Set the file storage stream
    void setFileStream(File* file, const String& filename, size_t fileSize);
    
    // Start operations
    bool send(unsigned long timeout);
    bool receive(unsigned long timeout);
    void abort();

    // Main Loop
    int loop(); // Returns 0 for busy, 1 for complete, -1 for error

    // Getters
    size_t getBytesTransferred() const { return _bytesTransferred; }
    size_t getFileSize() const { return _fileSize; }
    String getFilename() const { return _filename; }
    State getState() const { return _state; }

private:
    Stream* _io;
    File* _file;
    
    // Transfer context
    String _filename;
    size_t _fileSize;
    size_t _bytesTransferred;
    unsigned long _operationStartTime;
    unsigned long _timeoutMs;
    unsigned long _lastActivity;
    
    // State Machines
    State _state;
    ReceiveState _rState;
    bool _isSender;
    
    // Buffers
    uint8_t _txBuffer[256]; // Small chunk size for LoRa
    uint16_t _bufferIdx;
    
    // CRC Helpers
    uint16_t _calcCRC16(const uint8_t* data, size_t len);
    uint16_t _updcrc(uint8_t c, uint16_t crc);
    
    // Low Level ZModem OPS
    void _sendHexHeader(uint8_t type, const uint8_t* flags);
    void _sendBinaryHeader(uint8_t type, const uint8_t* flags);
    void _sendDataSubpacket(const uint8_t* data, size_t len, bool endFrame);
    
    // Input Handling
    void _processInput();
    int _readHexHeader(uint8_t& type, uint8_t* flags);
    
    // Helper State handlers
    void _handleSenderLoop();
    void _handleReceiverLoop();
    
    // Internal tracking
    int _retryCount;
    static const int MAX_RETRIES = 5;
};

#endif // ZMODEM_ENGINE_H
