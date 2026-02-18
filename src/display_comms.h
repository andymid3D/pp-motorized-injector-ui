#ifndef DISPLAY_COMMS_H
#define DISPLAY_COMMS_H

#include <Arduino.h>
#include <stdint.h>

namespace DisplayComms {

struct MouldParams {
    char name[32];
    float fillVolume;
    float fillSpeed;
    float fillPressure;
    float packVolume;
    float packSpeed;
    float packPressure;
    float packTime;
    float coolingTime;
    float fillAccel;
    float fillDecel;
    float packAccel;
    float packDecel;
    char mode[3]; // "2D" or "3D"
    float injectTorque;
};

struct CommonParams {
    float trapAccel;
    float compressTorque;
    uint32_t microIntervalMs;
    uint32_t microDurationMs;
    float purgeUp;
    float purgeDown;
    float purgeCurrent;
    float antidripVel;
    float antidripCurrent;
    float releaseDist;
    float releaseTrapVel;
    float releaseCurrent;
    uint32_t contactorCycles;
    uint32_t contactorLimit;
};

struct Status {
    float encoderTurns;
    float tempC;
    char state[24];
    uint16_t errorCode;
    char errorMsg[64];
};

void begin(HardwareSerial &serial, int rxPin, int txPin, uint32_t baud = 115200);
void update();
void applyUiUpdates();

// Query helpers (call from UI actions)
void sendQueryMould();
void sendQueryCommon();
void sendQueryState();
void sendQueryError();
bool sendMould(const MouldParams &params);
bool sendCommon(const CommonParams &params);

const Status &getStatus();
const MouldParams &getMould();
const CommonParams &getCommon();
bool isSafeForUpdate();

} // namespace DisplayComms

#endif // DISPLAY_COMMS_H
