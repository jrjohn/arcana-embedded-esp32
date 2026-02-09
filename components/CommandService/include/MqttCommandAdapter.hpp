#pragma once

#include "CommandTypes.hpp"
#include <cstddef>

namespace Arcana {
namespace Command {

/**
 * MQTT JSON Protocol:
 *   Request:  {"cmd": "ping", "params": {...}}
 *   Response: {"cmd": "ping", "status": 0, "data": {...}}
 */
class MqttCommandAdapter {
public:
    static bool ParseRequest(const char* data, size_t len, CommandRequest& out);
    static bool SerializeResponse(const CommandResponse& rsp,
                                   char* buf, size_t bufSize, size_t& outLen);

private:
    static const char* FuncCodeToString(FuncCode code);
    static FuncCode StringToFuncCode(const char* str);
};

} // namespace Command
} // namespace Arcana
