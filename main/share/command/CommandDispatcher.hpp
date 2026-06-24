#pragma once

#include "CommandTypes.hpp"
#include "CommandFactory.hpp"
#include "Observable.hpp"
#include "esp_err.h"

namespace Arcana {
namespace Command {

class CommandDispatcher {
public:
    explicit CommandDispatcher(CommandFactory& factory);
    ~CommandDispatcher();

    CommandDispatcher(const CommandDispatcher&) = delete;
    CommandDispatcher& operator=(const CommandDispatcher&) = delete;

    esp_err_t Init();
    esp_err_t Start();
    void Stop();

    void Dispatch(const CommandRequest& request);

    Observable<CommandResponse>& ResponseEvents() { return mResponseEvents; }

    // Test-only accessors. The async queue and ProcessCommand are normally
    // private because callers use Dispatch() (which routes sync vs async),
    // but tests need to drive the async path manually since the host
    // FreeRTOS stub doesn't actually spawn a task.
    EventQueue<CommandRequest, 10>& test_AsyncQueue() { return mAsyncQueue; }
    void test_ProcessCommand(const CommandRequest& req) { ProcessCommand(req); }

private:
    void ProcessCommand(const CommandRequest& request);

    // ESP32-S3 response send-queue depth. Responses are 1:1 with commands, so
    // this is >= the inbound async-command queue (10) to buffer a full burst of
    // replies instead of dropping while the BLE/MQTT sender drains.
    static constexpr size_t kRspQueueDepth = 16;

    CommandFactory& mFactory;
    EventQueue<CommandRequest, 10> mAsyncQueue;
    // On ESP32-S3 this is a task-backed (async) Observable so a slow BLE/MQTT
    // send never blocks command intake/execution; on the classic ESP32 it stays
    // default-constructed (synchronous) to save the queue + task RAM. See the
    // CommandDispatcher constructor.
    Observable<CommandResponse> mResponseEvents;
};

} // namespace Command
} // namespace Arcana
