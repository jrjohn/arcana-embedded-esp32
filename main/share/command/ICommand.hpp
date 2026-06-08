#pragma once

#include "CommandTypes.hpp"

namespace Arcana {
namespace Command {

class ICommand {
public:
    virtual ~ICommand() = default;
    virtual CommandResponse Execute(const CommandRequest& request) = 0;
    virtual bool IsAsync() const { return false; }
};

} // namespace Command
} // namespace Arcana
