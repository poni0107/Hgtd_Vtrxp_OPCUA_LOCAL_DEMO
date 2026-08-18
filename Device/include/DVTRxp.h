#ifndef __DVTRxp__H__
#define __DVTRxp__H__

#include <Base_DVTRxp.h>

#include "VtrxpController.h"

#include <cstdint>
#include <string>

namespace Device
{

// Custom QUASAR device-logic class. Base_DVTRxp and ASVTRxp remain generated.
class DVTRxp : public Base_DVTRxp
{
public:
    explicit DVTRxp(
        const Configuration::VTRxp& config,
        Parent_DVTRxp* parent
    );

    ~DVTRxp() noexcept;

    UaStatus callReadRegister(OpcUa_UInt32 registerAddress);
    UaStatus callWriteRegister(
        OpcUa_UInt32 registerAddress,
        OpcUa_Byte value
    );

    void startCommunication();
    void stopCommunication() noexcept;
    void update();

private:
    DVTRxp(const DVTRxp&) = delete;
    DVTRxp& operator=(const DVTRxp&) = delete;

    static std::uint64_t parseHexUint64(const std::string& text);
    static std::uint32_t parseHexUint32(const std::string& text);
    static int parsePort(const std::string& text);

    void publishOperationDiagnostics(
        const VtrxpCommunicationLib::RegisterOperationResult& result
    );
    void publishCommunicationDiagnostics();

    VtrxpCommunicationLib::VtrxpController m_vtrxpController {};
    std::string m_serialCommunicatorHost {};
    int m_serialCommunicatorPort { 0 };
    std::uint64_t m_felixId { 0 };
    std::uint64_t m_lpgbtId { 0 };
    std::uint32_t m_i2cMasterId { 0 };
    std::uint32_t m_i2cSlaveId { 0 };
};

}

#endif
