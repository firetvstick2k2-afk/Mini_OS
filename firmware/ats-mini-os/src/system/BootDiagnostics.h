#pragma once
#include <Arduino.h>

namespace BootDiagnostics {
void begin(bool enabled = true);
void setEnabled(bool enabled);
bool enabled();
void write(const char* message);
void printf(const char* format, ...);
}
