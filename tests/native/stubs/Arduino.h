#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

class String {
public:
    String() = default;
    String(const char* value) : _value(value ? value : "") {}
    String(const std::string& value) : _value(value) {}

    const char* c_str() const { return _value.c_str(); }

private:
    std::string _value;
};

unsigned long millis();

template <typename T> T min(T left, T right)
{
    return std::min(left, right);
}