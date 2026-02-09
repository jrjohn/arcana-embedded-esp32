#include "CommandDispatcher.hpp"
#include "esp_log.h"

static const char* TAG = "CmdDispatcher";

namespace Arcana {
namespace Command {

CommandDispatcher::CommandDispatcher(CommandFactory& factory)
    : mFactory(factory)
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
    auto cmd = mFactory.Create(request.Function);
    if (!cmd) {
        ESP_LOGW(TAG, "Unknown command: 0x%02x", static_cast<uint8_t>(request.Function));
        CommandResponse rsp;
        rsp.Source = request.Source;
        rsp.ConnectionId = request.ConnectionId;
        rsp.Function = request.Function;
        rsp.Status = kStatusUnknownCommand;
        rsp.PayloadLen = 0;
        mResponseEvents.Notify(rsp);
        return;
    }

    if (cmd->IsAsync()) {
        // Post to queue for async execution
        if (!mAsyncQueue.Post(request)) {
            ESP_LOGW(TAG, "Async queue full, dropping command 0x%02x",
                     static_cast<uint8_t>(request.Function));
        }
    } else {
        // Execute synchronously in caller's context
        ProcessCommand(request);
    }
}

void CommandDispatcher::ProcessCommand(const CommandRequest& request) {
    auto cmd = mFactory.Create(request.Function);
    if (!cmd) {
        return;
    }

    ESP_LOGI(TAG, "Executing command 0x%02x (source=%d)",
             static_cast<uint8_t>(request.Function),
             static_cast<uint8_t>(request.Source));

    CommandResponse rsp = cmd->Execute(request);
    mResponseEvents.Notify(rsp);
}

} // namespace Command
} // namespace Arcana
