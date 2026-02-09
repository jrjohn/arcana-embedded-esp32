#pragma once

#include <cstdint>
#include <cstring>
#include "esp_bt_defs.h"

namespace Arcana {
namespace Ble {

/*******************************************************************************
 * Enumerations
 ******************************************************************************/

enum class ConnectionState : uint8_t {
    Disconnected = 0,
    Connecting,
    Connected,
    Disconnecting
};

enum class BleRole : uint8_t {
    Server = 0,
    Client
};

/*******************************************************************************
 * Event Types
 ******************************************************************************/

struct BleConnectionEvent {
    BleRole Role;
    ConnectionState State;
    uint16_t ConnId;
    esp_bd_addr_t RemoteAddr;

    BleConnectionEvent()
        : Role(BleRole::Server)
        , State(ConnectionState::Disconnected)
        , ConnId(0) {
        memset(RemoteAddr, 0, sizeof(esp_bd_addr_t));
    }
};

struct BleSensorNotification {
    uint16_t CharUuid;
    esp_bd_addr_t RemoteAddr;
    uint8_t Data[20];
    uint16_t DataLen;

    BleSensorNotification()
        : CharUuid(0), DataLen(0) {
        memset(RemoteAddr, 0, sizeof(esp_bd_addr_t));
        memset(Data, 0, sizeof(Data));
    }
};

struct BleClientDiscovery {
    esp_bd_addr_t Addr;
    int8_t Rssi;
    char Name[32];

    BleClientDiscovery()
        : Rssi(0) {
        memset(Addr, 0, sizeof(esp_bd_addr_t));
        Name[0] = '\0';
    }
};

/*******************************************************************************
 * GATT Server Attribute Table Index
 ******************************************************************************/

enum SensorServiceAttr : uint16_t {
    ATTR_SVC = 0,

    ATTR_TEMP_CHAR,
    ATTR_TEMP_VAL,
    ATTR_TEMP_CCCD,

    ATTR_HUMID_CHAR,
    ATTR_HUMID_VAL,
    ATTR_HUMID_CCCD,

    ATTR_STATUS_CHAR,
    ATTR_STATUS_VAL,

    ATTR_CMD_CHAR,
    ATTR_CMD_VAL,
    ATTR_RSP_CHAR,
    ATTR_RSP_VAL,
    ATTR_RSP_CCCD,

    ATTR_COUNT
};

/*******************************************************************************
 * Client Connection Info
 ******************************************************************************/

static constexpr uint8_t kMaxServerConnections = 3;

struct ServerClientInfo {
    bool Connected;
    uint16_t ConnId;
    esp_bd_addr_t Addr;
    bool TempCccdEnabled;
    bool HumidCccdEnabled;
    bool RspCccdEnabled;

    ServerClientInfo()
        : Connected(false)
        , ConnId(0)
        , TempCccdEnabled(false)
        , HumidCccdEnabled(false)
        , RspCccdEnabled(false) {
        memset(Addr, 0, sizeof(esp_bd_addr_t));
    }
};

} // namespace Ble
} // namespace Arcana
