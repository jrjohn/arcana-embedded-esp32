#pragma once

#include "CommandTypes.hpp"
#include "CommandFactory.hpp"
#include "CommandDispatcher.hpp"
#include "KeyExchangeManager.hpp"
#include "Observable.hpp"
#include "ObservableSensor.hpp"
#include "esp_err.h"

namespace Arcana {
namespace Command {

class CommandService {
public:
    static CommandService& Instance();

    esp_err_t Init(Sensor::ObservableSensor* sensor);
    esp_err_t Start();
    void Stop();

    void HandleRequest(const CommandRequest& request);

    Observable<CommandResponse>& ResponseEvents();

    // Access to factory for updating stateful command state
    CommandFactory* Factory() { return mFactory.get(); }

    // Access to key exchange manager (nullptr if encryption disabled)
    KeyExchangeManager* KeyExchangeMgr() { return mKeyExchangeMgr.get(); }

private:
    CommandService() = default;
    ~CommandService() = default;
    CommandService(const CommandService&) = delete;
    CommandService& operator=(const CommandService&) = delete;

    std::unique_ptr<CommandFactory> mFactory;
    std::unique_ptr<CommandDispatcher> mDispatcher;
    std::unique_ptr<KeyExchangeManager> mKeyExchangeMgr;
};

} // namespace Command
} // namespace Arcana
