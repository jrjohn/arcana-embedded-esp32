#include "MqttCommandAdapter.hpp"
#include "cJSON.h"
#include "esp_log.h"
#include <cstring>
#include <cstdio>

static const char* TAG = "MqttCmdAdapter";

namespace Arcana {
namespace Command {

struct FuncCodeMapping {
    FuncCode Code;
    const char* Name;
};

static const FuncCodeMapping sFuncCodeMap[] = {
    { FuncCode::Ping,              "ping" },
    { FuncCode::GetSensorData,     "get_sensor_data" },
    { FuncCode::GetDeviceInfo,     "get_device_info" },
    { FuncCode::SetNotifyInterval, "set_notify_interval" },
    { FuncCode::GetBleStatus,      "get_ble_status" },
    { FuncCode::SetDeviceName,     "set_device_name" },
    { FuncCode::BleScan,           "ble_scan" },
    { FuncCode::GetMqttStatus,     "get_mqtt_status" },
};

static constexpr size_t kFuncCodeMapSize = sizeof(sFuncCodeMap) / sizeof(sFuncCodeMap[0]);

const char* MqttCommandAdapter::FuncCodeToString(FuncCode code) {
    for (size_t i = 0; i < kFuncCodeMapSize; ++i) {
        if (sFuncCodeMap[i].Code == code) {
            return sFuncCodeMap[i].Name;
        }
    }
    return "unknown";
}

FuncCode MqttCommandAdapter::StringToFuncCode(const char* str) {
    for (size_t i = 0; i < kFuncCodeMapSize; ++i) {
        if (strcmp(sFuncCodeMap[i].Name, str) == 0) {
            return sFuncCodeMap[i].Code;
        }
    }
    return static_cast<FuncCode>(0xFF);
}

bool MqttCommandAdapter::ParseRequest(const char* data, size_t len, CommandRequest& out) {
    if (!data || len == 0) {
        return false;
    }

    cJSON* root = cJSON_ParseWithLength(data, len);
    if (!root) {
        ESP_LOGW(TAG, "JSON parse failed");
        return false;
    }

    bool ok = false;
    do {
        cJSON* cmdItem = cJSON_GetObjectItem(root, "cmd");
        if (!cJSON_IsString(cmdItem)) {
            ESP_LOGW(TAG, "Missing 'cmd' field");
            break;
        }

        FuncCode code = StringToFuncCode(cmdItem->valuestring);
        if (static_cast<uint8_t>(code) == 0xFF) {
            ESP_LOGW(TAG, "Unknown command: %s", cmdItem->valuestring);
            break;
        }

        out.Source = CommandSource::MQTT;
        out.ConnectionId = 0;
        out.Function = code;
        out.PayloadLen = 0;

        // Parse params object — extract specific known params into payload
        cJSON* params = cJSON_GetObjectItem(root, "params");
        if (params && cJSON_IsObject(params)) {
            // For set_notify_interval: {"interval_ms": 2000}
            cJSON* intervalItem = cJSON_GetObjectItem(params, "interval_ms");
            if (intervalItem && cJSON_IsNumber(intervalItem)) {
                uint32_t val = static_cast<uint32_t>(intervalItem->valuedouble);
                memcpy(out.Payload, &val, sizeof(uint32_t));
                out.PayloadLen = sizeof(uint32_t);
            }

            // For ble_scan: {"duration_sec": 10}
            cJSON* durationItem = cJSON_GetObjectItem(params, "duration_sec");
            if (durationItem && cJSON_IsNumber(durationItem)) {
                uint32_t val = static_cast<uint32_t>(durationItem->valuedouble);
                memcpy(out.Payload, &val, sizeof(uint32_t));
                out.PayloadLen = sizeof(uint32_t);
            }

            // For set_device_name: {"name": "MY-DEVICE"}
            cJSON* nameItem = cJSON_GetObjectItem(params, "name");
            if (nameItem && cJSON_IsString(nameItem)) {
                size_t nameLen = strlen(nameItem->valuestring);
                if (nameLen > 0 && nameLen <= kMaxRequestPayload) {
                    memcpy(out.Payload, nameItem->valuestring, nameLen);
                    out.PayloadLen = static_cast<uint16_t>(nameLen);
                }
            }
        }

        ok = true;
    } while (false);

    cJSON_Delete(root);
    return ok;
}

bool MqttCommandAdapter::SerializeResponse(const CommandResponse& rsp,
                                            char* buf, size_t bufSize, size_t& outLen) {
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return false;
    }

    cJSON_AddStringToObject(root, "cmd", FuncCodeToString(rsp.Function));
    cJSON_AddNumberToObject(root, "status", rsp.Status);

    // Serialize data based on command type
    cJSON* dataObj = cJSON_CreateObject();
    if (rsp.Status == kStatusOk && rsp.PayloadLen > 0) {
        switch (rsp.Function) {
        case FuncCode::Ping: {
            if (rsp.PayloadLen >= sizeof(int64_t)) {
                int64_t ts = 0;
                memcpy(&ts, rsp.Payload, sizeof(int64_t));
                cJSON_AddNumberToObject(dataObj, "timestamp_us", static_cast<double>(ts));
            }
            break;
        }
        case FuncCode::GetSensorData: {
            if (rsp.PayloadLen >= 12) {
                float temp = 0, humid = 0;
                uint32_t tsMs = 0;
                memcpy(&temp,  rsp.Payload,     sizeof(float));
                memcpy(&humid, rsp.Payload + 4,  sizeof(float));
                memcpy(&tsMs,  rsp.Payload + 8,  sizeof(uint32_t));
                cJSON_AddNumberToObject(dataObj, "temp", static_cast<double>(static_cast<int>(temp * 100)) / 100.0);
                cJSON_AddNumberToObject(dataObj, "humid", static_cast<double>(static_cast<int>(humid * 100)) / 100.0);
                cJSON_AddNumberToObject(dataObj, "timestamp_ms", tsMs);
            }
            break;
        }
        case FuncCode::GetDeviceInfo: {
            if (rsp.PayloadLen >= 1) {
                uint8_t verLen = rsp.Payload[0];
                uint16_t offset = 1;
                if (rsp.PayloadLen >= offset + verLen + 10) {
                    char verBuf[64] = {};
                    memcpy(verBuf, rsp.Payload + offset, verLen);
                    verBuf[verLen] = '\0';
                    offset += verLen;

                    char macStr[18];
                    snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
                             rsp.Payload[offset], rsp.Payload[offset+1],
                             rsp.Payload[offset+2], rsp.Payload[offset+3],
                             rsp.Payload[offset+4], rsp.Payload[offset+5]);
                    offset += 6;

                    uint32_t freeHeap = 0;
                    memcpy(&freeHeap, rsp.Payload + offset, sizeof(uint32_t));

                    cJSON_AddStringToObject(dataObj, "idf_version", verBuf);
                    cJSON_AddStringToObject(dataObj, "mac", macStr);
                    cJSON_AddNumberToObject(dataObj, "free_heap", freeHeap);
                }
            }
            break;
        }
        case FuncCode::SetNotifyInterval: {
            if (rsp.PayloadLen >= sizeof(uint32_t)) {
                uint32_t interval = 0;
                memcpy(&interval, rsp.Payload, sizeof(uint32_t));
                cJSON_AddNumberToObject(dataObj, "interval_ms", interval);
            }
            break;
        }
        case FuncCode::GetBleStatus: {
            if (rsp.PayloadLen >= 1) {
                cJSON_AddNumberToObject(dataObj, "connections", rsp.Payload[0]);
            }
            break;
        }
        case FuncCode::SetDeviceName: {
            char nameBuf[30] = {};
            memcpy(nameBuf, rsp.Payload, rsp.PayloadLen);
            nameBuf[rsp.PayloadLen] = '\0';
            cJSON_AddStringToObject(dataObj, "name", nameBuf);
            break;
        }
        case FuncCode::BleScan: {
            if (rsp.PayloadLen >= sizeof(uint32_t)) {
                uint32_t dur = 0;
                memcpy(&dur, rsp.Payload, sizeof(uint32_t));
                cJSON_AddNumberToObject(dataObj, "duration_sec", dur);
            }
            break;
        }
        case FuncCode::GetMqttStatus: {
            if (rsp.PayloadLen >= 1) {
                cJSON_AddBoolToObject(dataObj, "connected", rsp.Payload[0] != 0);
            }
            break;
        }
        default:
            break;
        }
    }
    cJSON_AddItemToObject(root, "data", dataObj);

    char* printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!printed) {
        return false;
    }

    size_t printedLen = strlen(printed);
    if (printedLen >= bufSize) {
        cJSON_free(printed);
        return false;
    }

    memcpy(buf, printed, printedLen);
    buf[printedLen] = '\0';
    outLen = printedLen;
    cJSON_free(printed);

    return true;
}

} // namespace Command
} // namespace Arcana
