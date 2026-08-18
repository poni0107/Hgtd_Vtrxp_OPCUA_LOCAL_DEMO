cmake_minimum_required(VERSION 3.16)

project(VtrxpLocalMockGui LANGUAGES CXX)

option(
    VTRXP_USE_MOCK_BACKEND
    "Build the standalone local MOCK/SIMULATION demonstration"
    ON
)

if(NOT VTRXP_USE_MOCK_BACKEND)
    message(FATAL_ERROR
        "This standalone CMake project validates MOCK mode only. "
        "Build REAL mode from the CERN QUASAR project so its existing "
        "TDAQ/hgtd-felix-sw dependencies remain in effect."
    )
endif()

find_package(Threads REQUIRED)

add_library(VtrxpCommunicationMock STATIC
    VtrxpCommunicationLib/src/VtrxpController.cpp
    VtrxpCommunicationLib/src/MockVtrxpCommunicator.cpp
)

target_compile_features(VtrxpCommunicationMock PUBLIC cxx_std_17)
target_compile_definitions(VtrxpCommunicationMock PUBLIC
    VTRXP_USE_MOCK_BACKEND=1
)
target_include_directories(VtrxpCommunicationMock PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/VtrxpCommunicationLib/include
)
target_link_libraries(VtrxpCommunicationMock PUBLIC Threads::Threads)

add_executable(vtrxp_mock_demo demo/mock_demo.cpp)
target_link_libraries(vtrxp_mock_demo PRIVATE VtrxpCommunicationMock)

if(WIN32)
    add_executable(vtrxp_mock_gui WIN32 gui/win32_mock_gui.cpp)
    target_compile_features(vtrxp_mock_gui PRIVATE cxx_std_17)
    target_compile_definitions(vtrxp_mock_gui PRIVATE UNICODE _UNICODE)
    target_link_libraries(vtrxp_mock_gui PRIVATE
        VtrxpCommunicationMock
        user32
        gdi32
    )
endif()

enable_testing()
add_test(NAME vtrxp_mock_diagnostic_demo COMMAND vtrxp_mock_demo)
