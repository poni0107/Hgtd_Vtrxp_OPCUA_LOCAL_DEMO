#ifndef VTRXP_COMMUNICATION_LIB_VTRXP_COMMUNICATOR_H
#define VTRXP_COMMUNICATION_LIB_VTRXP_COMMUNICATOR_H

#include "VtrxpBackend.h"

#include "ConnectedComponents/HGTD_MsgTypes.h"
#include "ConnectedComponents/SingleConnectionBase.h"
#include "Networking/Client.h"

#include <LogIt.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>

namespace VtrxpCommunicationLib
{

// REAL CERN backend. This translation unit intentionally retains its
// hgtd-felix-sw/TDAQ/Networking dependencies.
class VtrxpCommunicator final :
    public hgtd::SingleConnectionBase,
    public IVtrxpBackend
{
public:
    VtrxpCommunicator();
    ~VtrxpCommunicator() override;

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

    // Retained as small REAL-backend setup primitives.
    void connectSerialCommunicator(const std::string& hostUrl, int port);
    void setLpgbtInfo(std::uint64_t felixId, std::uint64_t lpgbtId);
    void setI2cInfo(
        std::uint32_t i2cMasterId,
        std::uint32_t i2cSlaveId
    );

    void onMessage(
        hgtd::OwnedDataMessage<hgtd::DataMsgType> message
    ) override;

private:
    struct PendingOperation
    {
        bool active { false };
        bool completed { false };
        RegisterOperation operation { RegisterOperation::None };
        std::uint32_t registerAddress { 0 };
        std::uint8_t requestedValue { 0 };
        std::uint64_t generation { 0 };
        RegisterOperationResult result {};
    };

    void sendReadRequest(std::uint32_t registerAddress);
    void sendWriteRequest(
        std::uint32_t registerAddress,
        std::uint8_t value
    );

    RegisterOperationResult executeRead(std::uint32_t registerAddress);
    RegisterOperationResult executeWrite(
        std::uint32_t registerAddress,
        std::uint8_t value
    );
    RegisterOperationResult immediateFailure(
        RegisterOperation operation,
        std::uint32_t registerAddress,
        std::uint8_t value,
        const std::string& message
    );
    void failPending(const std::string& message);

    std::string m_hostUrl {};
    int m_port { 0 };
    std::uint64_t m_felixId { 0 };
    std::uint64_t m_lpgbtId { 0 };
    std::uint32_t m_i2cMasterId { 0 };
    std::uint32_t m_i2cSlaveId { 0 };

    std::mutex m_operationMutex;
    mutable std::mutex m_pendingMutex;
    std::condition_variable m_pendingCondition;
    PendingOperation m_pending {};
    std::uint64_t m_generation { 0 };

    mutable std::mutex m_lastReadMutex;
    RegisterOperationResult m_lastValidRead {};
    std::chrono::milliseconds m_responseTimeout { 1000 };
};

}

#endif
