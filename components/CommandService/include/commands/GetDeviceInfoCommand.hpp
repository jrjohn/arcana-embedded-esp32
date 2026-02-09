#pragma once

#include "ICommand.hpp"
#include "esp_system.h"
#include "esp_mac.h"
#include <cstring>
#include <cstdio>

namespace Arcana {
namespace Command {

class GetDeviceInfoCommand : public ICommand {
public:
    CommandResponse Execute(const CommandRequest& request) override {
        CommandResponse rsp;
        rsp.Source = request.Source;
        rsp.ConnectionId = request.ConnectionId;
        rsp.Function = FuncCode::GetDeviceInfo;
        rsp.Status = kStatusOk;

        // Pack: [idf_version_len:1][idf_version:N][mac:6][free_heap:4]
        const char* idfVer = esp_get_idf_version();
        uint8_t verLen = static_cast<uint8_t>(strlen(idfVer));

        uint16_t offset = 0;
        rsp.Payload[offset++] = verLen;
        memcpy(rsp.Payload + offset, idfVer, verLen);
        offset += verLen;

        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_BT);
        memcpy(rsp.Payload + offset, mac, 6);
        offset += 6;

        uint32_t freeHeap = esp_get_free_heap_size();
        memcpy(rsp.Payload + offset, &freeHeap, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        rsp.PayloadLen = offset;
        return rsp;
    }
};

} // namespace Command
} // namespace Arcana
