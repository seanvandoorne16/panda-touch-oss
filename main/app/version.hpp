#pragma once

#define FIRMWARE_VERSION        "1.0.0"

// GitHub Releases URL for the compiled firmware binary.
// After building: create a Release on GitHub and attach panda_touch_oss.bin.
// The device downloads this URL when the user taps "Bijwerken".
#define FIRMWARE_OTA_URL \
    "https://github.com/seanvandoorne16/panda-touch-oss/releases/latest/download/panda_touch_oss.bin"
