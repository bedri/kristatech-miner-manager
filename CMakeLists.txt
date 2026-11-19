cmake_minimum_required(VERSION 3.20)
project(kristatech-miner-manager VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Test)

add_library(kmm_core STATIC
    src/i18n.h
    src/i18n.cpp
    src/theme.h
    src/theme.cpp
    src/models.h
    src/models.cpp
    src/config_store.h
    src/config_store.cpp
    src/rpc_client.h
    src/rpc_client.cpp
    src/process_scanner.h
    src/process_scanner.cpp
    src/worker_controller.h
    src/worker_controller.cpp
)

target_include_directories(kmm_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(kmm_core PUBLIC
    Qt6::Widgets
    Qt6::Network
)

add_executable(kristatech-miner-manager
    src/main.cpp
    src/worker_dialog.h
    src/worker_dialog.cpp
    src/main_window.h
    src/main_window.cpp
    resources/resources.qrc
)

target_link_libraries(kristatech-miner-manager PRIVATE
    kmm_core
)

enable_testing()

add_executable(test_core
    tests/test_core.cpp
)

target_link_libraries(test_core PRIVATE
    kmm_core
    Qt6::Test
)

add_test(NAME core_tests COMMAND test_core)
