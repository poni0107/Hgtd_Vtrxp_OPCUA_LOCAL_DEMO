#ifndef VTRXP_COMMUNICATION_LIB_MOCK_VTRXP_COMMUNICATOR_H
#define VTRXP_COMMUNICATION_LIB_MOCK_VTRXP_COMMUNICATOR_H

#include "VtrxpBackend.h"

#include <array>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace VtrxpCommunicationLib
{

// MOCK/SIMULATION ONLY. No CERN, TDAQ, FELIX or Networking headers are used.
class MockVtrxpCommunicator final : public IVtrxpBackend
{
public:
    MockVtrxpCommunicator();
    explicit MockVtrxpCommunicator(const MockBackendOptions& options);
    ~MockVtrxpCommunicator() override;

    bool configure(
        const BackendConfiguration& configuration,
        std::string& error
    ) override;

    void run() override;
    void requestStop() noexcept override;
    bool connected() override;

    RegisterOperationResult readRegister(
        std::uint32_t registerAddress
    ) override;

    RegisterOperationResult writeRegister(
        std::uint32_t registerAddress,
        std::uint8_t value
    ) override;

    RegisterOperationResult lastValidRead() const override;
    bool setMockOptions(const MockBackendOptions& options) override;

private:
    RegisterOperationResult failure(
        RegisterOperation operation,
        std::uint32_t registerAddress,
        std::uint8_t value,
        const std::string& message
    );

    mutable std::mutex m_mutex;
    std::condition_variable m_stopCondition;
    std::array<std::uint8_t, 256> m_registers {};
    BackendConfiguration m_configuration {};
    MockBackendOptions m_options {};
    RegisterOperationResult m_lastValidRead {};
    std::uint64_t m_generation { 0 };
    bool m_configured { false };
    bool m_connected { false };
    bool m_stopRequested { false };
    bool m_runActive { false };
};

}

#endif
