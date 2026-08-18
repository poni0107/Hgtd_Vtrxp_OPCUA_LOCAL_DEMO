#include "VtrxpController.h"

#if defined(VTRXP_USE_MOCK_BACKEND) && VTRXP_USE_MOCK_BACKEND
#include "MockVtrxpCommunicator.h"
#else
#include "VtrxpCommunicator.h"
#endif

#include <exception>
#include <utility>

namespace VtrxpCommunicationLib
{

VtrxpController::VtrxpController():
#if defined(VTRXP_USE_MOCK_BACKEND) && VTRXP_USE_MOCK_BACKEND
    m_backend(std::make_shared<MockVtrxpCommunicator>()),
#else
    m_backend(std::make_shared<VtrxpCommunicator>()),
#endif
    m_workerState(std::make_shared<WorkerState>())
{
}

VtrxpController::~VtrxpController() noexcept
{
    stopCommunication();
}

bool VtrxpController::setupVtrxp(
    const std::string& hostUrl,
    int port,
    std::uint64_t felixId,
    std::uint64_t lpgbtId,
    std::uint32_t i2cMasterId,
    std::uint32_t i2cSlaveId
)
{
    const BackendConfiguration requested {
        hostUrl,
        port,
        felixId,
        lpgbtId,
        i2cMasterId,
        i2cSlaveId
    };

    std::lock_guard<std::mutex> lifecycleLock(m_lifecycleMutex);

    // Repeated setup with identical values is idempotent while the worker exists.
    if (m_vtrxpClientThread.joinable())
    {
        if (m_configured && requested == m_configuration)
            return true;

        std::lock_guard<std::mutex> stateLock(m_workerState->mutex);
        m_workerState->error =
            "Cannot reconfigure VTRx+ while the communication thread is active";
        return false;
    }

    {
        std::lock_guard<std::mutex> stateLock(m_workerState->mutex);
        m_workerState->communicationState = CommunicationState::Connecting;
        m_workerState->error.clear();
    }

    std::string error;
    try
    {
        if (!m_backend->configure(requested, error))
        {
            std::lock_guard<std::mutex> stateLock(m_workerState->mutex);
            m_workerState->error = error;
            m_workerState->communicationState = CommunicationState::Error;
            m_configured = false;
            return false;
        }
    }
    catch (const std::exception& exception)
    {
        std::lock_guard<std::mutex> stateLock(m_workerState->mutex);
        m_workerState->error = exception.what();
        m_workerState->communicationState = CommunicationState::Error;
        m_configured = false;
        return false;
    }
    catch (...)
    {
        std::lock_guard<std::mutex> stateLock(m_workerState->mutex);
        m_workerState->error = "Unknown exception while configuring VTRx+ backend";
        m_workerState->communicationState = CommunicationState::Error;
        m_configured = false;
        return false;
    }

    m_configuration = requested;
    m_configured = true;
    {
        std::lock_guard<std::mutex> stateLock(m_workerState->mutex);
        m_workerState->error.clear();
        m_workerState->communicationState = CommunicationState::Connecting;
    }
    return true;
}

bool VtrxpController::startCommunication()
{
    std::lock_guard<std::mutex> lifecycleLock(m_lifecycleMutex);

    if (m_vtrxpClientThread.joinable())
    {
        std::lock_guard<std::mutex> stateLock(m_workerState->mutex);
        if (m_workerState->running)
            return true;

        m_workerState->error =
            "VTRx+ worker exited; call stop before attempting a restart";
        return false;
    }

    if (!m_configured)
    {
        std::lock_guard<std::mutex> stateLock(m_workerState->mutex);
        m_workerState->error = "VTRx+ backend must be configured before start";
        m_workerState->communicationState = CommunicationState::Error;
        return false;
    }

    const auto backend = m_backend;
    const auto state = m_workerState;
    try
    {
        {
            std::lock_guard<std::mutex> stateLock(state->mutex);
            state->running = true;
            state->error.clear();
            state->communicationState = CommunicationState::Running;
        }
        m_vtrxpClientThread = std::thread([backend, state]()
        {
            try
            {
                backend->run();
            }
            catch (const std::exception& exception)
            {
                std::lock_guard<std::mutex> stateLock(state->mutex);
                state->error = exception.what();
            }
            catch (...)
            {
                std::lock_guard<std::mutex> stateLock(state->mutex);
                state->error = "Unknown exception in VTRx+ communication thread";
            }

            std::lock_guard<std::mutex> stateLock(state->mutex);
            state->running = false;
            if (state->communicationState != CommunicationState::Stopped)
            {
                state->communicationState = CommunicationState::Error;
                if (state->error.empty())
                    state->error = "VTRx+ communication worker exited unexpectedly";
            }
        });
    }
    catch (const std::exception& exception)
    {
        std::lock_guard<std::mutex> stateLock(m_workerState->mutex);
        m_workerState->running = false;
        m_workerState->error = exception.what();
        m_workerState->communicationState = CommunicationState::Error;
        return false;
    }

    return true;
}

void VtrxpController::stopCommunication() noexcept
{
    try
    {
        std::lock_guard<std::mutex> lifecycleLock(m_lifecycleMutex);
        {
            std::lock_guard<std::mutex> stateLock(m_workerState->mutex);
            m_workerState->communicationState = CommunicationState::Stopped;
        }
        if (m_backend)
            m_backend->requestStop();

        if (m_vtrxpClientThread.joinable())
        {
            if (m_vtrxpClientThread.get_id() == std::this_thread::get_id())
                m_vtrxpClientThread.detach();
            else
                m_vtrxpClientThread.join();
        }

        m_configured = false;
        std::lock_guard<std::mutex> stateLock(m_workerState->mutex);
        m_workerState->running = false;
        m_workerState->communicationState = CommunicationState::Stopped;
    }
    catch (...)
    {
        // std::thread destruction must never observe a joinable worker.
        try
        {
            if (m_vtrxpClientThread.joinable())
                m_vtrxpClientThread.detach();
        }
        catch (...)
        {
        }
    }
}

RegisterOperationResult VtrxpController::registerRead(
    std::uint32_t registerAddress
)
{
    const RegisterOperationResult result =
        m_backend->readRegister(registerAddress);
    std::lock_guard<std::mutex> resultLock(m_operationResultMutex);
    m_lastOperation = result;
    return result;
}

RegisterOperationResult VtrxpController::registerWrite(
    std::uint32_t registerAddress,
    std::uint8_t value
)
{
    const RegisterOperationResult result =
        m_backend->writeRegister(registerAddress, value);
    std::lock_guard<std::mutex> resultLock(m_operationResultMutex);
    m_lastOperation = result;
    return result;
}

RegisterOperationResult VtrxpController::lastValidRead() const
{
    return m_backend->lastValidRead();
}

RegisterOperationResult VtrxpController::lastOperation() const
{
    std::lock_guard<std::mutex> resultLock(m_operationResultMutex);
    return m_lastOperation;
}

CommunicationState VtrxpController::communicationState() const
{
    std::lock_guard<std::mutex> stateLock(m_workerState->mutex);
    return m_workerState->communicationState;
}

bool VtrxpController::setMockOptions(const MockBackendOptions& options)
{
    return m_backend->setMockOptions(options);
}

std::string VtrxpController::lastLifecycleError() const
{
    std::lock_guard<std::mutex> stateLock(m_workerState->mutex);
    return m_workerState->error;
}

}
