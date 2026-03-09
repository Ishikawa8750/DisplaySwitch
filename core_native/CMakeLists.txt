cmake_minimum_required(VERSION 3.20)

if(APPLE)
    project(DisplaySwitchNative VERSION 2.0.0 LANGUAGES CXX OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
else()
    project(DisplaySwitchNative VERSION 2.0.0 LANGUAGES CXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(BUILD_PYTHON_BINDINGS "Build Python bindings via pybind11" ON)
option(BUILD_TESTS "Build unit tests" ON)

# Platform-specific settings
if(WIN32)
    add_definitions(-D_WIN32_WINNT=0x0A00)  # Windows 10+
    add_definitions(-DUNICODE -D_UNICODE)
endif()

# Source files (platform-agnostic)
set(SOURCES
    src/edid_parser.cpp
    src/display_detector.cpp
    src/vcp_controller.cpp
    src/gpu_info.cpp
)

if(WIN32)
    list(APPEND SOURCES
        src/platform/windows/display_detector_win.cpp
        src/platform/windows/gpu_info_win.cpp
        src/platform/windows/vcp_controller_win.cpp
        src/platform/windows/wmi_brightness_win.cpp
    )
elseif(APPLE)
    list(APPEND SOURCES
        src/platform/macos/display_detector_mac.mm
        src/platform/macos/gpu_info_mac.mm
        src/platform/macos/vcp_controller_mac.mm
    )
elseif(UNIX)
    list(APPEND SOURCES
        src/platform/linux/display_detector_linux.cpp
        src/platform/linux/gpu_info_linux.cpp
        src/platform/linux/vcp_controller_linux.cpp
    )
endif()

# Main library (STATIC so pybind11 module and tests can link directly)
add_library(displayswitch_core STATIC ${SOURCES})

# Include directories
target_include_directories(displayswitch_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Platform-specific libraries
if(WIN32)
    target_link_libraries(displayswitch_core PRIVATE
        dxva2
        dxgi
        d3d11
        setupapi
        cfgmgr32
        wbemuuid
        oleaut32
        ole32
    )
elseif(APPLE)
    find_library(IOKIT_LIBRARY IOKit)
    find_library(COREFOUNDATION_LIBRARY CoreFoundation)
    find_library(COREGRAPHICS_LIBRARY CoreGraphics)
    find_library(FOUNDATION_LIBRARY Foundation)
    target_link_libraries(displayswitch_core PRIVATE
        ${IOKIT_LIBRARY}
        ${COREFOUNDATION_LIBRARY}
        ${COREGRAPHICS_LIBRARY}
        ${FOUNDATION_LIBRARY}
    )
elseif(UNIX)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBDRM REQUIRED libdrm)
    target_include_directories(displayswitch_core PRIVATE ${LIBDRM_INCLUDE_DIRS})
    target_link_libraries(displayswitch_core PRIVATE ${LIBDRM_LIBRARIES})
endif()

# ─── Shared DLL with C API (for Rust FFI / external consumers) ───────────
option(BUILD_SHARED_DLL "Build shared DLL with C API for FFI" ON)

if(BUILD_SHARED_DLL)
    add_library(displayswitch_ffi SHARED
        src/c_api.cpp
        ${SOURCES}
    )

    target_include_directories(displayswitch_ffi PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )

    target_compile_definitions(displayswitch_ffi PRIVATE DS_BUILD_DLL)

    if(WIN32)
        target_link_libraries(displayswitch_ffi PRIVATE
            dxva2
            dxgi
            d3d11
            setupapi
            cfgmgr32
            wbemuuid
            oleaut32
            ole32
        )
    elseif(APPLE)
        target_link_libraries(displayswitch_ffi PRIVATE
            ${IOKIT_LIBRARY}
            ${COREFOUNDATION_LIBRARY}
            ${COREGRAPHICS_LIBRARY}
            ${FOUNDATION_LIBRARY}
        )
    elseif(UNIX)
        target_include_directories(displayswitch_ffi PRIVATE ${LIBDRM_INCLUDE_DIRS})
        target_link_libraries(displayswitch_ffi PRIVATE ${LIBDRM_LIBRARIES})
    endif()

    # Copy shared library to tauri_ui/src-tauri for easy Rust access
    add_custom_command(TARGET displayswitch_ffi POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:displayswitch_ffi>
            ${CMAKE_CURRENT_SOURCE_DIR}/../tauri_ui/src-tauri/
        COMMENT "Copying $<TARGET_FILE_NAME:displayswitch_ffi> to tauri_ui/src-tauri/"
    )

    # macOS: ad-hoc codesign the dylib so it can be loaded by a signed app
    if(APPLE)
        add_custom_command(TARGET displayswitch_ffi POST_BUILD
            COMMAND codesign --force --sign - --timestamp=none
                $<TARGET_FILE:displayswitch_ffi>
            COMMAND codesign --force --sign - --timestamp=none
                ${CMAKE_CURRENT_SOURCE_DIR}/../tauri_ui/src-tauri/$<TARGET_FILE_NAME:displayswitch_ffi>
            COMMENT "Ad-hoc codesigning dylib for macOS"
        )
    endif()
endif()

# Python bindings
if(BUILD_PYTHON_BINDINGS)
    find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
    
    # Fetch pybind11 automatically
    include(FetchContent)
    FetchContent_Declare(
        pybind11
        GIT_REPOSITORY https://github.com/pybind/pybind11.git
        GIT_TAG        v2.13.6
    )
    FetchContent_MakeAvailable(pybind11)
    
    pybind11_add_module(displayswitch_native
        src/bindings/python_bindings.cpp
    )
    
    target_link_libraries(displayswitch_native PRIVATE displayswitch_core)
endif()

# Tests
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Installation
install(TARGETS displayswitch_core
    EXPORT DisplaySwitchTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/ DESTINATION include)
