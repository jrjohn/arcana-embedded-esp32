#include "CommandDispatcher.hpp"
#include "esp_log.h"

static const char* TAG = "CmdDispatcher";

namespace Arcana {
namespace Command {

CommandDispatcher::CommandDispatcher(CommandFactory& factory)
    : mFactory(factory)
#if CONFIG_IDF_TARGET_ESP32S3
    // ESP32-S3 (ample RAM): deliver responses on a dedicated task so a slow
    // BLE/MQTT transmit never blocks command intake/execution — a new command
    // can arrive and be answered while a prior response is still going out, and
    // back-to-back replies are serialized through the queue rather than
    // interrupting each other. ~4.3 KB queue + 4 KB stack.
    // Classic ESP32 omits this (default-constructed → synchronous send) to save
    // that RAM; that's its constrained variant.
    , mResponseEvents("cmdrsp", kRspQueueDepth, 4096, 5)
#endif
{
}

CommandDispatcher::~CommandDispatcher() {
    Stop();
}

esp_err_t CommandDispatcher::Init() {
    return ESP_OK;
}

esp_err_t CommandDispatcher::Start() {
    bool ok = mAsyncQueue.Start(
        [this](const CommandRequest& req) {
            ProcessCommand(req);
        },
        4096,  // stack size
        5      // priority
    );
    return ok ? ESP_OK : ESP_FAIL;
}

void CommandDispatcher::Stop() {
    mAsyncQueue.Stop();
}

void CommandDispatcher::Dispatch(const CommandRequest& request) {
    // Create command to check if it's async
    auto cmd = mFactory.Create(request.ClusterId, request.Command);
    if (!cmd) {
        ESP_LOGW(TAG, "Unknown command: cluster=0x%02x cmd=0x%02x",
                 static_cast<uint8_t>(request.ClusterId), request.Command);
        CommandResponse rsp;
        rsp.Source = request.Source;
        rsp.ConnectionId = request.ConnectionId;
        rsp.ClusterId = request.ClusterId;
        rsp.Command = request.Command;
        rsp.Status = kStatusUnknownCommand;
        rsp.PayloadLen = 0;
        rsp.StreamId = request.StreamId;
        rsp.Fin = true;
        mResponseEvents.Notify(rsp);
        return;
    }

    if (cmd->IsAsync()) {
        // Post to queue for async execution
        if (!mAsyncQueue.Post(request)) {
            ESP_LOGW(TAG, "Async queue full, dropping cluster=0x%02x cmd=0x%02x",
                     static_cast<uint8_t>(request.ClusterId), request.Command);
        }
    } else {
        // Execute synchronously in caller's context
        ProcessCommand(request);
    }
}

void CommandDispatcher::ProcessCommand(const CommandRequest& request) {
    auto cmd = mFactory.Create(request.ClusterId, request.Command);
    // LCOV_EXCL_START — IEC 62304 §5.5.3 defensive guard. ProcessCommand
    // is called either (a) from Dispatch which already validates the
    // factory result, or (b) via the async queue which receives only
    // requests that Dispatch has already validated. The null-cmd return
    // is defensive in case the factory becomes non-deterministic between
    // the validation and the dispatch.
    if (!cmd) {
        return;
    }
    // LCOV_EXCL_STOP

    ESP_LOGI(TAG, "Executing cluster=0x%02x cmd=0x%02x (source=%d)",
             static_cast<uint8_t>(request.ClusterId), request.Command,
             static_cast<uint8_t>(request.Source));

    CommandResponse rsp = cmd->Execute(request);
    rsp.StreamId = request.StreamId;
    mResponseEvents.Notify(rsp);
}

} // namespace Command
} // namespace Arcana
