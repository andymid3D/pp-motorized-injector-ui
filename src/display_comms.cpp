#include "display_comms.h"
#include "ui/ui.h"
#include "ui/screens.h"
#include "ui/vars.h"
#include "ui/structs.h"
#include "ui/eez-flow.h"
#include <strings.h>
#include <cstring>
#include <cctype>
#include <cstdio>

#ifndef DISPLAY_COMMS_DEBUG
#define DISPLAY_COMMS_DEBUG 1
#endif

#if DISPLAY_COMMS_DEBUG
#define COMMS_LOG(fmt, ...) do { Serial.printf("[DisplayComms] " fmt "\n", ##__VA_ARGS__); } while (0)
#else
#define COMMS_LOG(fmt, ...) do {} while (0)
#endif

namespace DisplayComms {

static HardwareSerial *uart = nullptr;
static char rxBuffer[256];
static size_t rxLen = 0;

static Status status = {};
static MouldParams mould = {};
static CommonParams common = {};

static float turnsToCm3(float turns) {
    // Keep aligned with controller's TURNS_PER_CM3_VOL
    static const float TURNS_PER_CM3 = 0.99925f;
    if (TURNS_PER_CM3 == 0.0f) return 0.0f;
    return turns / TURNS_PER_CM3;
}

static void uartSend(const char *msg) {
    if (!uart || !msg) return;
    uart->print(msg);
    uart->print('\n');
}

void begin(HardwareSerial &serial, int rxPin, int txPin, uint32_t baud) {
    uart = &serial;
    uart->begin(baud, SERIAL_8N1, rxPin, txPin);
    rxLen = 0;
    status.encoderTurns = 0.0f;
    status.tempC = 0.0f;
    status.state[0] = '\0';
    status.errorCode = 0;
    status.errorMsg[0] = '\0';

    COMMS_LOG("UART init RX=%d TX=%d baud=%lu", rxPin, txPin, static_cast<unsigned long>(baud));
}

static const char *nextToken(const char *str, char *out, size_t outLen, char delim) {
    if (!str || !out || outLen == 0) return nullptr;
    const char *p = strchr(str, delim);
    if (p) {
        size_t len = static_cast<size_t>(p - str);
        if (len >= outLen) len = outLen - 1;
        memcpy(out, str, len);
        out[len] = '\0';
        return p + 1;
    }
    strncpy(out, str, outLen - 1);
    out[outLen - 1] = '\0';
    return nullptr;
}

static void trimInPlace(char *text) {
    if (!text) return;

    size_t len = strlen(text);
    while (len > 0 && isspace(static_cast<unsigned char>(text[len - 1]))) {
        text[--len] = '\0';
    }

    size_t start = 0;
    while (text[start] != '\0' && isspace(static_cast<unsigned char>(text[start]))) {
        start++;
    }

    if (start > 0) {
        memmove(text, text + start, strlen(text + start) + 1);
    }
}

static void parseMessage(const char *msg) {
    char cmd[24];
    const char *rest = nextToken(msg, cmd, sizeof(cmd), '|');
    trimInPlace(cmd);

    if (strcasecmp(cmd, "ENC") == 0) {
        if (rest) {
            status.encoderTurns = static_cast<float>(atof(rest));
        }
        return;
    }

    if (strcasecmp(cmd, "TEMP") == 0) {
        if (rest) {
            status.tempC = static_cast<float>(atof(rest));
        }
        return;
    }

    if (strcasecmp(cmd, "STATE") == 0) {
        char field[32];
        if (rest) {
            rest = nextToken(rest, field, sizeof(field), '|');
            trimInPlace(field);
            strncpy(status.state, field, sizeof(status.state) - 1);
            status.state[sizeof(status.state) - 1] = '\0';
        }
        return;
    }

    if (strcasecmp(cmd, "ERROR") == 0) {
        char field[64];
        if (rest) {
            rest = nextToken(rest, field, sizeof(field), '|');
            trimInPlace(field);
            status.errorCode = static_cast<uint16_t>(strtoul(field, nullptr, 16));
            if (rest) {
                strncpy(status.errorMsg, rest, sizeof(status.errorMsg) - 1);
                status.errorMsg[sizeof(status.errorMsg) - 1] = '\0';
                trimInPlace(status.errorMsg);
            }
        }
        return;
    }

    if (strcasecmp(cmd, "MOULD_OK") == 0) {
        char field[64];
        int idx = 0;
        while (rest) {
            rest = nextToken(rest, field, sizeof(field), '|');
            trimInPlace(field);
            switch (idx) {
                case 0: strncpy(mould.name, field, sizeof(mould.name) - 1); mould.name[sizeof(mould.name) - 1] = '\0'; break;
                case 1: mould.fillVolume = atof(field); break;
                case 2: mould.fillSpeed = atof(field); break;
                case 3: mould.fillPressure = atof(field); break;
                case 4: mould.packVolume = atof(field); break;
                case 5: mould.packSpeed = atof(field); break;
                case 6: mould.packPressure = atof(field); break;
                case 7: mould.packTime = atof(field); break;
                case 8: mould.coolingTime = atof(field); break;
                case 9: mould.fillAccel = atof(field); break;
                case 10: mould.fillDecel = atof(field); break;
                case 11: mould.packAccel = atof(field); break;
                case 12: mould.packDecel = atof(field); break;
                case 13: strncpy(mould.mode, field, sizeof(mould.mode) - 1); mould.mode[sizeof(mould.mode) - 1] = '\0'; break;
                case 14: mould.injectTorque = atof(field); break;
                default: break;
            }
            idx++;
        }
        return;
    }

    if (strcasecmp(cmd, "COMMON_OK") == 0) {
        char field[64];
        int idx = 0;
        while (rest) {
            rest = nextToken(rest, field, sizeof(field), '|');
            trimInPlace(field);
            switch (idx) {
                case 0: common.trapAccel = atof(field); break;
                case 1: common.compressTorque = atof(field); break;
                case 2: common.microIntervalMs = static_cast<uint32_t>(atol(field)); break;
                case 3: common.microDurationMs = static_cast<uint32_t>(atol(field)); break;
                case 4: common.purgeUp = atof(field); break;
                case 5: common.purgeDown = atof(field); break;
                case 6: common.purgeCurrent = atof(field); break;
                case 7: common.antidripVel = atof(field); break;
                case 8: common.antidripCurrent = atof(field); break;
                case 9: common.releaseDist = atof(field); break;
                case 10: common.releaseTrapVel = atof(field); break;
                case 11: common.releaseCurrent = atof(field); break;
                case 12: common.contactorCycles = static_cast<uint32_t>(atol(field)); break;
                case 13: common.contactorLimit = static_cast<uint32_t>(atol(field)); break;
                default: break;
            }
            idx++;
        }
        return;
    }

    COMMS_LOG("Unknown message: %s", msg);
}

void update() {
    if (!uart) return;
    while (uart->available() > 0) {
        char c = static_cast<char>(uart->read());
        if (c == '\n' || c == '\r') {
            if (rxLen > 0) {
                rxBuffer[rxLen] = '\0';
                trimInPlace(rxBuffer);
                COMMS_LOG("RX: %s", rxBuffer);
                parseMessage(rxBuffer);
                rxLen = 0;
            }
        } else {
            if (rxLen < sizeof(rxBuffer) - 1) {
                rxBuffer[rxLen++] = c;
            } else {
                rxLen = 0;
            }
        }
    }
}

static void setLabelText(lv_obj_t *label, const char *text) {
    if (!label || !text) return;
    lv_label_set_text(label, text);
}

static void setLabelFloat(lv_obj_t *label, float value, const char *suffix = nullptr) {
    if (!label) return;
    char buf[32];
    if (suffix) {
        snprintf(buf, sizeof(buf), "%.2f%s", value, suffix);
    } else {
        snprintf(buf, sizeof(buf), "%.2f", value);
    }
    lv_label_set_text(label, buf);
}

void applyUiUpdates() {
    // Update global variables (used by EEZ flow)
    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PLUNGER_TIP_POSITION, FloatValue(turnsToCm3(status.encoderTurns)));

    plunger_stateValue plungerStateValue(eez::flow::getGlobalVariable(FLOW_GLOBAL_VARIABLE_PLUNGER_STATE));
    if (plungerStateValue) {
        plungerStateValue.temperature(status.tempC);
        plungerStateValue.current_barrel_capacity(turnsToCm3(status.encoderTurns));
    }

    const char *stateText = (status.state[0] != '\0') ? status.state : "--";
    setLabelText(objects.obj1__machine_state_text, stateText);
    setLabelText(objects.obj3__machine_state_text, stateText);
    setLabelText(objects.obj6__machine_state_text, stateText);

    // Mould values (Actual Mould Settings table)
    setLabelText(objects.obj4__mould_name_value, mould.name);
    setLabelFloat(objects.obj4__mould_fill_speed_value, mould.fillSpeed);
    setLabelFloat(objects.obj4__mould_fill_dist_value, mould.fillVolume);
    setLabelFloat(objects.obj4__mould_fill_accel_value, mould.fillAccel);
    setLabelFloat(objects.obj4__mould_hold_speed_value, mould.packSpeed);
    setLabelFloat(objects.obj4__mould_hold_dist_value, mould.packVolume);
    setLabelFloat(objects.obj4__mould_hold_accel_value, mould.packAccel);
}

void sendQueryMould() { uartSend("QUERY_MOULD"); }
void sendQueryCommon() { uartSend("QUERY_COMMON"); }
void sendQueryState() { uartSend("QUERY_STATE"); }
void sendQueryError() { uartSend("QUERY_ERROR"); }

bool sendMould(const MouldParams &params) {
    if (!isSafeForUpdate()) {
        return false;
    }

    char message[320];
    snprintf(
        message,
        sizeof(message),
        "MOULD|%s|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%s|%.3f",
        params.name,
        params.fillVolume,
        params.fillSpeed,
        params.fillPressure,
        params.packVolume,
        params.packSpeed,
        params.packPressure,
        params.packTime,
        params.coolingTime,
        params.fillAccel,
        params.fillDecel,
        params.packAccel,
        params.packDecel,
        params.mode,
        params.injectTorque
    );
    uartSend(message);
    COMMS_LOG("TX: %s", message);
    return true;
}

bool sendCommon(const CommonParams &params) {
    if (!isSafeForUpdate()) {
        return false;
    }

    char message[320];
    snprintf(
        message,
        sizeof(message),
        "COMMON|%.3f|%.3f|%lu|%lu|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%lu|%lu",
        params.trapAccel,
        params.compressTorque,
        static_cast<unsigned long>(params.microIntervalMs),
        static_cast<unsigned long>(params.microDurationMs),
        params.purgeUp,
        params.purgeDown,
        params.purgeCurrent,
        params.antidripVel,
        params.antidripCurrent,
        params.releaseDist,
        params.releaseTrapVel,
        params.releaseCurrent,
        static_cast<unsigned long>(params.contactorCycles),
        static_cast<unsigned long>(params.contactorLimit)
    );
    uartSend(message);
    COMMS_LOG("TX: %s", message);
    return true;
}

const Status &getStatus() { return status; }
const MouldParams &getMould() { return mould; }
const CommonParams &getCommon() { return common; }

static bool stateEquals(const char *a, const char *b) {
    if (!a || !b) return false;
    return strcasecmp(a, b) == 0;
}

bool isSafeForUpdate() {
    return stateEquals(status.state, "INIT_HEATING") ||
           stateEquals(status.state, "INIT_HOT_WAIT") ||
           stateEquals(status.state, "REFILL") ||
           stateEquals(status.state, "READY_TO_INJECT") ||
           stateEquals(status.state, "PURGE_ZERO") ||
           stateEquals(status.state, "CONFIRM_REMOVAL");
}

} // namespace DisplayComms
