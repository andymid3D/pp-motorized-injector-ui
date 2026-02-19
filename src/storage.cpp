#include "storage.h"

#include <LittleFS.h>

namespace Storage {

static const char *MOULDS_FILE = "/moulds.bin";
static const char *MOULDS_TMP_FILE = "/moulds.tmp";
static constexpr size_t DUMP_MAX_BYTES = 4096;

#ifndef STORAGE_FORMAT_FS_ON_BOOT
#define STORAGE_FORMAT_FS_ON_BOOT 0
#endif

static void dumpMouldsFileHex(const char *path) {
    File f = LittleFS.open(path, FILE_READ);
    if (!f) {
        Serial.printf("Storage::dump %s: open failed\n", path);
        return;
    }

    const size_t total = static_cast<size_t>(f.size());
    const size_t limit = total > DUMP_MAX_BYTES ? DUMP_MAX_BYTES : total;
    Serial.printf("Storage::dump %s: size=%u bytes (dump=%u)\n",
                  path,
                  static_cast<unsigned>(total),
                  static_cast<unsigned>(limit));

    uint8_t buf[16];
    size_t offset = 0;
    while (offset < limit) {
        const size_t want = ((limit - offset) >= sizeof(buf)) ? sizeof(buf) : (limit - offset);
        const size_t got = f.read(buf, want);
        if (got == 0) {
            break;
        }

        Serial.printf("  %04u: ", static_cast<unsigned>(offset));
        for (size_t i = 0; i < got; ++i) {
            Serial.printf("%02X ", buf[i]);
        }
        for (size_t i = got; i < sizeof(buf); ++i) {
            Serial.print("   ");
        }
        Serial.print(" | ");
        for (size_t i = 0; i < got; ++i) {
            const char c = static_cast<char>(buf[i]);
            Serial.print((c >= 32 && c <= 126) ? c : '.');
        }
        Serial.println();
        offset += got;
    }

    if (limit < total) {
        Serial.println("  ... dump truncated ...");
    }

    f.close();
}

bool init() {
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS Mount Failed");
        return false;
    }
    Serial.println("LittleFS Mounted");
    return true;
}

void loadMoulds(DisplayComms::MouldParams *moulds, int &count, int maxCount) {
    count = 0;
    if (!LittleFS.exists(MOULDS_FILE)) {
        Serial.println("No moulds file found, starting fresh.");
        return;
    }

    dumpMouldsFileHex(MOULDS_FILE);

    File file = LittleFS.open(MOULDS_FILE, FILE_READ);
    if (!file) {
        Serial.println("Failed to open moulds file for reading");
        return;
    }

    const size_t fileSize = static_cast<size_t>(file.size());
    int storedCount = 0;

    if (file.read((uint8_t *)&storedCount, sizeof(storedCount)) != sizeof(storedCount)) {
        Serial.println("Failed to read mould count");
        file.close();
        return;
    }

    if (storedCount < 0) {
        Serial.printf("Corrupt mould count (%d), resetting to 0\n", storedCount);
        file.close();
        return;
    }

    const size_t headerSize = sizeof(storedCount);
    const size_t recordSize = sizeof(DisplayComms::MouldParams);
    size_t maxRecordsBySize = 0;
    if (fileSize > headerSize) {
        maxRecordsBySize = (fileSize - headerSize) / recordSize;
    }

    int safeCount = storedCount;
    if (safeCount > maxCount) {
        Serial.printf("Warning: File has %d moulds, max is %d. Truncating.\n", safeCount, maxCount);
        safeCount = maxCount;
    }
    if (safeCount > static_cast<int>(maxRecordsBySize)) {
        Serial.printf("Corrupt file: header count=%d, available records=%u. Truncating.\n",
                      safeCount, static_cast<unsigned>(maxRecordsBySize));
        safeCount = static_cast<int>(maxRecordsBySize);
    }

    for (int i = 0; i < safeCount; i++) {
        if (file.read((uint8_t *)&moulds[i], recordSize) != recordSize) {
            Serial.printf("Failed to read mould %d\n", i);
            safeCount = i;
            break;
        }

        moulds[i].name[sizeof(moulds[i].name) - 1] = 0;
        moulds[i].mode[sizeof(moulds[i].mode) - 1] = 0;

        for (size_t k = 0; k < sizeof(moulds[i].name) - 1; ++k) {
            char c = moulds[i].name[k];
            if (c == 0) {
                break;
            }
            if (c < 32 || c > 126) {
                moulds[i].name[k] = '_';
            }
        }

        for (size_t k = 0; k < sizeof(moulds[i].mode) - 1; ++k) {
            char c = moulds[i].mode[k];
            if (c == 0) {
                break;
            }
            if (c < 32 || c > 126) {
                moulds[i].mode[k] = '_';
            }
        }
    }

    file.close();
    count = safeCount;
    Serial.printf("Loaded %d mould profiles.\n", count);
}

void saveMoulds(const DisplayComms::MouldParams *moulds, int count) {
    if (count < 0) {
        count = 0;
    }

    Serial.printf("Storage::saveMoulds begin count=%d\n", count);

    if (LittleFS.exists(MOULDS_TMP_FILE)) {
        LittleFS.remove(MOULDS_TMP_FILE);
    }

    File file = LittleFS.open(MOULDS_TMP_FILE, FILE_WRITE);
    if (!file) {
        Serial.println("Failed to open temporary mould file for writing");
        return;
    }

    const size_t headerSize = sizeof(count);
    const size_t recordSize = sizeof(DisplayComms::MouldParams);

    if (file.write((const uint8_t *)&count, headerSize) != headerSize) {
        Serial.println("Failed to write mould count to temporary file");
        file.close();
        LittleFS.remove(MOULDS_TMP_FILE);
        return;
    }

    for (int i = 0; i < count; i++) {
        if (file.write((const uint8_t *)&moulds[i], recordSize) != recordSize) {
            Serial.printf("Failed to write mould %d to temporary file\n", i);
            file.close();
            LittleFS.remove(MOULDS_TMP_FILE);
            return;
        }
        if ((i & 1) == 1) {
            delay(0);
        }
    }

    file.flush();
    file.close();

    const size_t expectedSize = headerSize + (static_cast<size_t>(count) * recordSize);
    File check = LittleFS.open(MOULDS_TMP_FILE, FILE_READ);
    if (!check) {
        Serial.println("Failed to reopen temporary mould file for validation");
        LittleFS.remove(MOULDS_TMP_FILE);
        return;
    }
    const size_t actualSize = static_cast<size_t>(check.size());
    check.close();

    if (actualSize != expectedSize) {
        Serial.printf("Temporary mould file size mismatch (%u/%u), aborting commit\n",
                      static_cast<unsigned>(actualSize),
                      static_cast<unsigned>(expectedSize));
        LittleFS.remove(MOULDS_TMP_FILE);
        return;
    }

    if (LittleFS.exists(MOULDS_FILE)) {
        if (!LittleFS.remove(MOULDS_FILE)) {
            Serial.println("Failed to remove old moulds file");
            LittleFS.remove(MOULDS_TMP_FILE);
            return;
        }
    }

    if (!LittleFS.rename(MOULDS_TMP_FILE, MOULDS_FILE)) {
        Serial.println("Failed to commit temporary moulds file");
        LittleFS.remove(MOULDS_TMP_FILE);
        return;
    }

    Serial.printf("Saved %d mould profiles (atomic).\n", count);
    Serial.printf("Storage::saveMoulds end count=%d\n", count);
}

} // namespace Storage
