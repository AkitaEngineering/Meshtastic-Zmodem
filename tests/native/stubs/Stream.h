#pragma once

#include "Arduino.h"

class Stream {
public:
    virtual ~Stream() = default;

    virtual int available() = 0;
    virtual int read() = 0;
    virtual int peek() = 0;
    virtual std::size_t write(uint8_t value) = 0;
    virtual void flush() {}

    virtual std::size_t write(const uint8_t* buffer, std::size_t length)
    {
        std::size_t written = 0;
        for (std::size_t index = 0; index < length; ++index) {
            written += write(buffer[index]);
        }
        return written;
    }

    std::size_t print(const char* text)
    {
        return write(reinterpret_cast<const uint8_t*>(text), std::strlen(text));
    }

    std::size_t print(unsigned long value)
    {
        const std::string text = std::to_string(value);
        return write(reinterpret_cast<const uint8_t*>(text.data()), text.size());
    }

    std::size_t print(int value)
    {
        const std::string text = std::to_string(value);
        return write(reinterpret_cast<const uint8_t*>(text.data()), text.size());
    }
};