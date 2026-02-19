#include "storage.h"
#include <Arduino.h>
#include <LittleFS.h>

namespace Storage {

static const char *MOULDS_FILE = "/moulds.bin";
static bool _initialized = false;

bool init() {
  if (_initialized)
    return true;
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS Mount Failed");
    return false;
  }
  _initialized = true;
  Serial.println("LittleFS Mounted");
  return true;
}

void loadMoulds(DisplayComms::MouldParams *moulds, int &count, int maxCount) {
  count = 0;
  if (!_initialized) {
    Serial.println("Storage not initialized, skipping load.");
    return;
  }
  if (!LittleFS.exists(MOULDS_FILE)) {
    Serial.println("No moulds file found, starting fresh.");
    return;
  }

  File file = LittleFS.open(MOULDS_FILE, FILE_READ);
  if (!file) {
    Serial.println("Failed to open moulds file for reading");
    return;
  }

  // Read count first
  if (file.read((uint8_t *)&count, sizeof(count)) != sizeof(count)) {
    Serial.println("Failed to read mould count");
    count = 0;
    file.close();
    return;
  }

  if (count > maxCount) {
    Serial.printf("Warning: File has %d moulds, but max is %d. Truncating.\n",
                  count, maxCount);
    count = maxCount;
  }

  // Read profiles
  for (int i = 0; i < count; i++) {
    if (file.read((uint8_t *)&moulds[i], sizeof(DisplayComms::MouldParams)) !=
        sizeof(DisplayComms::MouldParams)) {
      Serial.printf("Failed to read mould %d\n", i);
      count = i; // stop at last successful read
      break;
    }
  }

  file.close();
  Serial.printf("Loaded %d mould profiles.\n", count);
}

void saveMoulds(const DisplayComms::MouldParams *moulds, int count) {
  if (!_initialized) {
    Serial.println("Storage not initialized, skipping save.");
    // Attempt auto-init?
    if (!init())
      return;
  }

  File file = LittleFS.open(MOULDS_FILE, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open moulds file for writing");
    return;
  }

  // Write count
  if (file.write((const uint8_t *)&count, sizeof(count)) != sizeof(count)) {
    Serial.println("Failed to write mould count");
    file.close();
    return;
  }

  // Write profiles
  for (int i = 0; i < count; i++) {
    if (file.write((const uint8_t *)&moulds[i],
                   sizeof(DisplayComms::MouldParams)) !=
        sizeof(DisplayComms::MouldParams)) {
      Serial.printf("Failed to write mould %d\n", i);
      break;
    }
  }

  file.close();
  Serial.printf("Saved %d mould profiles.\n", count);
}

} // namespace Storage
