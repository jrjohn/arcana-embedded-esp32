#include "CommandFactory.hpp"

#include "commands/PingCommand.hpp"
#include "commands/GetSensorDataCommand.hpp"
#include "commands/GetDeviceInfoCommand.hpp"
#include "commands/SetNotifyIntervalCommand.hpp"
#include "commands/GetBleStatusCommand.hpp"
#include "commands/SetDeviceNameCommand.hpp"
#include "commands/BleScanCommand.hpp"
#include "commands/GetMqttStatusCommand.hpp"
#include "commands/KeyExchangeCommand.hpp"

namespace Arcana {
namespace Command {

std::unique_ptr<ICommand> CommandFactory::Create(Cluster cluster, uint8_t command) {
    switch (cluster) {
    case Cluster::System:
        switch (command) {
        case SystemCmd::Ping:
            return std::make_unique<PingCommand>();
        case SystemCmd::GetDeviceInfo:
            return std::make_unique<GetDeviceInfoCommand>();
        }
        break;

    case Cluster::Sensor:
        switch (command) {
        case SensorCmd::GetData:
            return std::make_unique<GetSensorDataCommand>(mDeps.Sensor);
        case SensorCmd::SetNotifyInterval:
            return std::make_unique<SetNotifyIntervalCommand>(mDeps.Sensor);
        }
        break;

    case Cluster::Ble:
        switch (command) {
        case BleCmd::GetStatus:
            return std::make_unique<GetBleStatusCommand>();
        case BleCmd::SetDeviceName:
            return std::make_unique<SetDeviceNameCommand>();
        case BleCmd::Scan:
            return std::make_unique<BleScanCommand>();
        }
        break;

    case Cluster::Mqtt:
        switch (command) {
        case MqttCmd::GetStatus: {
            auto cmd = std::make_unique<GetMqttStatusCommand>();
            mMqttStatusCmd = cmd.get();
            return cmd;
        }
        }
        break;

    case Cluster::Security:
        switch (command) {
        case SecurityCmd::KeyExchange:
            return std::make_unique<KeyExchangeCommand>(mDeps.KeyExchangeMgr);
        }
        break;

    default:
        break;
    }

    return nullptr;
}

} // namespace Command
} // namespace Arcana
