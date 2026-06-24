#pragma once

#include "CommandBridgeService.hpp"
#include "CommandService.hpp"

namespace Arcana {

class CommandBridgeServiceImpl : public CommandBridgeService {
public:
    static CommandBridgeService& getInstance();

    esp_err_t init_HAL() override;
    esp_err_t init() override;
    esp_err_t start() override;
    void stop() override;
    void SetCommandKey(const uint8_t key[32]) override { mCodec.SetKey(key); }

private:
    CommandBridgeServiceImpl() = default;
    ~CommandBridgeServiceImpl() override = default;
    CommandBridgeServiceImpl(const CommandBridgeServiceImpl&) = delete;
    CommandBridgeServiceImpl& operator=(const CommandBridgeServiceImpl&) = delete;

    Command::CommandCodec mCodec;
};

} // namespace Arcana
