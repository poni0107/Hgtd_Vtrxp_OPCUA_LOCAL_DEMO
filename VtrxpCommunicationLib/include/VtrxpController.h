#ifndef VTRXP_COMMUNICATION_LIB_VTRXP_CONTROLLER_H
#define VTRXP_COMMUNICATION_LIB_VTRXP_CONTROLLER_H

#include "VtrxpBackend.h"

#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace VtrxpCommunicationLib
{

class VtrxpController
{
public:
    VtrxpController();
    ~VtrxpController() noexcept;

    VtrxpController(const VtrxpController&) = delete;
    VtrxpController& operator=(const VtrxpController&) = delete;

    bool setupVtrxp(
        const std::string& hostUrl,
        int port,
        std::uint64_t felixId,
        std::uint64_t lpgbtId,
        std::uint32_t i2cMasterId,
        std::uint32_t i2cSlaveId
    );

    bool startCommunication();
    void stopCommunication() noexcept;

    RegisterOperationResult registerRead(std::uint32_t registerAddress);
    RegisterOperationResult registerWrite(
        std::uint32_t registerAddress,
        std::uint8_t value
    );
    RegisterOperationResult lastValidRead() const;
    RegisterOperationResult lastOperation() const;
    CommunicationState communicationState() const;

    bool setMockOptions(const MockBackendOptions& options);
    std::string lastLifecycleError() const;

private:
    struct WorkerState
    {
        mutable std::mutex mutex;
        bool running { false };
        CommunicationState communicationState {
            CommunicationState::Disconnected
        };
        std::string error;
    };

    mutable std::mutex m_lifecycleMutex;
    std::shared_ptr<IVtrxpBackend> m_backend;
    std::shared_ptr<WorkerState> m_workerState;
    std::thread m_vtrxpClientThread;
    BackendConfiguration m_configuration {};
    bool m_configured { false };

    mutable std::mutex m_operationResultMutex;
    RegisterOperationResult m_lastOperation {};
};

}

#endif
