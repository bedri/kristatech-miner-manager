cmake_minimum_required(VERSION 3.20)
project(kristatech-miner-manager VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(KMM_BUILD_TESTS "Build unit and UI test executables" ON)

set(_kmm_qt_components Widgets Network)
if(KMM_BUILD_TESTS)
    list(APPEND _kmm_qt_components Test)
endif()
find_package(Qt6 REQUIRED COMPONENTS ${_kmm_qt_components})

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
if(WIN32)
    target_link_libraries(kmm_core PUBLIC kernel32)
endif()

set(KMM_PLATFORM_SOURCES)
if(WIN32)
    list(APPEND KMM_PLATFORM_SOURCES resources/app_win.rc)
endif()

add_executable(kristatech-miner-manager
    src/main.cpp
    src/worker_dialog.h
    src/worker_dialog.cpp
    src/main_window.h
    src/main_window.cpp
    resources/resources.qrc
    ${KMM_PLATFORM_SOURCES}
)

if(WIN32)
    set_target_properties(kristatech-miner-manager PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
elseif(UNIX AND NOT APPLE)
    set_target_properties(kristatech-miner-manager PROPERTIES
        INSTALL_RPATH "\$ORIGIN/../lib;\$ORIGIN/lib;\$ORIGIN"
        BUILD_WITH_INSTALL_RPATH FALSE
    )
endif()

target_link_libraries(kristatech-miner-manager PRIVATE
    kmm_core
)

if(KMM_BUILD_TESTS)
    enable_testing()

    add_executable(test_core
        tests/test_core.cpp
    )

    target_link_libraries(test_core PRIVATE
        kmm_core
        Qt6::Test
    )

    add_test(NAME core_tests COMMAND test_core)

    add_executable(test_ui
        tests/test_ui.cpp
        src/worker_dialog.h
        src/worker_dialog.cpp
        src/main_window.h
        src/main_window.cpp
        resources/resources.qrc
    )

    target_link_libraries(test_ui PRIVATE
        kmm_core
        Qt6::Test
    )

    add_test(NAME ui_tests COMMAND test_ui)
endif()

include(GNUInstallDirs)

install(TARGETS kristatech-miner-manager
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(UNIX AND NOT APPLE)
    install(FILES resources/kristatech-miner-manager.desktop
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
    )
    install(FILES resources/icon.svg
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps
        RENAME kristatech-miner-manager.svg
    )
    foreach(_sz 16 24 32 48 64 128 256 512)
        install(FILES resources/icons/kristatech-miner-manager_${_sz}.png
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/${_sz}x${_sz}/apps
            RENAME kristatech-miner-manager.png
        )
    endforeach()
endif()
