#pragma once
#include <Arduino.h>

namespace BootDiagnostics {
void begin();
void write(const char* message);
void printf(const char* format, ...);
}
