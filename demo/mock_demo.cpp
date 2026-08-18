#include "VtrxpController.h"

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>

namespace
{

void printResult(
    const char* label,
    const VtrxpCommunicationLib::RegisterOperationResult& result
)
{
    std::cout << label
              << " success=" << std::boolalpha << result.success
              << " valid=" << result.valid
              << " address=0x" << std::hex << result.registerAddress
              << " value=0x" << static_cast<unsigned>(result.value)
              << std::dec
              << " generation=" << result.generation
              << " message=\"" << result.message << "\"\n";
}

void printState(
    const char* label,
    VtrxpCommunicationLib::CommunicationState state
)
{
    std::cout << label << " CommunicationState="
              << VtrxpCommunicationLib::communicationStateName(state)
              << '\n';
}

}

int main()
{
    using namespace VtrxpCommunicationLib;

    // These are simulation-only demonstration values, not VTRx+ register-map
    // claims. No semantic hardware behavior is attached to address 0x2a.
    constexpr std::uint32_t simulationOnlyRegister = 0x2a;
    constexpr std::uint8_t simulationOnlyValue = 0x5a;

    VtrxpController controller;
    printState("INITIAL   ", controller.communicationState());
    if (controller.communicationState() != CommunicationState::Disconnected)
        return 1;

    MockBackendOptions mockOptions;
    mockOptions.connectionAvailable = true;
    mockOptions.responseDelay = std::chrono::milliseconds(5);

    if (!controller.setMockOptions(mockOptions))
    {
        std::cerr << "This executable was not built with MOCK mode\n";
        return 2;
    }

    if (!controller.setupVtrxp("mock://local", 0, 0, 0, 0, 0))
    {
        std::cerr << "MOCK setup failed: "
                  << controller.lastLifecycleError() << '\n';
        return 3;
    }
    printState("CONFIGURED", controller.communicationState());
    if (controller.communicationState() != CommunicationState::Connecting)
        return 4;

    if (!controller.startCommunication())
    {
        std::cerr << "MOCK start failed: "
                  << controller.lastLifecycleError() << '\n';
        return 5;
    }
    printState("STARTED   ", controller.communicationState());
    if (controller.communicationState() != CommunicationState::Running)
        return 6;

    // A repeated start is intentionally idempotent.
    if (!controller.startCommunication())
        return 7;

    const auto writeResult = controller.registerWrite(
        simulationOnlyRegister,
        simulationOnlyValue
    );
    printResult("MOCK WRITE", writeResult);
    if (!writeResult.success)
        return 8;

    const auto readResult = controller.registerRead(simulationOnlyRegister);
    printResult("MOCK READ ", readResult);
    if (!readResult.success
        || !readResult.valid
        || readResult.value != simulationOnlyValue)
    {
        return 9;
    }

    const auto lastReadAfterSuccess = controller.lastValidRead();
    std::cout << "OPC UA projection LastReadAddress=0x" << std::hex
              << lastReadAfterSuccess.registerAddress
              << " LastReadValue=0x"
              << static_cast<unsigned>(lastReadAfterSuccess.value)
              << std::dec
              << " LastOperationSuccess=" << std::boolalpha
              << controller.lastOperation().success
              << " CommunicationState="
              << communicationStateName(controller.communicationState())
              << '\n';

    const auto invalidResult = controller.registerRead(0x100);
    printResult("MOCK ERROR", invalidResult);
    if (invalidResult.success)
        return 10;

    const auto lastReadAfterFailure = controller.lastValidRead();
    if (!lastReadAfterFailure.success
        || lastReadAfterFailure.registerAddress != simulationOnlyRegister
        || lastReadAfterFailure.value != simulationOnlyValue
        || controller.lastOperation().success)
    {
        return 11;
    }
    printState("AFTER ERROR", controller.communicationState());
    if (controller.communicationState() != CommunicationState::Running)
        return 12;

    controller.stopCommunication();
    controller.stopCommunication();
    printState("STOPPED   ", controller.communicationState());
    if (controller.communicationState() != CommunicationState::Stopped)
        return 13;

    const auto disconnectedResult = controller.registerRead(
        simulationOnlyRegister
    );
    printResult("AFTER STOP", disconnectedResult);
    if (disconnectedResult.success)
        return 14;

    if (!controller.setupVtrxp("mock://local-restart", 0, 0, 0, 0, 0))
        return 15;
    if (!controller.startCommunication())
        return 16;
    printState("RESTARTED ", controller.communicationState());
    if (controller.communicationState() != CommunicationState::Running)
        return 17;

    const auto readAfterRestart = controller.registerRead(
        simulationOnlyRegister
    );
    printResult("AFTER RESTART", readAfterRestart);
    if (!readAfterRestart.success
        || readAfterRestart.value != simulationOnlyValue)
    {
        return 18;
    }
    controller.stopCommunication();
    if (controller.communicationState() != CommunicationState::Stopped)
        return 19;

    VtrxpController unavailableController;
    MockBackendOptions unavailableOptions;
    unavailableOptions.connectionAvailable = false;
    if (!unavailableController.setMockOptions(unavailableOptions))
        return 20;
    if (unavailableController.setupVtrxp("mock://unavailable", 0, 0, 0, 0, 0))
        return 21;
    printState("CONNECT ERR", unavailableController.communicationState());
    if (unavailableController.communicationState() != CommunicationState::Error)
        return 22;

    std::cout << "MOCK OPC UA diagnostic projection passed\n";
    return 0;
}
