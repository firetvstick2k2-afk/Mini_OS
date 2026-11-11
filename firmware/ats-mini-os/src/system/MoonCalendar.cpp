#include "MoonCalendar.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {
constexpr double kSynodicMonthDays = 29.530588853;
constexpr double kPi = 3.14159265358979323846;
constexpr double kKnownNewMoonUtc = 947182440.0; // 2000-01-06 18:14 UTC
constexpr time_t kValidEpochFloor = 1700000000;

const char* phaseName(double phase) {
  if (phase < 0.03 || phase >= 0.97) return "Neumond";
  if (phase < 0.22) return "Zunehmende Sichel";
  if (phase < 0.28) return "Erstes Viertel";
  if (phase < 0.47) return "Zunehmender Mond";
  if (phase < 0.53) return "Vollmond";
  if (phase < 0.72) return "Abnehmender Mond";
  if (phase < 0.78) return "Letztes Viertel";
  return "Abnehmende Sichel";
}
}

MoonCalendar::Snapshot MoonCalendar::calculate(time_t now) {
  Snapshot result;
  if (now < kValidEpochFloor) return result;

  const double elapsedDays =
    (static_cast<double>(now) - kKnownNewMoonUtc) / 86400.0;
  const double lunation = floor(elapsedDays / kSynodicMonthDays);
  double age = elapsedDays - lunation * kSynodicMonthDays;
  if (age < 0.0) age += kSynodicMonthDays;

  result.ageDays = static_cast<float>(age);
  result.phase = static_cast<float>(age / kSynodicMonthDays);
  const double illumination = (1.0 - cos(2.0 * kPi * result.phase)) * 50.0;
  result.illuminationPercent = static_cast<uint8_t>(
    constrain(static_cast<int>(lround(illumination)), 0, 100));
  strlcpy(result.phaseName, phaseName(result.phase), sizeof(result.phaseName));

  const double nextNew = kKnownNewMoonUtc +
    (lunation + 1.0) * kSynodicMonthDays * 86400.0;
  const double fullCycle = lunation + (result.phase < 0.5f ? 0.5 : 1.5);
  const double nextFull = kKnownNewMoonUtc +
    fullCycle * kSynodicMonthDays * 86400.0;
  result.nextNewMoon = static_cast<time_t>(llround(nextNew / 60.0) * 60);
  result.nextFullMoon = static_cast<time_t>(llround(nextFull / 60.0) * 60);
  result.valid = true;
  return result;
}
