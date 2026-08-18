#include <Configuration.hxx>

#include <ASVTRxp.h>
#include <DVTRxp.h>
#include <LogIt.h>

#include <limits>
#include <stdexcept>

namespace
{

std::string withoutHexPrefix(const std::string& text)
{
    if (text.size() >= 2
        && text[0] == '0'
        && (text[1] == 'x' || text[1] == 'X'))
    {
        return text.substr(2);
    }
    return text;
}

std::uint64_t parseUnsigned(
    const std::string& original,
    int base,
    const char* fieldName
)
{
    const std::string value = base == 16
        ? withoutHexPrefix(original)
        : original;

    if (value.empty())
        throw std::invalid_argument(std::string(fieldName) + " is empty");

    std::size_t consumed = 0;
    const std::uint64_t parsed = std::stoull(value, &consumed, base);
    if (consumed != value.size())
    {
        throw std::invalid_argument(
            std::string(fieldName) + " contains trailing characters"
        );
    }
    return parsed;
}

}

namespace Device
{

DVTRxp::DVTRxp(
    const Configuration::VTRxp& config,
    Parent_DVTRxp* parent
):
    Base_DVTRxp(config, parent),
    m_serialCommunicatorHost(config.serialCommunicatorHost()),
    m_serialCommunicatorPort(parsePort(config.serialCommunicatorPort())),
    m_felixId(parseHexUint64(config.felixId())),
    m_lpgbtId(parseHexUint64(config.lpGbtId())),
    m_i2cMasterId(parseHexUint32(config.i2cMasterId())),
    m_i2cSlaveId(parseHexUint32(config.i2cSlaveId()))
{
}

DVTRxp::~DVTRxp() noexcept
{
    try
    {
        stopCommunication();
    }
    catch (...)
    {
        // Destructors must not propagate. VtrxpController also has a noexcept
        // stop/destructor as a second line of defense.
    }
}

UaStatus DVTRxp::callReadRegister(OpcUa_UInt32 registerAddress)
{
    const auto result = m_vtrxpController.registerRead(registerAddress);
    publishOperationDiagnostics(result);
    if (!result.success || !result.valid)
        return OpcUa_Bad;
    return OpcUa_Good;
}

UaStatus DVTRxp::callWriteRegister(
    OpcUa_UInt32 registerAddress,
    OpcUa_Byte value
)
{
    const auto result = m_vtrxpController.registerWrite(
        registerAddress,
        value
    );
    publishOperationDiagnostics(result);
    return result.success ? OpcUa_Good : OpcUa_Bad;
}

void DVTRxp::startCommunication()
{
    const bool configured = m_vtrxpController.setupVtrxp(
        m_serialCommunicatorHost,
        m_serialCommunicatorPort,
        m_felixId,
        m_lpgbtId,
        m_i2cMasterId,
        m_i2cSlaveId
    );

    if (!configured || !m_vtrxpController.startCommunication())
    {
        LOG(Log::ERR) << "VTRx+ communication startup failed: "
                      << m_vtrxpController.lastLifecycleError();
    }

    publishCommunicationDiagnostics();
}

void DVTRxp::stopCommunication() noexcept
{
    m_vtrxpController.stopCommunication();
}

void DVTRxp::update()
{
    const auto operation = m_vtrxpController.lastOperation();
    if (operation.generation != 0)
        publishOperationDiagnostics(operation);
    else
        publishCommunicationDiagnostics();
}

void DVTRxp::publishOperationDiagnostics(
    const VtrxpCommunicationLib::RegisterOperationResult& result
)
{
    auto* addressSpace = getAddressSpaceLink();

    // LastReadAddress and LastReadValue are updated as one logical pair only
    // after a validated READ. A failed READ never destroys the last good pair.
    if (result.operation == VtrxpCommunicationLib::RegisterOperation::Read
        && result.success
        && result.valid)
    {
        addressSpace->setLastReadAddress(
            result.registerAddress,
            OpcUa_Good
        );
        addressSpace->setLastReadValue(result.value, OpcUa_Good);
    }

    addressSpace->setLastOperationType(
        UaString(
            VtrxpCommunicationLib::registerOperationName(result.operation)
        ),
        OpcUa_Good
    );
    addressSpace->setLastOperationAddress(
        result.registerAddress,
        OpcUa_Good
    );
    addressSpace->setLastOperationValue(result.value, OpcUa_Good);
    addressSpace->setLastOperationSuccess(result.success, OpcUa_Good);
    addressSpace->setLastOperationStatus(
        UaString(result.message.c_str()),
        OpcUa_Good
    );
    addressSpace->setLastError(
        UaString(result.success ? "" : result.message.c_str()),
        OpcUa_Good
    );
    addressSpace->setGeneration(result.generation, OpcUa_Good);
    publishCommunicationDiagnostics();
}

void DVTRxp::publishCommunicationDiagnostics()
{
    const auto state = m_vtrxpController.communicationState();
    auto* addressSpace = getAddressSpaceLink();
    addressSpace->setCommunicationState(
        UaString(VtrxpCommunicationLib::communicationStateName(state)),
        OpcUa_Good
    );

    if (state == VtrxpCommunicationLib::CommunicationState::Error)
    {
        const std::string lifecycleError =
            m_vtrxpController.lastLifecycleError();
        addressSpace->setLastError(
            UaString(lifecycleError.c_str()),
            OpcUa_Good
        );
    }
    else if (m_vtrxpController.lastOperation().generation == 0)
    {
        // Give clients a readable initial LastError value after successful
        // startup without erasing a later operation-level error.
        addressSpace->setLastError(UaString(""), OpcUa_Good);
    }
}

std::uint64_t DVTRxp::parseHexUint64(const std::string& text)
{
    return parseUnsigned(text, 16, "64-bit hexadecimal configuration value");
}

std::uint32_t DVTRxp::parseHexUint32(const std::string& text)
{
    const std::uint64_t parsed = parseUnsigned(
        text,
        16,
        "32-bit hexadecimal configuration value"
    );
    if (parsed > std::numeric_limits<std::uint32_t>::max())
        throw std::out_of_range("Hexadecimal configuration value exceeds uint32_t");
    return static_cast<std::uint32_t>(parsed);
}

int DVTRxp::parsePort(const std::string& text)
{
    const bool explicitHex = text.size() >= 2
        && text[0] == '0'
        && (text[1] == 'x' || text[1] == 'X');
    const std::uint64_t parsed = parseUnsigned(
        text,
        explicitHex ? 16 : 10,
        "serialCommunicatorPort"
    );

    // TCP/UDP port zero remains representable for MOCK configurations, but
    // values outside the protocol's 16-bit range are rejected.
    if (parsed > 65535)
        throw std::out_of_range("serialCommunicatorPort exceeds 65535");
    return static_cast<int>(parsed);
}

}
