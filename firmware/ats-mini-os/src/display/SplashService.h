#pragma once

#include <Arduino.h>

namespace SplashService {
constexpr const char* IMAGE_PATH = "/splash.png";
constexpr const char* TEMP_PATH = "/splash.png.tmp";
constexpr size_t MAX_FILE_SIZE = 512U * 1024U;

// Validate PNG signature, IHDR dimensions and file size without touching the
// currently installed splash image.
bool validate(const char* path, uint16_t expectedWidth, uint16_t expectedHeight);
bool install();
void discardTemporary();
bool exists();
}  // namespace SplashService
