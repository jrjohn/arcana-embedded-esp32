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

    CommandFactory& mFactory;
    EventQueue<CommandRequest, 10> mAsyncQueue;
    Observable<CommandResponse> mResponseEvents;
};

} // namespace Command
} // namespace Arcana
