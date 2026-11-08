#pragma once

// Shared LittleFS diagnostic logger. Every call flushes before returning so
// the last event survives an unexpected ESP32 restart.
void writeDiagnosticLog(const char* event, const char* details = "");
