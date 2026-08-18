#include "VtrxpCommunicator.h"

#include <exception>
#include <iomanip>
#include <limits>
#include <vector>

namespace VtrxpCommunicationLib
{

using namespace hgtd;

VtrxpCommunicator::VtrxpCommunicator()
{
    LOG(Log::INF) << "VtrxpCommunicator REAL backend constructed";
}

VtrxpCommunicator::~VtrxpCommunicator()
{
    requestStop();
    LOG(Log::INF) << "VtrxpCommunicator REAL backend destroyed";
}

bool VtrxpCommunicator::configure(
    const BackendConfiguration& configuration,
    std::string& error
)
{
    setLpgbtInfo(configuration.felixId, configuration.lpgbtId);
    setI2cInfo(configuration.i2cMasterId, configuration.i2cSlaveId);
    connectSerialCommunicator(configuration.hostUrl, configuration.port);

    if (!connected())
    {
        error = "REAL SerialCommunicator connection failed";
        return false;
    }

    error.clear();
    return true;
}

void VtrxpCommunicator::run()
{
    // The blocking behavior and callback thread are owned by hgtd-felix-sw.
    SingleConnectionBase::run();
}

void VtrxpCommunicator::requestStop() noexcept
{
    failPending("REAL communication stopped before a reply was received");
    try
    {
        // Preserve the prototype's established shutdown primitive. Whether
        // this always releases run() must be validated in the CERN runtime.
        disconnect();
    }
    catch (const std::exception& exception)
    {
        LOG(Log::ERR) << "Exception during REAL disconnect: "
                      << exception.what();
    }
    catch (...)
    {
        LOG(Log::ERR) << "Unknown exception during REAL disconnect";
    }
}

bool VtrxpCommunicator::connected()
{
    try
    {
        return isConnected();
    }
    catch (...)
    {
        return false;
    }
}

void VtrxpCommunicator::connectSerialCommunicator(
    const std::string& hostUrl,
    int port
)
{
    m_hostUrl = hostUrl;
    m_port = port;

    try
    {
        connect(hostUrl, port);
    }
    catch (const std::exception& exception)
    {
        LOG(Log::ERR) << "Caught during connect(" << hostUrl << ", "
                      << port << "): " << exception.what();
    }

    if (connected())
        LOG(Log::INF) << "Connected to " << m_hostUrl << ':' << m_port;
    else
        LOG(Log::ERR) << "Connection to " << m_hostUrl << ':' << m_port
                      << " failed";
}

void VtrxpCommunicator::setLpgbtInfo(
    std::uint64_t felixId,
    std::uint64_t lpgbtId
)
{
    m_felixId = felixId;
    m_lpgbtId = lpgbtId;
}

void VtrxpCommunicator::setI2cInfo(
    std::uint32_t i2cMasterId,
    std::uint32_t i2cSlaveId
)
{
    m_i2cMasterId = i2cMasterId;
    m_i2cSlaveId = i2cSlaveId;
}

RegisterOperationResult VtrxpCommunicator::readRegister(
    std::uint32_t registerAddress
)
{
    std::lock_guard<std::mutex> operationLock(m_operationMutex);
    return executeRead(registerAddress);
}

RegisterOperationResult VtrxpCommunicator::writeRegister(
    std::uint32_t registerAddress,
    std::uint8_t value
)
{
    std::lock_guard<std::mutex> operationLock(m_operationMutex);
    return executeWrite(registerAddress, value);
}

RegisterOperationResult VtrxpCommunicator::executeRead(
    std::uint32_t registerAddress
)
{
    if (registerAddress > std::numeric_limits<std::uint8_t>::max())
    {
        return immediateFailure(
            RegisterOperation::Read,
            registerAddress,
            0,
            "REAL request uses a one-byte register address"
        );
    }
    if (!connected())
    {
        return immediateFailure(
            RegisterOperation::Read,
            registerAddress,
            0,
            "REAL backend is disconnected"
        );
    }

    std::unique_lock<std::mutex> pendingLock(m_pendingMutex);
    m_pending = PendingOperation {};
    m_pending.active = true;
    m_pending.operation = RegisterOperation::Read;
    m_pending.registerAddress = registerAddress;
    m_pending.generation = ++m_generation;
    pendingLock.unlock();

    try
    {
        sendReadRequest(registerAddress);
    }
    catch (const std::exception& exception)
    {
        failPending(std::string("REAL read send failed: ") + exception.what());
    }
    catch (...)
    {
        failPending("REAL read send failed with an unknown exception");
    }

    pendingLock.lock();
    if (!m_pendingCondition.wait_for(
            pendingLock,
            m_responseTimeout,
            [this]() { return m_pending.completed; }
        ))
    {
        m_pending.completed = true;
        m_pending.result = {
            RegisterOperation::Read,
            registerAddress,
            0,
            false,
            false,
            "Timed out waiting for a validated REAL read reply",
            m_pending.generation
        };
    }

    RegisterOperationResult result = m_pending.result;
    m_pending.active = false;
    return result;
}

RegisterOperationResult VtrxpCommunicator::executeWrite(
    std::uint32_t registerAddress,
    std::uint8_t value
)
{
    if (registerAddress > std::numeric_limits<std::uint8_t>::max())
    {
        return immediateFailure(
            RegisterOperation::Write,
            registerAddress,
            value,
            "REAL request uses a one-byte register address"
        );
    }
    if (!connected())
    {
        return immediateFailure(
            RegisterOperation::Write,
            registerAddress,
            value,
            "REAL backend is disconnected"
        );
    }

    std::unique_lock<std::mutex> pendingLock(m_pendingMutex);
    m_pending = PendingOperation {};
    m_pending.active = true;
    m_pending.operation = RegisterOperation::Write;
    m_pending.registerAddress = registerAddress;
    m_pending.requestedValue = value;
    m_pending.generation = ++m_generation;
    pendingLock.unlock();

    try
    {
        sendWriteRequest(registerAddress, value);
    }
    catch (const std::exception& exception)
    {
        failPending(std::string("REAL write send failed: ") + exception.what());
    }
    catch (...)
    {
        failPending("REAL write send failed with an unknown exception");
    }

    pendingLock.lock();
    if (!m_pendingCondition.wait_for(
            pendingLock,
            m_responseTimeout,
            [this]() { return m_pending.completed; }
        ))
    {
        m_pending.completed = true;
        m_pending.result = {
            RegisterOperation::Write,
            registerAddress,
            value,
            false,
            false,
            "Timed out waiting for a validated REAL write reply",
            m_pending.generation
        };
    }

    RegisterOperationResult result = m_pending.result;
    m_pending.active = false;
    return result;
}

void VtrxpCommunicator::sendReadRequest(std::uint32_t registerAddress)
{
    SerComI2C request;
    request.m_msg_type = SerComI2C::MsgType::REQUEST;
    request.m_req_type = SerComI2C::ReqType::READ;
    request.m_felix_id = m_felixId;
    request.m_lpgbt_id = m_lpgbtId;
    request.m_master_id = m_i2cMasterId;
    request.m_slave_adr = m_i2cSlaveId;
    request.m_reg_adr_width = 1;
    request.m_reg_adr = registerAddress;
    request.m_adr_10bit = false;
    request.m_data = std::vector<std::uint8_t>(1);

    DataMessage<DataMsgType> message;
    message.m_header.m_message_type = DataMsgType::SER_I2CREQ;
    message.m_header.m_priority = 0x00;
    message.m_header.m_fid = m_felixId;
    message << request;
    send(message);
}

void VtrxpCommunicator::sendWriteRequest(
    std::uint32_t registerAddress,
    std::uint8_t value
)
{
    SerComI2C request;
    request.m_msg_type = SerComI2C::MsgType::REQUEST;
    request.m_req_type = SerComI2C::ReqType::WRITE;
    request.m_felix_id = m_felixId;
    request.m_lpgbt_id = m_lpgbtId;
    request.m_master_id = m_i2cMasterId;
    request.m_slave_adr = m_i2cSlaveId;
    request.m_reg_adr_width = 1;
    request.m_reg_adr = registerAddress;
    request.m_adr_10bit = false;
    request.m_data = { value };

    DataMessage<DataMsgType> message;
    message.m_header.m_message_type = DataMsgType::SER_I2CREQ;
    message.m_header.m_priority = 0x00;
    message.m_header.m_fid = m_felixId;
    message << request;
    send(message);
}

void VtrxpCommunicator::onMessage(
    OwnedDataMessage<hgtd::DataMsgType> message
)
{
    auto dataMessage = message.msg;
    if (dataMessage.m_header.m_message_type != DataMsgType::SER_I2CREPLY)
        return;

    SerComI2C reply;
    try
    {
        dataMessage >> reply;
    }
    catch (const std::exception& exception)
    {
        failPending(std::string("Malformed REAL I2C reply: ") + exception.what());
        return;
    }
    catch (...)
    {
        failPending("Malformed REAL I2C reply");
        return;
    }

    std::lock_guard<std::mutex> pendingLock(m_pendingMutex);
    if (!m_pending.active || m_pending.completed)
        return;
    if (reply.m_msg_type != SerComI2C::MsgType::REPLY)
        return;
    if (reply.m_reg_adr != m_pending.registerAddress)
        return;

    if (m_pending.operation == RegisterOperation::Read)
    {
        if (reply.m_req_type != SerComI2C::ReqType::READ)
            return;
        if (!reply.m_success)
        {
            m_pending.result = {
                RegisterOperation::Read,
                m_pending.registerAddress,
                0,
                false,
                false,
                "REAL read reply reported failure",
                m_pending.generation
            };
        }
        else if (reply.m_data.empty())
        {
            m_pending.result = {
                RegisterOperation::Read,
                m_pending.registerAddress,
                0,
                false,
                false,
                "REAL read reply contained no data",
                m_pending.generation
            };
        }
        else
        {
            m_pending.result = {
                RegisterOperation::Read,
                m_pending.registerAddress,
                reply.m_data[0],
                true,
                true,
                "Validated REAL read reply",
                m_pending.generation
            };
            std::lock_guard<std::mutex> readLock(m_lastReadMutex);
            m_lastValidRead = m_pending.result;
        }
    }
    else if (m_pending.operation == RegisterOperation::Write)
    {
        if (reply.m_req_type != SerComI2C::ReqType::WRITE)
            return;
        m_pending.result = {
            RegisterOperation::Write,
            m_pending.registerAddress,
            m_pending.requestedValue,
            reply.m_success,
            reply.m_success,
            reply.m_success
                ? "Validated REAL write reply"
                : "REAL write reply reported failure",
            m_pending.generation
        };
    }
    else
    {
        return;
    }

    m_pending.completed = true;
    m_pendingCondition.notify_all();
}

RegisterOperationResult VtrxpCommunicator::lastValidRead() const
{
    std::lock_guard<std::mutex> readLock(m_lastReadMutex);
    return m_lastValidRead;
}

RegisterOperationResult VtrxpCommunicator::immediateFailure(
    RegisterOperation operation,
    std::uint32_t registerAddress,
    std::uint8_t value,
    const std::string& message
)
{
    std::lock_guard<std::mutex> pendingLock(m_pendingMutex);
    return {
        operation,
        registerAddress,
        value,
        false,
        false,
        message,
        ++m_generation
    };
}

void VtrxpCommunicator::failPending(const std::string& message)
{
    std::lock_guard<std::mutex> pendingLock(m_pendingMutex);
    if (!m_pending.active || m_pending.completed)
        return;

    m_pending.result = {
        m_pending.operation,
        m_pending.registerAddress,
        m_pending.requestedValue,
        false,
        false,
        message,
        m_pending.generation
    };
    m_pending.completed = true;
    m_pendingCondition.notify_all();
}

}
