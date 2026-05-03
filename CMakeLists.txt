cmake_minimum_required(VERSION 3.16)

project(scada_iec104_socket
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(scada_core
    src/common/Config.cpp
    src/common/Logger.cpp
    src/net/SocketCompat.cpp
    src/net/TcpClient.cpp
    src/iec104/Iec104Frame.cpp
    src/iec104/Iec104Client.cpp
    src/scada/DataPoint.cpp
    src/scada/AlarmManager.cpp
    src/scada/DataCache.cpp
    src/scada/ScadaSystem.cpp
    src/mock/MockIec104Server.cpp
)

target_include_directories(scada_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(WIN32)
    target_link_libraries(scada_core PUBLIC ws2_32)
endif()

if(MSVC)
    target_compile_options(scada_core PRIVATE /W4)
else()
    target_compile_options(scada_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(scada_client src/main_client.cpp)
target_link_libraries(scada_client PRIVATE scada_core)

add_executable(iec104_mock_server src/main_mock_server.cpp)
target_link_libraries(iec104_mock_server PRIVATE scada_core)

add_executable(scada_selftest tests/iec104_frame_test.cpp)
target_link_libraries(scada_selftest PRIVATE scada_core)
