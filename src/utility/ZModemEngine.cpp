/**
 * @file ZModemEngine.cpp
 * @author Akita Engineering
 * @brief Internal ZModem Implementation.
 * "Commercial Grade" robustness: Self-contained, CRC16, Non-blocking.
 * @version 1.1.0
 */

#include "ZModemEngine.h"

// ZModem Control Frame End (CRCE is final, CRCG/CRCW are intermediate)
#define ZCRCG 0x47
#define ZCRCE 0x45

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

    // File-info parsing state
    _fileInfoIndex = 0;
    _fileInfoEscape = false;
    _fileInfoAwaitingCRC = false;
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
    _timeoutMs = timeout;
    _operationStartTime = millis();
    _lastActivity = millis();
    _retryCount = 0;
    if (_debug) {
        _debug->print("ZModemEngine: send() started\n");
    }
    return true;
}

bool ZModemEngine::receive(unsigned long timeout) {
    if (!_io) return false;
    _isSender = false;
    _state = STATE_AWAIT_ZRINIT; // Generic start state
    _rState = RSTATE_AWAIT_HEADER;
    _timeoutMs = timeout;
    _operationStartTime = millis();
    _lastActivity = millis();
    
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
    }
    _state = STATE_ERROR;
}

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
    if (_debug) {
        // Log non-fatal state transitions for visibility
        _debug->print("ZModemEngine: loop tick, state=");
        _debug->print((int)_state);
        _debug->print("\n");
    }
    return (_state == STATE_COMPLETE) ? 1 : (_state == STATE_ERROR ? -1 : 0);
}

// --- Sender Logic ---
void ZModemEngine::_handleSenderLoop() {
    static unsigned long lastSend = 0;
    uint8_t rxType;
    uint8_t rxFlags[4];
    
    // Check for incoming ACKs/NAKs
    if (_io->available()) {
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
                         // Chunk acked, continue (handled implicitly by ZRPOS check)
                     } else if (rxType == ZRPOS) {
                         // Resend from pos (Error or checkpoint)
                         size_t pos = rxFlags[0] | (rxFlags[1] << 8) | (rxFlags[2] << 16) | (rxFlags[3] << 24);
                         if (_file) _file->seek(pos);
                         _bytesTransferred = pos;
                     }
                     break;
                case STATE_SEND_ZEOF:
                    if (rxType == ZRINIT) {
                        // Receiver ready for next file or finish
                        _state = STATE_SEND_ZFIN;
                    }
                    break;
                case STATE_SEND_ZFIN:
                    if (rxType == ZFIN) {
                        // Two O's usually sent here
                        _io->print("OO");
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
            if (millis() - lastSend > 1000) { 
                _sendHexHeader(ZRQINIT, ZERO_FLAGS);
                lastSend = millis();
            }
            break;

        case STATE_SEND_ZFILE:
             // Send ZFILE Header + Data Subpacket (Filename/Size)
             if (millis() - lastSend > 1000) {
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
                 lastSend = millis();
             }
             break;

        case STATE_SEND_ZDATA:
            // Stream file data
            if (_file && _file->available()) {
                uint8_t buf[128]; // chunk size
                size_t readLen = _file->read(buf, 128);
                if (readLen > 0) {
                    bool isLast = (_file->available() == 0);
                    // Use an explicit 4-byte little-endian offset for flags
                    uint8_t pos[4];
                    pos[0] = _bytesTransferred & 0xFF;
                    pos[1] = (_bytesTransferred >> 8) & 0xFF;
                    pos[2] = (_bytesTransferred >> 16) & 0xFF;
                    pos[3] = (_bytesTransferred >> 24) & 0xFF;
                    _sendBinaryHeader(ZDATA, pos);
                    _sendDataSubpacket(buf, readLen, isLast);
                    _bytesTransferred += readLen;
                    lastSend = millis();
                    if (isLast) _state = STATE_SEND_ZEOF;
                }
            } else if (_bytesTransferred == _fileSize) {
                _state = STATE_SEND_ZEOF;
            }
            break;
             
        case STATE_SEND_ZEOF:
             if (millis() - lastSend > 1000) {
                 uint8_t pos[4];
                 pos[0] = _bytesTransferred & 0xFF;
                 pos[1] = (_bytesTransferred >> 8) & 0xFF;
                 pos[2] = (_bytesTransferred >> 16) & 0xFF;
                 pos[3] = (_bytesTransferred >> 24) & 0xFF;
                 _sendHexHeader(ZEOF, pos);
                 lastSend = millis();
             }
             break;

        case STATE_SEND_ZFIN:
             if (millis() - lastSend > 1000) {
                 _sendHexHeader(ZFIN, ZERO_FLAGS);
                 lastSend = millis();
             }
             break;
        default:
             // Should not happen
             break;
    }
}

// --- Receiver Logic ---
void ZModemEngine::_handleReceiverLoop() {
    static unsigned long lastAck = 0;
    uint8_t rxType;
    uint8_t rxFlags[4];
    
    // Process incoming control header
    if (_io->available()) {
        int res = _readHexHeader(rxType, rxFlags);
        
        if (res == 1) {
             _lastActivity = millis();
             
             if (rxType == ZRQINIT) {
                 // Sender requests initialization
                 _sendHexHeader(ZRINIT, ZERO_FLAGS);
                 _rState = RSTATE_AWAIT_HEADER;
             }
             else if (rxType == ZFILE) {
                 // Sender announces file — next comes a data subpacket containing
                 // NUL-terminated filename and ASCII filesize. Enter the
                 // RSTATE_READ_ZFILE state and accumulate the subpacket
                 // non-blocking (we will parse when the terminating ZDLE/... is seen).
                 _fileInfoIndex = 0;
                 _fileInfoEscape = false;
                 _fileInfoAwaitingCRC = false;
                 _rState = RSTATE_READ_ZFILE;
             }
             else if (rxType == ZDATA) {
                 // ZDATA header received, next bytes are subpacket data
                 // The main loop handles the file stream write based on RSTATE_READ_ZDATA
                 
                 // NOTE: Since the ZModem Engine does NOT handle data subpacket reading/escaping/CRC checks
                 // directly in the loop, this is where commercial hardening is needed.
                 // This engine expects the stream to contain clean data if in RSTATE_READ_ZDATA.
                 
                 // Implicitly ACK by sending ZACK or ZRPOS after data read
             }
             else if (rxType == ZEOF) {
                 // Received End of File signal
                 _sendHexHeader(ZRINIT, ZERO_FLAGS); // Ready for next file or ZFIN
             }
             else if (rxType == ZFIN) {
                 _sendHexHeader(ZFIN, ZERO_FLAGS);
                 _state = STATE_COMPLETE;
             }
        }
    }
    
    // Data Writing Loop (if actively reading data)
    if (_rState == RSTATE_READ_ZFILE) {
        // Accumulate the ZFILE data subpacket (filename\0filesize\0), with ZDLE-escaping.
        // Read available bytes non-destructively into the internal input buffer
        _fillInput();
        // Parse from _inBuf similar to header parsing but handle ZFILE subpacket bytes
        // We will consume bytes from _inBuf as we process them.
        size_t idx = 0;
        while (idx < _inBufLen) {
            uint8_t b = _inBuf[idx++];
            if (!_fileInfoEscape) {
                if (b == ZDLE) { _fileInfoEscape = true; continue; }
                if (_fileInfoIndex < sizeof(_fileInfoBuffer) - 1) _fileInfoBuffer[_fileInfoIndex++] = b;
                else { _state = STATE_ERROR; return; }
            } else {
                _fileInfoEscape = false;
                if (b == ZCRCE || b == ZCRCG) {
                    // End-of-subpacket. Need two CRC bytes; ensure they are in _inBuf
                    if (idx + 2 > _inBufLen) {
                        // not enough bytes yet; roll back idx and wait
                        idx -= 1; // put back the marker
                        break;
                    }
                    // skip CRC bytes
                    idx += 2;

                    // Null-terminate and parse filename and filesize
                    _fileInfoBuffer[_fileInfoIndex] = 0;
                    const char* p = (const char*)_fileInfoBuffer;
                    size_t fnameLen = strlen(p);
                    const char* sizeStr = p + fnameLen + 1;
                    size_t parsedSize = 0;
                    if (sizeStr && *sizeStr) parsedSize = strtoul(sizeStr, NULL, 10);

                    // Store parsed filename into fixed buffer
                    strncpy(_filename, p, FILENAME_MAX_LEN - 1);
                    _filename[FILENAME_MAX_LEN - 1] = '\0';
                    _fileSize = parsedSize;

                    // Acknowledge and move to reading actual file data
                    _sendHexHeader(ZRPOS, ZERO_FLAGS);
                    _rState = RSTATE_READ_ZDATA;

                    // Reset file-info accumulators
                    _fileInfoIndex = 0;
                    _fileInfoEscape = false;
                    _fileInfoAwaitingCRC = false;

                    // Remove consumed bytes from _inBuf
                    if (idx < _inBufLen) memmove(_inBuf, _inBuf + idx, _inBufLen - idx);
                    _inBufLen -= idx;
                    idx = 0;
                    break;
                } else {
                    // Normal escaped byte
                    uint8_t orig = b ^ 0x40;
                    if (_fileInfoIndex < sizeof(_fileInfoBuffer) - 1) _fileInfoBuffer[_fileInfoIndex++] = orig;
                    else { _state = STATE_ERROR; return; }
                }
            }
        }
        // if we've consumed some bytes without finishing, remove them
        if (idx > 0 && idx <= _inBufLen) {
            if (idx < _inBufLen) memmove(_inBuf, _inBuf + idx, _inBufLen - idx);
            _inBufLen -= idx;
        }
    }
    
    else if (_rState == RSTATE_READ_ZDATA && _file) {
        // Read escaped subpackets, validate CRC, then write payload in bulk.
        _fillInput();

        // Temporary buffer for unescaped data
        const size_t SUBBUF_SZ = 512;
        uint8_t subbuf[SUBBUF_SZ];
        size_t subidx = 0;

        size_t idx = 0;
        bool escape = false;
        while (idx < _inBufLen) {
            uint8_t b = _inBuf[idx++];

            if (!escape) {
                if (b == ZDLE) { escape = true; continue; }
                // normal data byte
                if (subidx < SUBBUF_SZ) subbuf[subidx++] = b;
                else { _state = STATE_ERROR; return; }
            } else {
                // escaped byte or end-of-subpacket marker
                escape = false;
                if (b == ZCRCE || b == ZCRCG || b == ZCRCG) {
                    // End-of-subpacket marker: need two CRC bytes following
                    if (idx + 2 > _inBufLen) {
                        // wait for CRC bytes to arrive
                        idx -= 1; // push back ZDLE marker handling
                        break;
                    }

                    uint8_t crcHigh = _inBuf[idx++];
                    uint8_t crcLow = _inBuf[idx++];
                    uint16_t receivedCrc = ((uint16_t)crcHigh << 8) | crcLow;

                    // Compute CRC over unescaped data
                    uint16_t crc = 0;
                    for (size_t j = 0; j < subidx; ++j) crc = _updcrc(subbuf[j], crc);
                    // include the end marker in CRC as sender does
                    crc = _updcrc(b, crc);

                    if (crc == receivedCrc) {
                        // Valid subpacket: write to file
                        if (subidx > 0) {
                            _file->write(subbuf, subidx);
                            _bytesTransferred += subidx;
                        }
                        // ACK the chunk
                        _sendHexHeader(ZACK, ZERO_FLAGS);
                        _lastActivity = millis();
                        // reset subbuffer
                        subidx = 0;
                    } else {
                        // CRC mismatch: request resend from current offset
                        uint8_t pos[4];
                        pos[0] = _bytesTransferred & 0xFF;
                        pos[1] = (_bytesTransferred >> 8) & 0xFF;
                        pos[2] = (_bytesTransferred >> 16) & 0xFF;
                        pos[3] = (_bytesTransferred >> 24) & 0xFF;
                        _sendHexHeader(ZRPOS, pos);
                        _lastActivity = millis();
                        // on CRC error we discard subbuf and continue waiting for resend
                        subidx = 0;
                    }
                    // remove consumed bytes from _inBuf by shifting remaining
                    if (idx < _inBufLen) {
                        size_t rem = _inBufLen - idx;
                        memmove(_inBuf, _inBuf + idx, rem);
                        _inBufLen = rem;
                    } else {
                        _inBufLen = 0;
                    }
                    idx = 0; // restart parsing from buffer start
                    continue;
                } else {
                    // Normal escaped data byte
                    uint8_t orig = b ^ 0x40;
                    if (subidx < SUBBUF_SZ) subbuf[subidx++] = orig;
                    else { _state = STATE_ERROR; return; }
                }
            }
        }
        // remove any fully consumed bytes at front
        if (idx > 0 && idx <= _inBufLen) {
            if (idx < _inBufLen) memmove(_inBuf, _inBuf + idx, _inBufLen - idx);
            _inBufLen -= idx;
        }
    }
    
    // Keepalive (If waiting for sender to act)
    if (millis() - lastAck > 3000 && _state != STATE_COMPLETE && _state != STATE_ERROR) {
        _sendHexHeader(ZRINIT, ZERO_FLAGS); // Keep poking sender
        lastAck = millis();
    }
}


// --- Low Level Protocol (Unchanged, simplified implementation) ---

void ZModemEngine::_sendHexHeader(uint8_t type, const uint8_t* flags) {
    uint16_t crc = 0;
    
    _io->write(ZPAD); _io->write(ZPAD);
    _io->write(ZDLE); _io->write(ZHEX);
    
    char hexBuf[12];
    sprintf(hexBuf, "%02X%02X%02X%02X%02X", type, flags[0], flags[1], flags[2], flags[3]);
    
    crc = _updcrc(type, crc);
    crc = _updcrc(flags[0], crc);
    crc = _updcrc(flags[1], crc);
    crc = _updcrc(flags[2], crc);
    crc = _updcrc(flags[3], crc);
    
    _io->print(hexBuf);
    
    sprintf(hexBuf, "%02X%02X", (crc >> 8) & 0xFF, crc & 0xFF);
    _io->print(hexBuf);
    
    _io->write('\r'); _io->write('\n');
    if (type != ZFIN && type != ZACK) _io->write(0x11); // XON
}

void ZModemEngine::_sendBinaryHeader(uint8_t type, const uint8_t* flags) {
    uint16_t crc = 0;
    _io->write(ZPAD); _io->write(ZDLE); _io->write(ZBIN);
    
    _io->write(type); crc = _updcrc(type, crc);
    _io->write(flags[0]); crc = _updcrc(flags[0], crc);
    _io->write(flags[1]); crc = _updcrc(flags[1], crc);
    _io->write(flags[2]); crc = _updcrc(flags[2], crc);
    _io->write(flags[3]); crc = _updcrc(flags[3], crc);
    
    _io->write((crc >> 8) & 0xFF);
    _io->write(crc & 0xFF);
}

void ZModemEngine::_sendDataSubpacket(const uint8_t* data, size_t len, bool endFrame) {
    uint16_t crc = 0;
    for(size_t i=0; i<len; i++) {
        // Simple escaping for ZDLE
        // This is highly simplified and avoids complex 7E/9E escaping needed in traditional ZModem
        // It only checks for ZDLE and common control codes
        if (data[i] == ZDLE || data[i] == 0x10 || data[i] == 0x11 || data[i] == 0x13 || data[i] == 0x0d || data[i] == 0x8d) {
             _io->write(ZDLE);
             uint8_t esc = data[i] ^ 0x40;
             _io->write(esc);
             crc = _updcrc(data[i], crc); // CRC calculated on ORIGINAL byte
        } else {
             _io->write(data[i]);
             crc = _updcrc(data[i], crc);
        }
    }
    
    _io->write(ZDLE);
    _io->write(endFrame ? ZCRCE : ZCRCG); 
    crc = _updcrc(endFrame ? ZCRCE : ZCRCG, crc);
    
    _io->write((crc >> 8) & 0xFF);
    _io->write(crc & 0xFF);
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

// Simplified Header Reader
int ZModemEngine::_readHexHeader(uint8_t& type, uint8_t* flags) {
    // Non-destructive header parsing using internal buffer. We fill from
    // _io when available and parse only from _inBuf; bytes are removed from
    // the buffer only when a complete header is consumed.

    _fillInput();

    // Minimum expected header length: 4 control bytes + 2(type) + 8(flags) + 4(crc) + 2(CRLF) = 20
    const size_t MIN_HDR_LEN = 20;
    if (_inBufLen < MIN_HDR_LEN) return 0;

    // Look for header start at buffer start
    if (!(_inBuf[0] == ZPAD && _inBuf[1] == ZPAD && _inBuf[2] == ZDLE && _inBuf[3] == ZHEX)) return 0;

    // Ensure we have all ascii hex characters following
    // ascii hex digits start at index 4 and span 16 bytes (2 + 8 + 4 + 2)
    if (_inBufLen < 4 + 16) return 0;

    auto hexToByte = [](uint8_t hi, uint8_t lo) -> uint8_t {
        auto val = [](uint8_t c)->int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        };
        int h = val(hi); int l = val(lo);
        if (h < 0 || l < 0) return 0;
        return (uint8_t)((h << 4) | l);
    };

    // Parse type
    type = hexToByte(_inBuf[4], _inBuf[5]);
    // Parse 4 flags (each two hex chars)
    for (int i = 0; i < 4; ++i) {
        size_t idx = 6 + i * 2;
        flags[i] = hexToByte(_inBuf[idx], _inBuf[idx+1]);
    }

    // We consumed 4 control bytes + 16 ascii hex chars = 20 bytes
    size_t consumed = 4 + 16;
    // Remove consumed bytes from buffer
    if (consumed < _inBufLen) memmove(_inBuf, _inBuf + consumed, _inBufLen - consumed);
    _inBufLen -= consumed;
    return 1;
}

void ZModemEngine::_fillInput() {
    if (!_io) return;
    int avail = _io->available();
    if (avail <= 0) return;
    size_t space = sizeof(_inBuf) - _inBufLen;
    if (space == 0) return;
    int toRead = avail < (int)space ? avail : (int)space;
    for (int i = 0; i < toRead; ++i) {
        int c = _io->read();
        if (c == -1) break;
        _inBuf[_inBufLen++] = (uint8_t)c;
    }
}
