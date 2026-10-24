#pragma once
// Minimal PNG writer: 8-bit RGB, uncompressed (stored) deflate blocks. No
// dependencies, so the CLI builds anywhere a C++17 compiler does.
#include <stdint.h>

// rgba: width*height*4 bytes; alpha is dropped. Returns false on I/O error.
bool writePngRgb(const char* path, const uint8_t* rgba, int width, int height);
