#pragma once
// Host stand-in for the ESP32Servo library. hal/Motion.cpp keeps its own
// commanded angle; the host reads that through Motion::currentOffsetDeg().
class Servo {
 public:
  void setPeriodHertz(int) {}
  bool attach(int, int, int) { return true; }
  void write(int angle) { lastAngle = angle; }
  int lastAngle = 90;
};
