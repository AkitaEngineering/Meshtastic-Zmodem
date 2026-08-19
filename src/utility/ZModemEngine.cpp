/**
 * @file ZModemEngine.cpp
 * @author Akita Engineering
 * @brief Internal ZModem Implementation.
 * "Commercial Grade" robustness: Self-contained, CRC16, Non-blocking.
 * @version 1.1.0
 */

#include "ZModemEngine.h"

// ZModem Control Frame End (CRCE is final, CRCG/CRCW are intermediate)
#define ZCRCE 0x45
#define ZCRCG 0x47
#define ZCRCW 0x48

// Helper constants
const uint8_t ZERO_FLAGS[4] = {0, 0, 0, 0};

ZModemEngine::ZModemEngine() {
    _io = nullptr;
    _file = nullptr;
    _state = STATE_IDLE;
    _rState = RSTATE_IDLE;
    _bytesTransferred = 0;
    _fileSize = 0;
    _isSender = false;
    _filename[0] = '\0';
    _inBufLen = 0;
    _retryCount = 0;
    _timeoutMs = 0;
    _operationStartTime = 0;
    _lastActivity = 0;

    // File-info parsing state
    _fileInfoIndex = 0;
    _fileInfoEscape = false;
    _fileInfoAwaitingCRC = false;
    _fileInfoFrameEnd = 0;
    _dataSubpacketLen = 0;
    _dataSubpacketEscape = false;
    _dataSubpacketAwaitingCRC = false;
    _dataSubpacketFrameEnd = 0;
    _dataExpectedPos = 0;
    _loggedState = STATE_IDLE;

    // Sender/receiver timing (instance vars instead of function-local statics)
    _senderLastSend = 0;
    _receiverLastAck = 0;

    // Retransmit state
    _lastDataLen = 0;
    _lastDataPos = 0;
    _lastDataPending = false;
    _lastDataEndFrame = false;
    _lastSendTime = 0;
    _baseRetryIntervalMs = DEFAULT_BASE_RETRY_MS;
    _retryIntervalMs = _baseRetryIntervalMs;
    // XMODEM cache init
    _xmodemLastLen = 0;
    _xmodemLastPos = 0;
    _xmodemLastPending = false;
}

void ZModemEngine::reset()
{
    _file = nullptr;
    _filename[0] = '\0';
    _state = STATE_IDLE;
    _rState = RSTATE_IDLE;
    _bytesTransferred = 0;
    _fileSize = 0;
    _isSender = false;
    _operationStartTime = 0;
    _timeoutMs = 0;
    _lastActivity = 0;
    _inBufLen = 0;

    _fileInfoIndex = 0;
    _fileInfoEscape = false;
    _fileInfoAwaitingCRC = false;
    _fileInfoFrameEnd = 0;
    _dataSubpacketLen = 0;
    _dataSubpacketEscape = false;
    _dataSubpacketAwaitingCRC = false;
    _dataSubpacketFrameEnd = 0;
    _dataExpectedPos = 0;
    _loggedState = STATE_IDLE;

    _senderLastSend = 0;
    _receiverLastAck = 0;

    _lastDataLen = 0;
    _lastDataPos = 0;
    _lastDataPending = false;
    _lastDataEndFrame = false;
    _lastSendTime = 0;
    _baseRetryIntervalMs = DEFAULT_BASE_RETRY_MS;
    _retryIntervalMs = _baseRetryIntervalMs;

    _xmodemStarted = false;
    _xmodemUseCRC = true;
    _xmodemExpectedBlock = 1;
    _xmodemRetryCount = 0;
    _xmodemLastSend = 0;
    _xmodemRetryInterval = 0;
    _xmodemSendStarted = false;
    _xmodemSendBlock = 1;
    _xmodemSendRetry = 0;
    _xmodemSendLast = 0;
    _xmodemSendInterval = 0;
    _xmodemLastLen = 0;
    _xmodemLastPos = 0;
    _xmodemLastPending = false;
    _retryCount = 0;
}

void ZModemEngine::begin(Stream& ioStream) {
    _io = &ioStream;
    _inBufLen = 0;
    if (_debug) {
        _debug->print("ZModemEngine: begin\n");
    }
}

void ZModemEngine::setFileStream(File* file, const String& filename, size_t fileSize) {
    setFileStream(file, filename.c_str(), fileSize);
}

void ZModemEngine::setFileStream(File* file, const char* filename, size_t fileSize) {
    _file = file;
    if (filename && filename[0]) {
        strncpy(_filename, filename, FILENAME_MAX_LEN - 1);
        _filename[FILENAME_MAX_LEN - 1] = '\0';
    } else {
        _filename[0] = '\0';
    }
    _fileSize = fileSize;
    _bytesTransferred = 0;
}

bool ZModemEngine::send(unsigned long timeout) {
    if (!_io || !_file) return false;
    _isSender = true;
    _state = STATE_SEND_ZRQINIT;
    _rState = RSTATE_IDLE;
    _timeoutMs = timeout;
    _operationStartTime = millis();
    _lastActivity = millis();
    _retryCount = 0;
    _inBufLen = 0;
    _fileInfoIndex = 0;
    _fileInfoEscape = false;
    _fileInfoAwaitingCRC = false;
    _fileInfoFrameEnd = 0;
    _dataSubpacketLen = 0;
    _dataSubpacketEscape = false;
    _dataSubpacketAwaitingCRC = false;
    _dataSubpacketFrameEnd = 0;
    _dataExpectedPos = 0;
    _xmodemStarted = false;
    _xmodemRetryCount = 0;
    _xmodemRetryInterval = 0;
    _xmodemExpectedBlock = 1;
    _xmodemSendStarted = false;
    _xmodemSendBlock = 1;
    _xmodemSendRetry = 0;
    _xmodemSendLast = 0;
    _xmodemSendInterval = 0;
    _xmodemLastPending = false;
    _lastDataPending = false;
    _lastDataEndFrame = false;
    _loggedState = _state;
    if (_debug) {
        _debug->print("ZModemEngine: send() started\n");
    }
    return true;
}

bool ZModemEngine::receive(unsigned long timeout) {
    if (!_io) return false;
    _isSender = false;
    _state = STATE_RECEIVING;
    _rState = RSTATE_AWAIT_HEADER;
    _timeoutMs = timeout;
    _operationStartTime = millis();
    _lastActivity = millis();
    _inBufLen = 0;
    _fileInfoIndex = 0;
    _fileInfoEscape = false;
    _fileInfoAwaitingCRC = false;
    _fileInfoFrameEnd = 0;
    _dataSubpacketLen = 0;
    _dataSubpacketEscape = false;
    _dataSubpacketAwaitingCRC = false;
    _dataSubpacketFrameEnd = 0;
    _dataExpectedPos = 0;
    _xmodemStarted = false;
    _xmodemRetryCount = 0;
    _xmodemRetryInterval = 0;
    _xmodemExpectedBlock = 1;
    _xmodemSendStarted = false;
    _xmodemLastPending = false;
    _lastDataPending = false;
    _lastDataEndFrame = false;
    
    _loggedState = _state;

    // Receiver sends ZRINIT to start
    _sendHexHeader(ZRINIT, ZERO_FLAGS); 
    if (_debug) {
        _debug->print("ZModemEngine: receive() started, ZRINIT sent\n");
    }
    return true;
}

void ZModemEngine::abort() {
    if (_io) {
        uint8_t abortSeq[] = {ZDLE, ZCAN, ZDLE, ZCAN, ZDLE, ZCAN, ZDLE, ZCAN};
        _io->write(abortSeq, 8);
        _io->flush();
    }
    _state = STATE_ERROR;
}

// Simple XMODEM constants
#define XSOH 0x01
#define XSTX 0x02
#define XEOT 0x04
#define XACK 0x06
#define XNAK 0x15
#define XCAN 0x18


int ZModemEngine::loop() {
    if (_state == STATE_IDLE || _state == STATE_COMPLETE || _state == STATE_ERROR) {
        return (_state == STATE_COMPLETE) ? 1 : (_state == STATE_ERROR ? -1 : 0);
    }

    // Timeout Check
    if (millis() - _lastActivity > _timeoutMs) {
        _state = STATE_ERROR;
        if (_debug) {
            _debug->print("ZModemEngine: timeout exceeded, entering ERROR state\n");
        }
        return -1;
    }

    if (_isSender) {
        _handleSenderLoop();
    } else {
        _handleReceiverLoop();
    }
    _logStateIfChanged();
    return (_state == STATE_COMPLETE) ? 1 : (_state == STATE_ERROR ? -1 : 0);
}

// --- Sender Logic ---
void ZModemEngine::_handleSenderLoop() {
    // If XMODEM compatibility is enabled, prefer using the XMODEM sender fallback
    if (_xmodemEnabled && _isSender) {
        _handleXmodemSender();
        return;
    }
    uint8_t rxType;
    uint8_t rxFlags[4];

    _fillInput();
    if (_inputContainsAbort()) {
        _state = STATE_ERROR;
        if (_debug) _debug->print("ZModemEngine: abort sequence received\n");
        return;
    }
    
    // Check for incoming ACKs/NAKs
    if (_io->available() || _inBufLen > 0) {
        int res = _readHexHeader(rxType, rxFlags);
        if (res == 1) {
            _lastActivity = millis();
            // Process response
            switch(_state) {
                case STATE_SEND_ZRQINIT:
                case STATE_AWAIT_ZRINIT:
                    if (rxType == ZRINIT) {
                        _state = STATE_SEND_ZFILE;
                    }
                    break;
                case STATE_SEND_ZFILE:
                    if (rxType == ZRPOS) {
                         // Ack for file, requested position
                         size_t pos = rxFlags[0] | (rxFlags[1] << 8) | (rxFlags[2] << 16) | (rxFlags[3] << 24);
                         if (_file) _file->seek(pos);
                         _bytesTransferred = pos;
                         _state = STATE_SEND_ZDATA;
                    }
                    break;
                case STATE_SEND_ZDATA:
                     if (rxType == ZACK) {
                         // Chunk acked: clear pending resend state and reset backoff
                         _lastDataPending = false;
                         _retryCount = 0;
                         _retryIntervalMs = _baseRetryIntervalMs;
                     } else if (rxType == ZRPOS) {
                         // Resend from pos (Error or checkpoint)
                         size_t pos = rxFlags[0] | (rxFlags[1] << 8) | (rxFlags[2] << 16) | (rxFlags[3] << 24);
                         if (_file) _file->seek(pos);
                         _bytesTransferred = pos;
                         // If we have cached data overlapping this position, mark pending so sender will retransmit
                         if (_lastDataLen > 0 && _lastDataPos == pos) {
                             _lastDataPending = true;
                             _retryIntervalMs = _baseRetryIntervalMs;
                             _lastSendTime = millis();
                             _retryCount = 0;
                         }
                     }
                     break;
                case STATE_SEND_ZEOF:
                    if (rxType == ZACK) {
                        _lastDataPending = false;
                        _retryCount = 0;
                        _retryIntervalMs = _baseRetryIntervalMs;
                    } else if (rxType == ZRPOS) {
                        size_t pos = rxFlags[0] | (rxFlags[1] << 8) | (rxFlags[2] << 16) | (rxFlags[3] << 24);
                        if (_file) {
                            _file->seek(pos);
                        }
                        _bytesTransferred = pos;
                        _lastDataPending = (_lastDataLen > 0 && _lastDataPos == pos);
                        if (_lastDataPending) {
                            _retryIntervalMs = _baseRetryIntervalMs;
                            _lastSendTime = millis();
                            _retryCount = 0;
                        }
                        _state = STATE_SEND_ZDATA;
                    } else if (rxType == ZRINIT) {
                        // Receiver ready for next file or finish
                        _state = STATE_SEND_ZFIN;
                    }
                    break;
                case STATE_SEND_ZFIN:
                    if (rxType == ZFIN) {
                        // Two O's usually sent here
                        _io->print("OO");
                        _io->flush();
                        _state = STATE_COMPLETE;
                    }
                    break;
                default:
                    // Unexpected response
                    break;
            }
        }
    }

    // Sending Actions (Retry every 1 second if stuck on a state waiting for remote action)
    switch(_state) {
        case STATE_SEND_ZRQINIT:
            if (millis() - _senderLastSend > 1000) { 
                _sendHexHeader(ZRQINIT, ZERO_FLAGS);
                _senderLastSend = millis();
            }
            break;

        case STATE_SEND_ZFILE:
             // Send ZFILE Header + Data Subpacket (Filename/Size)
             if (millis() - _senderLastSend > 1000) {
                 _sendBinaryHeader(ZFILE, ZERO_FLAGS);
                 // Build filename\0filesize\0 in a bounded payload
                 char payload[128];
                 payload[0] = '\0';
                 size_t used = 0;
                 // copy filename
                 if (_filename[0]) {
                     strncpy(payload, _filename, sizeof(payload) - 1);
                     payload[sizeof(payload) - 1] = '\0';
                     used = strnlen(payload, sizeof(payload));
                 }
                 // append NUL and filesize string
                 if (used + 1 < sizeof(payload)) {
                     payload[used] = '\0';
                     used += 1;
                     // write filesize as ASCII
                     char sizeStr[32];
                     snprintf(sizeStr, sizeof(sizeStr), "%lu", (unsigned long)_fileSize);
                     size_t sizeLen = strnlen(sizeStr, sizeof(sizeStr));
                     size_t copyLen = ((used + sizeLen + 1) < sizeof(payload)) ? sizeLen : (sizeof(payload) - used - 1);
                     if (copyLen > 0) {
                         memcpy(payload + used, sizeStr, copyLen);
                         used += copyLen;
                     }
                     // ensure trailing NUL
                     if (used < sizeof(payload)) payload[used++] = '\0';
                 }

                 _sendDataSubpacket((const uint8_t*)payload, used, true); // End frame
                 _senderLastSend = millis();
             }
             break;

        case STATE_SEND_ZDATA:
            // If we have a pending last-data that needs retransmit and the retry timer expired, resend
            if (_lastDataPending) {
                if (millis() - _lastSendTime >= _retryIntervalMs) {
                    if (_retryCount >= MAX_RETRIES) {
                        // Too many retries, abort
                        _state = STATE_ERROR;
                        if (_debug) _debug->print("ZModemEngine: max retries exceeded, aborting\n");
                        return;
                    }
                    // Retransmit last header + data
                    uint8_t pos[4];
                    pos[0] = _lastDataPos & 0xFF;
                    pos[1] = (_lastDataPos >> 8) & 0xFF;
                    pos[2] = (_lastDataPos >> 16) & 0xFF;
                    pos[3] = (_lastDataPos >> 24) & 0xFF;
                    _sendBinaryHeader(ZDATA, pos);
                    _sendDataSubpacket(_lastDataBuf, _lastDataLen, _lastDataEndFrame);
                    _lastSendTime = millis();
                    _retryCount++;
                    _retryIntervalMs = (unsigned long)min((unsigned long)MAX_RETRY_INTERVAL_MS, _retryIntervalMs * 2UL);
                }
            } else {
                // Stream new file data into a buffer and send
                if (_file && _file->available()) {
                    size_t chunkSz = sizeof(_lastDataBuf);
                    size_t readLen = _file->read(_lastDataBuf, chunkSz);
                    if (readLen > 0) {
                        bool isLast = (_file->available() == 0);
                        // Use an explicit 4-byte little-endian offset for flags
                        uint8_t pos[4];
                        pos[0] = _bytesTransferred & 0xFF;
                        pos[1] = (_bytesTransferred >> 8) & 0xFF;
                        pos[2] = (_bytesTransferred >> 16) & 0xFF;
                        pos[3] = (_bytesTransferred >> 24) & 0xFF;
                        _sendBinaryHeader(ZDATA, pos);
                        _sendDataSubpacket(_lastDataBuf, readLen, isLast);
                        // Cache last data for potential retransmit
                        _lastDataLen = readLen;
                        _lastDataPos = _bytesTransferred;
                        _lastDataEndFrame = isLast;
                        _lastDataPending = true;
                        _lastSendTime = millis();
                        _retryCount = 0;
                        _retryIntervalMs = _baseRetryIntervalMs;

                        _bytesTransferred += readLen;
                        if (isLast) _state = STATE_SEND_ZEOF;
                    }
                } else if (_bytesTransferred == _fileSize) {
                    _state = STATE_SEND_ZEOF;
                }
            }
            break;
             
        case STATE_SEND_ZEOF:
             if (millis() - _senderLastSend > 1000) {
                 uint8_t pos[4];
                 pos[0] = _bytesTransferred & 0xFF;
                 pos[1] = (_bytesTransferred >> 8) & 0xFF;
                 pos[2] = (_bytesTransferred >> 16) & 0xFF;
                 pos[3] = (_bytesTransferred >> 24) & 0xFF;
                 _sendHexHeader(ZEOF, pos);
                 _senderLastSend = millis();
             }
             break;

        case STATE_SEND_ZFIN:
             if (millis() - _senderLastSend > 1000) {
                 _sendHexHeader(ZFIN, ZERO_FLAGS);
                 _senderLastSend = millis();
             }
             break;
        default:
             // Should not happen
             break;
    }
}

// --- Receiver Logic ---
void ZModemEngine::_handleReceiverLoop() {
    uint8_t rxType;
    uint8_t rxFlags[4];

    if (_xmodemEnabled && (_xmodemStarted || (_io && _io->available() &&
                                              (_io->peek() == XSOH || _io->peek() == XSTX || _io->peek() == XEOT)))) {
        _handleXmodemReceiver();
        return;
    }

    _fillInput();
    if (_inputContainsAbort()) {
        _state = STATE_ERROR;
        _rState = RSTATE_ERROR;
        if (_debug) _debug->print("ZModemEngine: abort sequence received\n");
        return;
    }

    if ((_rState == RSTATE_AWAIT_HEADER || _rState == RSTATE_AWAIT_ZDATA) && _inBufLen > 0) {
        int res = _readHeader(rxType, rxFlags);

        if (res == 1) {
            _lastActivity = millis();

            if (rxType == ZRQINIT) {
                _sendHexHeader(ZRINIT, ZERO_FLAGS);
                _rState = RSTATE_AWAIT_HEADER;
            } else if (rxType == ZFILE) {
                _fileInfoIndex = 0;
                _fileInfoEscape = false;
                _fileInfoAwaitingCRC = false;
                _fileInfoFrameEnd = 0;
                _rState = RSTATE_READ_ZFILE;
            } else if (rxType == ZDATA) {
                _dataExpectedPos = (size_t)rxFlags[0]
                    | ((size_t)rxFlags[1] << 8)
                    | ((size_t)rxFlags[2] << 16)
                    | ((size_t)rxFlags[3] << 24);
                _dataSubpacketLen = 0;
                _dataSubpacketEscape = false;
                _dataSubpacketAwaitingCRC = false;
                _dataSubpacketFrameEnd = 0;
                _rState = RSTATE_READ_ZDATA;
            } else if (rxType == ZEOF) {
                _sendHexHeader(ZRINIT, ZERO_FLAGS);
                _rState = RSTATE_AWAIT_HEADER;
            } else if (rxType == ZFIN) {
                _sendHexHeader(ZFIN, ZERO_FLAGS);
                _state = STATE_COMPLETE;
                _rState = RSTATE_COMPLETE;
                return;
            } else if (rxType == ZCAN || rxType == ZABORT) {
                _state = STATE_ERROR;
                _rState = RSTATE_ERROR;
                return;
            }
        }
    }

    if (_rState == RSTATE_READ_ZFILE) {
        _fillInput();

        size_t idx = 0;
        while (idx < _inBufLen) {
            if (_fileInfoAwaitingCRC) {
                uint8_t crcHigh;
                uint8_t crcLow;
                size_t crcIdx = idx;
                if (!_readEscapedByte(crcIdx, crcHigh) || !_readEscapedByte(crcIdx, crcLow)) break;

                uint16_t receivedCrc = ((uint16_t)crcHigh << 8) | crcLow;
                uint16_t crc = 0;
                for (size_t j = 0; j < _fileInfoIndex; ++j) crc = _updcrc(_fileInfoBuffer[j], crc);
                crc = _updcrc(_fileInfoFrameEnd, crc);
                idx = crcIdx;

                if (crc != receivedCrc) {
                    _sendHexHeader(ZNAK, ZERO_FLAGS);
                    _fileInfoIndex = 0;
                    _fileInfoEscape = false;
                    _fileInfoAwaitingCRC = false;
                    _fileInfoFrameEnd = 0;
                    _rState = RSTATE_AWAIT_HEADER;
                    _lastActivity = millis();
                    break;
                }

                size_t infoLen = (_fileInfoIndex < (sizeof(_fileInfoBuffer) - 1)) ? _fileInfoIndex : (sizeof(_fileInfoBuffer) - 1);
                _fileInfoBuffer[infoLen] = '\0';

                const char* filename = (const char*)_fileInfoBuffer;
                const char* sizeStr = nullptr;
                for (size_t i = 0; i < infoLen; ++i) {
                    if (_fileInfoBuffer[i] == '\0') {
                        sizeStr = (const char*)(_fileInfoBuffer + i + 1);
                        break;
                    }
                }

                if (!sizeStr) {
                    _sendHexHeader(ZNAK, ZERO_FLAGS);
                    _fileInfoIndex = 0;
                    _fileInfoEscape = false;
                    _fileInfoAwaitingCRC = false;
                    _fileInfoFrameEnd = 0;
                    _rState = RSTATE_AWAIT_HEADER;
                    break;
                }

                strncpy(_filename, filename, FILENAME_MAX_LEN - 1);
                _filename[FILENAME_MAX_LEN - 1] = '\0';
                _fileSize = (sizeStr[0] != '\0') ? strtoul(sizeStr, NULL, 10) : 0;

                _sendHexHeader(ZRPOS, ZERO_FLAGS);
                _fileInfoIndex = 0;
                _fileInfoEscape = false;
                _fileInfoAwaitingCRC = false;
                _fileInfoFrameEnd = 0;
                _rState = RSTATE_AWAIT_ZDATA;
                _lastActivity = millis();
                break;
            }

            uint8_t b = _inBuf[idx++];
            if (!_fileInfoEscape) {
                if (b == ZDLE) {
                    _fileInfoEscape = true;
                    continue;
                }
                if (_fileInfoIndex < sizeof(_fileInfoBuffer) - 1) {
                    _fileInfoBuffer[_fileInfoIndex++] = b;
                } else {
                    _state = STATE_ERROR;
                    return;
                }
            } else {
                _fileInfoEscape = false;
                if (b == ZCRCE || b == ZCRCG || b == ZCRCW) {
                    _fileInfoAwaitingCRC = true;
                    _fileInfoFrameEnd = b;
                } else {
                    uint8_t orig = b ^ 0x40;
                    if (_fileInfoIndex < sizeof(_fileInfoBuffer) - 1) {
                        _fileInfoBuffer[_fileInfoIndex++] = orig;
                    } else {
                        _state = STATE_ERROR;
                        return;
                    }
                }
            }
        }

        if (idx > 0 && idx <= _inBufLen) {
            if (idx < _inBufLen) memmove(_inBuf, _inBuf + idx, _inBufLen - idx);
            _inBufLen -= idx;
        }
    }

    else if (_rState == RSTATE_READ_ZDATA && _file) {
        _fillInput();

        size_t idx = 0;
        while (idx < _inBufLen) {
            if (_dataSubpacketAwaitingCRC) {
                uint8_t crcHigh;
                uint8_t crcLow;
                size_t crcIdx = idx;
                if (!_readEscapedByte(crcIdx, crcHigh) || !_readEscapedByte(crcIdx, crcLow)) break;

                uint16_t receivedCrc = ((uint16_t)crcHigh << 8) | crcLow;
                uint16_t crc = 0;
                for (size_t j = 0; j < _dataSubpacketLen; ++j) crc = _updcrc(_dataSubpacketBuffer[j], crc);
                crc = _updcrc(_dataSubpacketFrameEnd, crc);
                idx = crcIdx;

                if (crc == receivedCrc) {
                    if (_dataExpectedPos > _bytesTransferred) {
                        uint8_t pos[4];
                        pos[0] = _bytesTransferred & 0xFF;
                        pos[1] = (_bytesTransferred >> 8) & 0xFF;
                        pos[2] = (_bytesTransferred >> 16) & 0xFF;
                        pos[3] = (_bytesTransferred >> 24) & 0xFF;
                        _sendHexHeader(ZRPOS, pos);
                    } else if (_dataExpectedPos + _dataSubpacketLen <= _bytesTransferred) {
                        _sendHexHeader(ZACK, ZERO_FLAGS);
                    } else {
                        const size_t skip = _bytesTransferred - _dataExpectedPos;
                        if (_dataSubpacketLen > skip) {
                            _file->write(_dataSubpacketBuffer + skip, _dataSubpacketLen - skip);
                            _bytesTransferred += (_dataSubpacketLen - skip);
                        }
                        _sendHexHeader(ZACK, ZERO_FLAGS);
                    }
                    _lastActivity = millis();
                } else {
                    uint8_t pos[4];
                    pos[0] = _bytesTransferred & 0xFF;
                    pos[1] = (_bytesTransferred >> 8) & 0xFF;
                    pos[2] = (_bytesTransferred >> 16) & 0xFF;
                    pos[3] = (_bytesTransferred >> 24) & 0xFF;
                    _sendHexHeader(ZRPOS, pos);
                    _lastActivity = millis();
                }

                _dataSubpacketLen = 0;
                _dataSubpacketEscape = false;
                _dataSubpacketAwaitingCRC = false;
                _dataSubpacketFrameEnd = 0;
                _rState = RSTATE_AWAIT_ZDATA;
                break;
            }

            uint8_t b = _inBuf[idx++];
            if (!_dataSubpacketEscape) {
                if (b == ZDLE) {
                    _dataSubpacketEscape = true;
                    continue;
                }
                if (_dataSubpacketLen < sizeof(_dataSubpacketBuffer)) {
                    _dataSubpacketBuffer[_dataSubpacketLen++] = b;
                } else {
                    _state = STATE_ERROR;
                    return;
                }
            } else {
                _dataSubpacketEscape = false;
                if (b == ZCRCE || b == ZCRCG || b == ZCRCW) {
                    _dataSubpacketAwaitingCRC = true;
                    _dataSubpacketFrameEnd = b;
                } else {
                    uint8_t orig = b ^ 0x40;
                    if (_dataSubpacketLen < sizeof(_dataSubpacketBuffer)) {
                        _dataSubpacketBuffer[_dataSubpacketLen++] = orig;
                    } else {
                        _state = STATE_ERROR;
                        return;
                    }
                }
            }
        }

        if (idx > 0 && idx <= _inBufLen) {
            if (idx < _inBufLen) memmove(_inBuf, _inBuf + idx, _inBufLen - idx);
            _inBufLen -= idx;
        }
    }
    
    // Keepalive only while waiting for the session to start. Sending ZRINIT
    // during ZDATA/ZEOF can make the sender skip remaining file confirmation.
    if (_rState == RSTATE_AWAIT_HEADER && _bytesTransferred == 0 &&
        millis() - _receiverLastAck > 3000 && _state != STATE_COMPLETE && _state != STATE_ERROR) {
        _sendHexHeader(ZRINIT, ZERO_FLAGS);
        _receiverLastAck = millis();
    }

    // If XMODEM compatibility enabled and we haven't entered ZMODEM transfer after some time,
    // try XMODEM handling (non-blocking). This allows legacy tools to send via XMODEM.
    if (_xmodemEnabled && _state != STATE_COMPLETE && _state != STATE_ERROR) {
        _handleXmodemReceiver();
    }
}

// Basic XMODEM receiver (non-blocking, checksum-based fallback)
void ZModemEngine::_handleXmodemReceiver() {
    if (!_io || !_file) return;

    // We'll attempt CRC-mode first if enabled. If not started, periodically send 'C' to request CRC.
    if (!_xmodemStarted) {
        unsigned long now = millis();
        if (_xmodemRetryInterval == 0) {
            _xmodemRetryInterval = DEFAULT_BASE_RETRY_MS;
            _xmodemLastSend = 0;
            _xmodemRetryCount = 0;
        }
        if (now - _xmodemLastSend >= _xmodemRetryInterval) {
            // send requester: 'C' for CRC mode or NAK for checksum
            if (_xmodemUseCRC) _io->write('C'); else _io->write(XNAK);
            _io->flush();
            _xmodemLastSend = now;
            _xmodemRetryCount++;
            // exponential backoff
            if (_xmodemRetryInterval < MAX_RETRY_INTERVAL_MS) _xmodemRetryInterval = min((unsigned long)MAX_RETRY_INTERVAL_MS, _xmodemRetryInterval * 2UL);
            if (_xmodemRetryCount > XMODEM_MAX_RETRIES) {
                if (_debug) _debug->print("ZModemEngine: XMODEM no response, giving up\n");
                _xmodemStarted = false;
                return;
            }
        }
        // wait for incoming starter
        if (!_io->available()) return;
    }

    int c = _io->peek();
    if (c == -1) return;

    // Check for EOT
    if (c == XEOT) {
        _io->read();
        _io->write(XACK);
        _io->flush();
        _lastActivity = millis();
        _state = STATE_COMPLETE;
        if (_debug) _debug->print("ZModemEngine: XMODEM EOT received, transfer complete\n");
        return;
    }

    // 128-byte XMODEM only. 1K (STX) would require a 1KB stack buffer on a
    // constrained MCU, so reject it and stay on the standard 128-byte path.
    if (c == XSTX) {
        _io->read();
        _io->write(XNAK);
        _io->flush();
        return;
    }
    if (c != XSOH) {
        return;
    }

    _xmodemStarted = true;

    const size_t blockSize = 128;
    size_t needed = 1 + 2 + blockSize + (_xmodemUseCRC ? 2 : 1);
    if ((size_t)_io->available() < needed) {
        return;
    }

    int start = _io->read(); // consume SOH
    (void)start;
    int blk = _io->read();
    int blkComp = _io->read();
    if ((blk ^ blkComp) != 0xFF) {
        _io->write(XNAK);
        _io->flush();
        return;
    }

    uint8_t dataBuf[128];
    for (size_t i = 0; i < blockSize; ++i) {
        int b = _io->read(); if (b == -1) { _io->write(XNAK); return; }
        dataBuf[i] = (uint8_t)b;
    }

    // Read checksum/CRC
    uint16_t receivedCrc = 0;
    if (_xmodemUseCRC) {
        int hi = _io->read(); int lo = _io->read();
        if (hi == -1 || lo == -1) { _io->write(XNAK); return; }
        receivedCrc = ((uint16_t)hi << 8) | (uint16_t)lo;
        uint16_t calc = _calcCRC16(dataBuf, blockSize);
        if (calc != receivedCrc) {
            // CRC fail -> request resend
            _io->write(XNAK);
            _io->flush();
            // retry/backoff
            if (_xmodemRetryCount++ >= XMODEM_MAX_RETRIES) { _state = STATE_ERROR; return; }
            if (_xmodemRetryInterval == 0) _xmodemRetryInterval = DEFAULT_BASE_RETRY_MS;
            _xmodemRetryInterval = min((unsigned long)MAX_RETRY_INTERVAL_MS, _xmodemRetryInterval * 2UL);
            _xmodemLastSend = millis();
            return;
        }
    } else {
        int chksum = _io->read(); if (chksum == -1) { _io->write(XNAK); return; }
        uint8_t sum = 0; for (size_t i = 0; i < blockSize; ++i) sum = (uint8_t)(sum + dataBuf[i]);
        if (sum != (uint8_t)chksum) {
            _io->write(XNAK);
            _io->flush();
            if (_xmodemRetryCount++ >= XMODEM_MAX_RETRIES) { _state = STATE_ERROR; return; }
            if (_xmodemRetryInterval == 0) _xmodemRetryInterval = DEFAULT_BASE_RETRY_MS;
            _xmodemRetryInterval = min((unsigned long)MAX_RETRY_INTERVAL_MS, _xmodemRetryInterval * 2UL);
            _xmodemLastSend = millis();
            return;
        }
    }

    // Block OK: check block number
    if (blk == _xmodemExpectedBlock) {
        _file->write(dataBuf, blockSize);
        _bytesTransferred += blockSize;
        _io->write(XACK);
        _io->flush();
        _lastActivity = millis();
        _xmodemExpectedBlock++;
        _xmodemRetryCount = 0;
        _xmodemRetryInterval = DEFAULT_BASE_RETRY_MS;
    } else if (blk == (uint8_t)(_xmodemExpectedBlock - 1)) {
        // duplicate block (sender retransmitted last, maybe ACK lost) -> ack again
        _io->write(XACK);
        _io->flush();
    } else {
        // unexpected block number
        _io->write(XNAK);
        _io->flush();
    }
}

// XMODEM sender implementation (CRC-mode preferred). Non-blocking.
void ZModemEngine::_handleXmodemSender() {
    if (!_io || !_file) return;

    // Wait for receiver request ('C' for CRC or NAK for checksum)
    if (!_xmodemSendStarted) {
        if (_io->available()) {
            int b = _io->read();
            if (b == 'C' || b == 'c') {
                _xmodemUseCRC = true;
                _xmodemSendStarted = true;
                _xmodemSendBlock = 1;
                _xmodemSendRetry = 0;
                _xmodemSendInterval = DEFAULT_BASE_RETRY_MS;
                _xmodemSendLast = 0;
            } else if (b == XNAK) {
                _xmodemUseCRC = false;
                _xmodemSendStarted = true;
                _xmodemSendBlock = 1;
                _xmodemSendRetry = 0;
                _xmodemSendInterval = DEFAULT_BASE_RETRY_MS;
                _xmodemSendLast = 0;
            } else {
                // ignore other bytes
                return;
            }
        } else {
            return;
        }
    }

    // If currently waiting for ACK/NAK for last block, check for responses or timeout
    if (_xmodemSendLast != 0 && (millis() - _xmodemSendLast) < _xmodemSendInterval) {
        // check for ACK/NAK
        if (_io->available()) {
            int r = _io->read();
            if (r == XACK) {
                // success, advance block
                _xmodemSendBlock++;
                _xmodemSendLast = 0;
                _xmodemSendRetry = 0;
                _xmodemSendInterval = DEFAULT_BASE_RETRY_MS;
                // On ACK, commit the cached block (advance bytesTransferred) and clear cache
                if (_xmodemLastPending) {
                    // commit position
                    _bytesTransferred = _xmodemLastPos + _xmodemLastLen;
                    _xmodemLastPending = false;
                }
            } else if (r == XNAK) {
                // resend last block
                if (_xmodemSendRetry++ >= XMODEM_MAX_RETRIES) { _state = STATE_ERROR; return; }
                _xmodemSendInterval = min((unsigned long)MAX_RETRY_INTERVAL_MS, _xmodemSendInterval * 2UL);
                // leave _xmodemLastPending true and schedule immediate resend
                _xmodemSendLast = 0; // trigger resend below
            } else if (r == XCAN) {
                _state = STATE_ERROR; return;
            } else {
                // ignore
            }
        }
        return;
    }

    // Send next block if any
    if (_file && !_file->available() && _bytesTransferred < _fileSize) {
        // Ensure file pointer is at correct offset
        _file->seek(_bytesTransferred);
    }

    if (_bytesTransferred >= _fileSize) {
        // No more data: send EOT and wait for ACK
        if (_xmodemSendLast == 0) {
            _io->write(XEOT);
            _io->flush();
            _xmodemSendLast = millis();
            _xmodemSendRetry = 0;
            _xmodemSendInterval = DEFAULT_BASE_RETRY_MS;
            return;
        } else {
            // waiting for ACK for EOT
            if (_io->available()) {
                int r = _io->read();
                if (r == XACK) {
                    _state = STATE_COMPLETE;
                    return;
                } else if (r == XNAK) {
                    if (_xmodemSendRetry++ >= XMODEM_MAX_RETRIES) {
                        _state = STATE_ERROR;
                        return;
                    }
                    _xmodemSendLast = 0; // resend EOT
                    _xmodemSendInterval = min((unsigned long)MAX_RETRY_INTERVAL_MS, _xmodemSendInterval * 2UL);
                    return;
                }
            }
            // timeout handling
            if (millis() - _xmodemSendLast >= _xmodemSendInterval) {
                if (_xmodemSendRetry++ >= XMODEM_MAX_RETRIES) {
                    _state = STATE_ERROR;
                    return;
                }
                _xmodemSendLast = 0; // resend
                _xmodemSendInterval = min((unsigned long)MAX_RETRY_INTERVAL_MS, _xmodemSendInterval * 2UL);
            }
            return;
        }
    }

    // Prepare/send block using cached buffer when available. This avoids extra
    // file seeks/reads on retransmit and enables immediate resend of the last block.
    if (!_xmodemLastPending) {
        // Need to fill cache with next block
        if (_file) {
            // ensure file pointer
            _file->seek(_bytesTransferred);
            size_t readLen = _file->read(_xmodemLastBlock, 128);
            _xmodemLastLen = readLen;
            // pad if short
            if (_xmodemLastLen < 128) {
                for (size_t i = _xmodemLastLen; i < 128; ++i) _xmodemLastBlock[i] = 0x1A;
            }
        } else {
            // no file -> send empty padded block
            _xmodemLastLen = 0;
            for (size_t i = 0; i < 128; ++i) _xmodemLastBlock[i] = 0x1A;
        }
        _xmodemLastPos = _bytesTransferred;
        _xmodemLastPending = true;
    }

    // Send the cached block (either first-send or retransmit)
    _io->write(XSOH);
    _io->write(_xmodemSendBlock);
    _io->write(255 - _xmodemSendBlock);
    _io->write(_xmodemLastBlock, 128);
    if (_xmodemUseCRC) {
        uint16_t crc = _calcCRC16(_xmodemLastBlock, 128);
        _io->write((crc >> 8) & 0xFF);
        _io->write(crc & 0xFF);
    } else {
        uint8_t sum = 0; for (int i = 0; i < 128; ++i) sum = (uint8_t)(sum + _xmodemLastBlock[i]);
        _io->write(sum);
    }

    _xmodemSendLast = millis();
    _xmodemSendRetry = 0;
    _xmodemSendInterval = DEFAULT_BASE_RETRY_MS;
    _io->flush();
}


// --- Low Level Protocol (Unchanged, simplified implementation) ---

void ZModemEngine::_sendHexHeader(uint8_t type, const uint8_t* flags) {
    uint16_t crc = 0;
    
    _io->write(ZPAD); _io->write(ZPAD);
    _io->write(ZDLE); _io->write(ZHEX);
    
    char hexBuf[12];
    snprintf(hexBuf, sizeof(hexBuf), "%02X%02X%02X%02X%02X", type, flags[0], flags[1], flags[2], flags[3]);
    
    crc = _updcrc(type, crc);
    crc = _updcrc(flags[0], crc);
    crc = _updcrc(flags[1], crc);
    crc = _updcrc(flags[2], crc);
    crc = _updcrc(flags[3], crc);
    
    _io->print(hexBuf);
    
    snprintf(hexBuf, sizeof(hexBuf), "%02X%02X", (crc >> 8) & 0xFF, crc & 0xFF);
    _io->print(hexBuf);
    
    _io->write('\r'); _io->write('\n');
    if (type != ZFIN && type != ZACK) _io->write(0x11); // XON
    _io->flush();
}

void ZModemEngine::_sendBinaryHeader(uint8_t type, const uint8_t* flags) {
    uint16_t crc = 0;
    _io->write(ZPAD); _io->write(ZDLE); _io->write(ZBIN);
    
    crc = _updcrc(type, crc);       _writeZdle(type);
    crc = _updcrc(flags[0], crc);   _writeZdle(flags[0]);
    crc = _updcrc(flags[1], crc);   _writeZdle(flags[1]);
    crc = _updcrc(flags[2], crc);   _writeZdle(flags[2]);
    crc = _updcrc(flags[3], crc);   _writeZdle(flags[3]);
    
    _writeZdle((crc >> 8) & 0xFF);
    _writeZdle(crc & 0xFF);
}

void ZModemEngine::_sendDataSubpacket(const uint8_t* data, size_t len, bool endFrame) {
    uint16_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc = _updcrc(data[i], crc);
        _writeZdle(data[i]);
    }

    const uint8_t frameEnd = endFrame ? ZCRCE : ZCRCG;
    _io->write(ZDLE);
    _io->write(frameEnd);
    crc = _updcrc(frameEnd, crc);

    _writeZdle((crc >> 8) & 0xFF);
    _writeZdle(crc & 0xFF);
    _io->flush();
}

// Simple CRC16 XMODEM update
uint16_t ZModemEngine::_updcrc(uint8_t c, uint16_t crc) {
    int count;
    crc = crc ^ (((uint16_t)c) << 8);
    for (count = 0; count < 8; count++) {
        if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
        else crc = crc << 1;
    }
    return crc;
}

// Compute 16-bit CRC-CCITT for buffer (compatible with XMODEM-CRC)
uint16_t ZModemEngine::_calcCRC16(const uint8_t* data, size_t len) {
    uint16_t crc = 0;
    for (size_t i = 0; i < len; ++i) {
        crc = _updcrc(data[i], crc);
    }
    return crc;
}

// Simplified Header Reader
int ZModemEngine::_readHexHeader(uint8_t& type, uint8_t* flags) {
    // Non-destructive header parsing using internal buffer. We fill from
    // _io when available and parse only from _inBuf; bytes are removed from
    // the buffer only when a complete header is consumed.

    _fillInput();

    // Scan forward to find the header start sequence (ZPAD ZPAD ZDLE ZHEX),
    // discarding any leading garbage (e.g. XON bytes, CR/LF from previous frames).
    while (_inBufLen >= 4) {
        if (_inBuf[0] == ZPAD && _inBuf[1] == ZPAD && _inBuf[2] == ZDLE && _inBuf[3] == ZHEX)
            break;
        memmove(_inBuf, _inBuf + 1, _inBufLen - 1);
        _inBufLen--;
    }

    // 4 control bytes + 14 ASCII hex chars (type, 4 flags, CRC). CRLF/XON are optional trailer.
    const size_t HEX_CHARS = 14;
    if (_inBufLen < 4 + HEX_CHARS) return 0;

    auto hexVal = [](uint8_t c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    auto parseHexByte = [&](size_t idx, uint8_t& out) -> bool {
        int high = hexVal(_inBuf[idx]);
        int low = hexVal(_inBuf[idx + 1]);
        if (high < 0 || low < 0) return false;
        out = (uint8_t)((high << 4) | low);
        return true;
    };

    uint8_t parsedType;
    uint8_t parsedFlags[4];
    uint8_t crcHigh;
    uint8_t crcLow;
    if (!parseHexByte(4, parsedType) ||
        !parseHexByte(6, parsedFlags[0]) ||
        !parseHexByte(8, parsedFlags[1]) ||
        !parseHexByte(10, parsedFlags[2]) ||
        !parseHexByte(12, parsedFlags[3]) ||
        !parseHexByte(14, crcHigh) ||
        !parseHexByte(16, crcLow)) {
        memmove(_inBuf, _inBuf + 1, _inBufLen - 1);
        _inBufLen--;
        return 0;
    }

    uint16_t receivedCrc = ((uint16_t)crcHigh << 8) | crcLow;
    uint16_t computedCrc = 0;
    computedCrc = _updcrc(parsedType, computedCrc);
    for (int i = 0; i < 4; ++i) computedCrc = _updcrc(parsedFlags[i], computedCrc);

    size_t consumed = 4 + HEX_CHARS;
    if (consumed < _inBufLen && _inBuf[consumed] == '\r') consumed++;
    if (consumed < _inBufLen && _inBuf[consumed] == '\n') consumed++;
    if (consumed < _inBufLen && _inBuf[consumed] == 0x11) consumed++;

    if (consumed < _inBufLen) memmove(_inBuf, _inBuf + consumed, _inBufLen - consumed);
    _inBufLen -= consumed;
    if (computedCrc != receivedCrc) return 0;

    type = parsedType;
    for (int i = 0; i < 4; ++i) flags[i] = parsedFlags[i];
    return 1;
}

int ZModemEngine::_readHeader(uint8_t& type, uint8_t* flags) {
    _fillInput();

    while (_inBufLen > 0) {
        if (_inBuf[0] != ZPAD) {
            memmove(_inBuf, _inBuf + 1, _inBufLen - 1);
            _inBufLen--;
            continue;
        }

        if (_inBufLen >= 2 && _inBuf[1] == ZPAD) {
            if (_inBufLen < 4) return 0;
            if (_inBuf[2] == ZDLE && _inBuf[3] == ZHEX) {
                return _readHexHeader(type, flags);
            }
        }

        if (_inBufLen >= 2 && _inBuf[1] == ZDLE) {
            if (_inBufLen < 3) return 0;
            if (_inBuf[2] == ZBIN) {
                size_t idx = 3;
                auto readEscapedByte = [&](uint8_t& out) -> bool {
                    if (idx >= _inBufLen) return false;
                    uint8_t value = _inBuf[idx++];
                    if (value == ZDLE) {
                        if (idx >= _inBufLen) {
                            idx--;
                            return false;
                        }
                        value = _inBuf[idx++] ^ 0x40;
                    }
                    out = value;
                    return true;
                };

                uint8_t crcHigh;
                uint8_t crcLow;
                if (!readEscapedByte(type)) return 0;
                for (int i = 0; i < 4; ++i) {
                    if (!readEscapedByte(flags[i])) return 0;
                }
                if (!readEscapedByte(crcHigh) || !readEscapedByte(crcLow)) return 0;

                uint16_t computedCrc = 0;
                computedCrc = _updcrc(type, computedCrc);
                for (int i = 0; i < 4; ++i) computedCrc = _updcrc(flags[i], computedCrc);
                uint16_t receivedCrc = ((uint16_t)crcHigh << 8) | crcLow;

                if (idx < _inBufLen) memmove(_inBuf, _inBuf + idx, _inBufLen - idx);
                _inBufLen -= idx;
                if (computedCrc != receivedCrc) return 0;
                return 1;
            }
        }

        memmove(_inBuf, _inBuf + 1, _inBufLen - 1);
        _inBufLen--;
    }

    return 0;
}

void ZModemEngine::_fillInput() {
    if (!_io) return;
    int avail = _io->available();
    if (avail <= 0) return;
    size_t space = sizeof(_inBuf) - _inBufLen;
    if (space == 0) {
        const size_t drop = (_inBufLen > 64) ? 64 : 1;
        memmove(_inBuf, _inBuf + drop, _inBufLen - drop);
        _inBufLen -= drop;
        space = sizeof(_inBuf) - _inBufLen;
    }
    int toRead = avail < (int)space ? avail : (int)space;
    for (int i = 0; i < toRead; ++i) {
        int c = _io->read();
        if (c == -1) break;
        _inBuf[_inBufLen++] = (uint8_t)c;
    }
}

void ZModemEngine::_writeZdle(uint8_t value) {
    if (value == ZDLE || value == 0x10 || value == 0x11 || value == 0x13 || (value & 0x7F) == 0x0D) {
        _io->write(ZDLE);
        _io->write((uint8_t)(value ^ 0x40));
    } else {
        _io->write(value);
    }
}

bool ZModemEngine::_readEscapedByte(size_t& idx, uint8_t& out) {
    if (idx >= _inBufLen) return false;
    uint8_t value = _inBuf[idx++];
    if (value == ZDLE) {
        if (idx >= _inBufLen) {
            idx--;
            return false;
        }
        out = (uint8_t)(_inBuf[idx++] ^ 0x40);
        return true;
    }
    out = value;
    return true;
}

bool ZModemEngine::_inputContainsAbort() const {
    int pairs = 0;
    int consecutiveCan = 0;
    for (size_t i = 0; i < _inBufLen; ++i) {
        if (_inBuf[i] == 0x18) {
            consecutiveCan++;
            if (consecutiveCan >= 5) return true;
        } else {
            consecutiveCan = 0;
        }
        if (i + 1 < _inBufLen && _inBuf[i] == ZDLE && _inBuf[i + 1] == ZCAN) {
            pairs++;
            if (pairs >= 2) return true;
        }
    }
    return false;
}

void ZModemEngine::_logStateIfChanged() {
    if (!_debug || _state == _loggedState) return;
    _debug->print("ZModemEngine: state=");
    _debug->print((int)_state);
    _debug->print("\n");
    _loggedState = _state;
}
