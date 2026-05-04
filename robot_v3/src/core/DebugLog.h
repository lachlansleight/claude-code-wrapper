#pragma once

#include <Arduino.h>

#define LOG_INFO(fmt, ...)  Serial.printf("[INFO ] " fmt "\n", ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  Serial.printf("[WARN ] " fmt "\n", ##__VA_ARGS__)
#define LOG_ERR(fmt, ...)   Serial.printf("[ERROR] " fmt "\n", ##__VA_ARGS__)
#define LOG_WS(fmt, ...)    Serial.printf("[ws   ] " fmt "\n", ##__VA_ARGS__)
#define LOG_EVT(fmt, ...)   Serial.printf("[evt  ] " fmt "\n", ##__VA_ARGS__)
