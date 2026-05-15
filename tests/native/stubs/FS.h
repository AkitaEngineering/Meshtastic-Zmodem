#pragma once

#include "Arduino.h"

#include <memory>
#include <unordered_map>

#ifndef FILE_READ
#define FILE_READ "r"
#endif

#ifndef FILE_WRITE
#define FILE_WRITE "w"
#endif

class File {
public:
    File() : _data(std::make_shared<std::vector<uint8_t>>()) {}
    explicit File(std::vector<uint8_t> data) : _data(std::make_shared<std::vector<uint8_t>>(std::move(data))) {}
    File(std::shared_ptr<std::vector<uint8_t>> data, bool writable, bool open = true)
        : _data(std::move(data)), _writable(writable), _open(open) {}

    static File invalid() { return File(nullptr, false, false); }

    std::size_t read(uint8_t* buffer, std::size_t length)
    {
        if (!_open || !_data || _position >= _data->size()) {
            return 0;
        }

        const std::size_t remaining = _data->size() - _position;
        const std::size_t toRead = std::min(length, remaining);
        std::memcpy(buffer, _data->data() + _position, toRead);
        _position += toRead;
        return toRead;
    }

    int available() const
    {
        return (_open && _data && _position < _data->size()) ? static_cast<int>(_data->size() - _position) : 0;
    }

    bool seek(std::size_t position)
    {
        if (!_open || !_data || position > _data->size()) {
            return false;
        }
        _position = position;
        return true;
    }

    std::size_t write(const uint8_t* buffer, std::size_t length)
    {
        if (!_open || !_data || !_writable) {
            return 0;
        }

        if (_position + length > _data->size()) {
            _data->resize(_position + length);
        }
        std::memcpy(_data->data() + _position, buffer, length);
        _position += length;
        return length;
    }

    std::size_t write(uint8_t value)
    {
        return write(&value, 1);
    }

    std::size_t size() const { return (_data) ? _data->size() : 0; }
    void close() { _position = 0; _open = false; }
    bool isDirectory() const { return false; }
    explicit operator bool() const { return _open && static_cast<bool>(_data); }

    const std::vector<uint8_t>& data() const
    {
        static const std::vector<uint8_t> empty;
        return _data ? *_data : empty;
    }

private:
    std::shared_ptr<std::vector<uint8_t>> _data;
    std::size_t _position = 0;
    bool _writable = true;
    bool _open = true;
};

class FS {
public:
    File open(const String& path, const char* mode)
    {
        return open(path.c_str(), mode);
    }

    File open(const char* path, const char* mode)
    {
        const std::string key = path ? path : "";
        const bool writeMode = mode && mode[0] == 'w';
        if (writeMode) {
            auto storage = std::make_shared<std::vector<uint8_t>>();
            _files[key] = storage;
            return File(storage, true);
        }

        auto it = _files.find(key);
        if (it == _files.end()) {
            return File::invalid();
        }
        return File(it->second, false);
    }

    void setFileData(const String& path, const std::vector<uint8_t>& data)
    {
        setFileData(path.c_str(), data);
    }

    void setFileData(const char* path, const std::vector<uint8_t>& data)
    {
        _files[path ? path : ""] = std::make_shared<std::vector<uint8_t>>(data);
    }

    std::vector<uint8_t> getFileData(const char* path) const
    {
        auto it = _files.find(path ? path : "");
        if (it == _files.end() || !it->second) {
            return {};
        }
        return *it->second;
    }

private:
    std::unordered_map<std::string, std::shared_ptr<std::vector<uint8_t>>> _files;
};
