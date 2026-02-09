#pragma once

#include "CommandTypes.hpp"
#include <cstdint>

namespace Arcana {
namespace Command {

/**
 * BLE Binary Protocol:
 *   Request:  [FuncCode:1][PayloadLen:2 LE][Payload:N]
 *   Response: [FuncCode:1][Status:1][PayloadLen:2 LE][Payload:N]
 */
class BleCommandAdapter {
public:
    static bool ParseRequest(uint16_t connId, const uint8_t* data,
                              uint16_t len, CommandRequest& out);

    static bool SerializeResponse(const CommandResponse& rsp,
                                   uint8_t* buf, uint16_t bufSize, uint16_t& outLen);
};

} // namespace Command
} // namespace Arcana
