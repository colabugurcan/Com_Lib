cmake_minimum_required(VERSION 3.16)

project(communication_lib VERSION 0.1.0 LANGUAGES CXX)

option(COMM_BUILD_TESTS "Build unit tests" ON)
option(COMM_BUILD_EXAMPLES "Build usage examples" ON)
option(COMM_HEADER_ONLY "Build the library as header-only" ON)
option(COMM_ENABLE_INSTALL "Enable install targets" ON)
option(COMM_ENABLE_INTEGRATION_TESTS "Build integration tests requiring hardware" OFF)
option(COMM_ENABLE_PCAN "Enable PCAN-Basic CAN backend" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

file(GLOB_RECURSE COMM_HEADERS CONFIGURE_DEPENDS "include/*.hpp")
set(COMM_SOURCES
    src/can/pcan_basic.cpp
    src/can/socketcan.cpp
    src/ethernet/tcp.cpp
    src/ethernet/udp.cpp
    src/serial/serial_port.cpp
)
if(NOT COMM_ENABLE_PCAN)
    list(FILTER COMM_SOURCES EXCLUDE REGEX ".*/pcan_basic.cpp$")
    list(FILTER COMM_SOURCES EXCLUDE REGEX ".*/socketcan.cpp$")
endif()

find_package(Threads REQUIRED)

set(COMM_HAS_PCAN FALSE)
if(COMM_ENABLE_PCAN)
    find_path(PCANBASIC_INCLUDE_DIR PCANBasic.h)
    find_library(PCANBASIC_LIBRARY NAMES pcanbasic)
    if(PCANBASIC_INCLUDE_DIR AND PCANBASIC_LIBRARY)
        set(COMM_HAS_PCAN TRUE)
    else()
        message(WARNING "PCANBasic headers or library not found; disabling PCAN backend")
    endif()
endif()

if(COMM_HEADER_ONLY OR NOT COMM_SOURCES)
    add_library(comm INTERFACE)
    set(_comm_scope INTERFACE)
    if(COMM_SOURCES)
        target_sources(
            comm
            INTERFACE
                $<BUILD_INTERFACE:${COMM_SOURCES}>
        )
    endif()
    target_include_directories(
        comm
        INTERFACE
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    )
    target_compile_features(comm INTERFACE cxx_std_17)
    target_link_libraries(comm INTERFACE Threads::Threads)
else()
    add_library(comm STATIC ${COMM_SOURCES} ${COMM_HEADERS})
    set(_comm_scope PUBLIC)
    target_include_directories(
        comm
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    )
    target_compile_features(comm PUBLIC cxx_std_17)
    target_link_libraries(comm PUBLIC Threads::Threads)
endif()

if(COMM_HAS_PCAN)
    target_compile_definitions(comm ${_comm_scope} COMM_HAS_PCAN=1)
    target_include_directories(comm ${_comm_scope} ${PCANBASIC_INCLUDE_DIR})
    target_link_libraries(comm ${_comm_scope} ${PCANBASIC_LIBRARY})
endif()

if(COMM_ENABLE_INSTALL)
    include(GNUInstallDirs)

    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    if(TARGET comm)
        install(TARGETS comm
            EXPORT commTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        )

        install(EXPORT commTargets
            FILE commTargets.cmake
            NAMESPACE comm::
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/communication_lib
        )
    endif()

    include(CMakePackageConfigHelpers)
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/communication_libConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    configure_package_config_file(
        ${CMAKE_CURRENT_LIST_DIR}/cmake/communication_libConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/communication_libConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/communication_lib
    )

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/communication_libConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/communication_libConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/communication_lib
    )
endif()

if(COMM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(COMM_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
