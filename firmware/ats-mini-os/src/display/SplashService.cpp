#include "SplashService.h"

#include <LittleFS.h>
#include <FS.h>
#include <string.h>

namespace SplashService {
namespace {
uint32_t readBigEndian32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) |
         (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) |
         static_cast<uint32_t>(p[3]);
}
}

bool validate(const char* path, uint16_t expectedWidth, uint16_t expectedHeight) {
  if (!path || !expectedWidth || !expectedHeight) return false;
  File file = LittleFS.open(path, "r");
  if (!file || file.isDirectory()) {
    if (file) file.close();
    return false;
  }
  const size_t size = file.size();
  uint8_t header[24] = {};
  const size_t read = file.read(header, sizeof(header));
  file.close();
  static const uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  if (size < sizeof(header) || size > MAX_FILE_SIZE || read != sizeof(header) ||
      memcmp(header, signature, sizeof(signature)) != 0 ||
      memcmp(header + 12, "IHDR", 4) != 0) return false;
  return readBigEndian32(header + 16) == expectedWidth &&
         readBigEndian32(header + 20) == expectedHeight;
}

bool install() {
  constexpr const char* backup = "/splash.png.bak";
  if (!LittleFS.exists(TEMP_PATH)) return false;
  if (LittleFS.exists(backup)) return false;
  const bool hadCurrent = LittleFS.exists(IMAGE_PATH);
  if (hadCurrent && !LittleFS.rename(IMAGE_PATH, backup)) return false;
  if (!LittleFS.rename(TEMP_PATH, IMAGE_PATH)) {
    if (hadCurrent) LittleFS.rename(backup, IMAGE_PATH);
    return false;
  }
  if (hadCurrent) LittleFS.remove(backup);
  return true;
}

void discardTemporary() {
  if (LittleFS.exists(TEMP_PATH)) LittleFS.remove(TEMP_PATH);
}

bool exists() {
  return LittleFS.exists(IMAGE_PATH);
}
}  // namespace SplashService
