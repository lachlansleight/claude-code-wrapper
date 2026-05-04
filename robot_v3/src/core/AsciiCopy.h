#pragma once

#include <Arduino.h>

namespace AsciiCopy {

void copy(char* dst, size_t cap, const char* src);
void copyPreserveNewlines(char* dst, size_t cap, const char* src);
void basename(const char* path, char* out, size_t cap);

}  // namespace AsciiCopy
