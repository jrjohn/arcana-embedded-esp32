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

private:
    void ProcessCommand(const CommandRequest& request);

    CommandFactory& mFactory;
    EventQueue<CommandRequest, 10> mAsyncQueue;
    Observable<CommandResponse> mResponseEvents;
};

} // namespace Command
} // namespace Arcana
