#include "MockVtrxpCommunicator.h"

#include <chrono>
#include <thread>

namespace VtrxpCommunicationLib
{

MockVtrxpCommunicator::MockVtrxpCommunicator() = default;

MockVtrxpCommunicator::MockVtrxpCommunicator(
    const MockBackendOptions& options
):
    m_options(options)
{
}

MockVtrxpCommunicator::~MockVtrxpCommunicator()
{
    requestStop();
}

bool MockVtrxpCommunicator::configure(
    const BackendConfiguration& configuration,
    std::string& error
)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_configuration = configuration;
    m_configured = true;
    m_stopRequested = false;
    m_connected = m_options.connectionAvailable;

    if (!m_connected)
    {
        error = "MOCK connection is configured as unavailable";
        return false;
    }

    error.clear();
    return true;
}

void MockVtrxpCommunicator::run()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_runActive = true;
    m_stopCondition.wait(lock, [this]() { return m_stopRequested; });
    m_runActive = false;
    m_connected = false;
}

void MockVtrxpCommunicator::requestStop() noexcept
{
    try
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stopRequested = true;
            m_connected = false;
        }
        m_stopCondition.notify_all();
    }
    catch (...)
    {
        // A shutdown path must never propagate an exception.
    }
}

bool MockVtrxpCommunicator::connected()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_configured && m_connected && !m_stopRequested;
}

RegisterOperationResult MockVtrxpCommunicator::readRegister(
    std::uint32_t registerAddress
)
{
    std::chrono::milliseconds responseDelay { 0 };
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        responseDelay = m_options.responseDelay;
    }
    if (responseDelay.count() > 0)
        std::this_thread::sleep_for(responseDelay);

    std::lock_guard<std::mutex> lock(m_mutex);
    if (registerAddress >= m_registers.size())
    {
        return failure(
            RegisterOperation::Read,
            registerAddress,
            0,
            "MOCK register address is outside the 8-bit register bank"
        );
    }
    if (!m_configured || !m_connected || m_stopRequested)
    {
        return failure(
            RegisterOperation::Read,
            registerAddress,
            0,
            "MOCK backend is disconnected"
        );
    }

    RegisterOperationResult result;
    result.operation = RegisterOperation::Read;
    result.registerAddress = registerAddress;
    result.value = m_registers[registerAddress];
    result.valid = true;
    result.success = true;
    result.message = "MOCK read completed";
    result.generation = ++m_generation;
    m_lastValidRead = result;
    return result;
}

RegisterOperationResult MockVtrxpCommunicator::writeRegister(
    std::uint32_t registerAddress,
    std::uint8_t value
)
{
    std::chrono::milliseconds responseDelay { 0 };
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        responseDelay = m_options.responseDelay;
    }
    if (responseDelay.count() > 0)
        std::this_thread::sleep_for(responseDelay);

    std::lock_guard<std::mutex> lock(m_mutex);
    if (registerAddress >= m_registers.size())
    {
        return failure(
            RegisterOperation::Write,
            registerAddress,
            value,
            "MOCK register address is outside the 8-bit register bank"
        );
    }
    if (!m_configured || !m_connected || m_stopRequested)
    {
        return failure(
            RegisterOperation::Write,
            registerAddress,
            value,
            "MOCK backend is disconnected"
        );
    }

    m_registers[registerAddress] = value;

    RegisterOperationResult result;
    result.operation = RegisterOperation::Write;
    result.registerAddress = registerAddress;
    result.value = value;
    result.valid = true;
    result.success = true;
    result.message = "MOCK write completed";
    result.generation = ++m_generation;
    return result;
}

RegisterOperationResult MockVtrxpCommunicator::lastValidRead() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastValidRead;
}

bool MockVtrxpCommunicator::setMockOptions(
    const MockBackendOptions& options
)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_options = options;
    if (m_configured)
        m_connected = options.connectionAvailable && !m_stopRequested;
    return true;
}

RegisterOperationResult MockVtrxpCommunicator::failure(
    RegisterOperation operation,
    std::uint32_t registerAddress,
    std::uint8_t value,
    const std::string& message
)
{
    RegisterOperationResult result;
    result.operation = operation;
    result.registerAddress = registerAddress;
    result.value = value;
    result.valid = false;
    result.success = false;
    result.message = message;
    result.generation = ++m_generation;
    return result;
}

}
