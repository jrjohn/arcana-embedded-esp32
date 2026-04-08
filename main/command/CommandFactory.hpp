#pragma once

#include "ICommand.hpp"
#include "ObservableSensor.hpp"
#include <memory>

namespace Arcana {
namespace Command {

class GetMqttStatusCommand;
class KeyExchangeManager;

class CommandFactory {
public:
    struct Dependencies {
        Sensor::ObservableSensor* Sensor = nullptr;
        KeyExchangeManager* KeyExchangeMgr = nullptr;
    };

    explicit CommandFactory(const Dependencies& deps) : mDeps(deps) {}

    std::unique_ptr<ICommand> Create(Cluster cluster, uint8_t command);

    // Access to stateful commands for external updates
    GetMqttStatusCommand* MqttStatusCmd() { return mMqttStatusCmd; }

private:
    Dependencies mDeps;
    GetMqttStatusCommand* mMqttStatusCmd = nullptr;
};

} // namespace Command
} // namespace Arcana
