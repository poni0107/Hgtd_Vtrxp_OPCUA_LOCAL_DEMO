#ifndef VTRXP_COMMUNICATION_LIB_VTRXP_BACKEND_H
#define VTRXP_COMMUNICATION_LIB_VTRXP_BACKEND_H

#include <chrono>
#include <cstdint>
#include <string>

namespace VtrxpCommunicationLib
{

enum class RegisterOperation : std::uint8_t
{
    None,
    Read,
    Write
};

enum class CommunicationState : std::uint8_t
{
    Disconnected,
    Connecting,
    Running,
    Stopped,
    Error
};

inline const char* communicationStateName(CommunicationState state)
{
    switch (state)
    {
        case CommunicationState::Disconnected: return "Disconnected";
        case CommunicationState::Connecting: return "Connecting";
        case CommunicationState::Running: return "Running";
        case CommunicationState::Stopped: return "Stopped";
        case CommunicationState::Error: return "Error";
    }
    return "Error";
}

inline const char* registerOperationName(RegisterOperation operation)
{
    switch (operation)
    {
        case RegisterOperation::None: return "None";
        case RegisterOperation::Read: return "Read";
        case RegisterOperation::Write: return "Write";
    }
    return "None";
}

struct RegisterOperationResult
{
    RegisterOperation operation { RegisterOperation::None };
    std::uint32_t registerAddress { 0 };
    std::uint8_t value { 0 };
    bool valid { false };
    bool success { false };
    std::string message { "No register operation has completed" };
    std::uint64_t generation { 0 };
};

struct BackendConfiguration
{
    std::string hostUrl;
    int port { 0 };
    std::uint64_t felixId { 0 };
    std::uint64_t lpgbtId { 0 };
    std::uint32_t i2cMasterId { 0 };
    std::uint32_t i2cSlaveId { 0 };
};

struct MockBackendOptions
{
    bool connectionAvailable { true };
    std::chrono::milliseconds responseDelay { 0 };
};

class IVtrxpBackend
{
public:
    virtual ~IVtrxpBackend() = default;

    virtual bool configure(
        const BackendConfiguration& configuration,
        std::string& error
    ) = 0;

    // run() owns the backend's blocking communication/event loop.
    virtual void run() = 0;
    virtual void requestStop() noexcept = 0;
    virtual bool connected() = 0;

    virtual RegisterOperationResult readRegister(
        std::uint32_t registerAddress
    ) = 0;

    virtual RegisterOperationResult writeRegister(
        std::uint32_t registerAddress,
        std::uint8_t value
    ) = 0;

    virtual RegisterOperationResult lastValidRead() const = 0;

    // Only MOCK backends accept these options. REAL returns false.
    virtual bool setMockOptions(const MockBackendOptions&)
    {
        return false;
    }
};

inline bool operator==(
    const BackendConfiguration& lhs,
    const BackendConfiguration& rhs
)
{
    return lhs.hostUrl == rhs.hostUrl
        && lhs.port == rhs.port
        && lhs.felixId == rhs.felixId
        && lhs.lpgbtId == rhs.lpgbtId
        && lhs.i2cMasterId == rhs.i2cMasterId
        && lhs.i2cSlaveId == rhs.i2cSlaveId;
}

inline bool operator!=(
    const BackendConfiguration& lhs,
    const BackendConfiguration& rhs
)
{
    return !(lhs == rhs);
}

}

#endif
