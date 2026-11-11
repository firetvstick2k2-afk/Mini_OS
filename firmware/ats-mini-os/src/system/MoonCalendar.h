#pragma once

#include <Arduino.h>
#include <time.h>

class MoonCalendar {
public:
  struct Snapshot {
    float ageDays = 0.0f;
    uint8_t illuminationPercent = 0;
    float phase = 0.0f;
    char phaseName[24] = "Keine Zeit";
    time_t nextNewMoon = 0;
    time_t nextFullMoon = 0;
    bool valid = false;
  };

  static Snapshot calculate(time_t now);
};
