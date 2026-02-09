#include "CommandFactory.hpp"

#include "commands/PingCommand.hpp"
#include "commands/GetSensorDataCommand.hpp"
#include "commands/GetDeviceInfoCommand.hpp"
#include "commands/SetNotifyIntervalCommand.hpp"
#include "commands/GetBleStatusCommand.hpp"
#include "commands/SetDeviceNameCommand.hpp"
#include "commands/BleScanCommand.hpp"
#include "commands/GetMqttStatusCommand.hpp"

namespace Arcana {
namespace Command {

std::unique_ptr<ICommand> CommandFactory::Create(FuncCode code) {
    switch (code) {
    case FuncCode::Ping:
        return std::make_unique<PingCommand>();

    case FuncCode::GetSensorData:
        return std::make_unique<GetSensorDataCommand>(mDeps.Sensor);

    case FuncCode::GetDeviceInfo:
        return std::make_unique<GetDeviceInfoCommand>();

    case FuncCode::SetNotifyInterval:
        return std::make_unique<SetNotifyIntervalCommand>(mDeps.Sensor);

    case FuncCode::GetBleStatus:
        return std::make_unique<GetBleStatusCommand>();

    case FuncCode::SetDeviceName:
        return std::make_unique<SetDeviceNameCommand>();

    case FuncCode::BleScan:
        return std::make_unique<BleScanCommand>();

    case FuncCode::GetMqttStatus: {
        auto cmd = std::make_unique<GetMqttStatusCommand>();
        mMqttStatusCmd = cmd.get();
        return cmd;
    }

    default:
        return nullptr;
    }
}

} // namespace Command
} // namespace Arcana
