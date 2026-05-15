#pragma once

#include "Arduino.h"

class File {
public:
    File() = default;
    explicit File(std::vector<uint8_t> data) : _data(std::move(data)) {}

    std::size_t read(uint8_t* buffer, std::size_t length)
    {
        if (_position >= _data.size()) {
            return 0;
        }

        const std::size_t remaining = _data.size() - _position;
        const std::size_t toRead = std::min(length, remaining);
        std::memcpy(buffer, _data.data() + _position, toRead);
        _position += toRead;
        return toRead;
    }

    int available() const
    {
        return (_position < _data.size()) ? static_cast<int>(_data.size() - _position) : 0;
    }

    bool seek(std::size_t position)
    {
        if (position > _data.size()) {
            return false;
        }
        _position = position;
        return true;
    }

    std::size_t write(const uint8_t* buffer, std::size_t length)
    {
        if (_position + length > _data.size()) {
            _data.resize(_position + length);
        }
        std::memcpy(_data.data() + _position, buffer, length);
        _position += length;
        return length;
    }

    std::size_t write(uint8_t value)
    {
        return write(&value, 1);
    }

    std::size_t size() const { return _data.size(); }
    void close() { _position = 0; }
    bool isDirectory() const { return false; }
    explicit operator bool() const { return true; }

    const std::vector<uint8_t>& data() const { return _data; }

private:
    std::vector<uint8_t> _data;
    std::size_t _position = 0;
};