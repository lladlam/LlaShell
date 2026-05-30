cmake_minimum_required(VERSION 3.20)
project(LlaShell LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT MSVC)
    set(CMAKE_RC_COMPILER windres)
endif()

add_compile_definitions(
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if(MSVC)
    add_compile_options(/W4 /utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -finput-charset=UTF-8 -Wno-unknown-pragmas -Wno-unused-parameter)
    add_link_options(-Wl,--allow-multiple-definition)
endif()

# --- CoreIPC DLL ---
add_library(CoreIPC SHARED
    CoreIPC/src/coreipc.cpp
    CoreIPC/src/pipe_server.cpp
    CoreIPC/src/pipe_client.cpp
    CoreIPC/src/pipe_security.cpp
    CoreIPC/src/protocol_codec.cpp
    CoreIPC/src/heartbeat.cpp
    CoreIPC/src/shm_manager.cpp
    CoreIPC/src/shm_slot.cpp
    CoreIPC/src/crc32.cpp
    CoreIPC/src/guardian.cpp
)

target_include_directories(CoreIPC PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/CoreIPC/include
)

target_compile_definitions(CoreIPC PRIVATE COREIPC_EXPORTS)

set_target_properties(CoreIPC PROPERTIES
    PREFIX ""
    OUTPUT_NAME "CoreIPC"
)

target_link_libraries(CoreIPC PRIVATE
    kernel32
    advapi32
    ole32
    ntdll
)

if(MSVC)
    target_link_options(CoreIPC PRIVATE /DEF:${CMAKE_CURRENT_SOURCE_DIR}/CoreIPC/coreipc.def)
else()
    target_sources(CoreIPC PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/CoreIPC/coreipc.def)
endif()

# --- Shared include directory ---
set(SHARED_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Shared)

# --- Desktop.exe ---
add_executable(Desktop WIN32
    Desktop/src/desktop_main.cpp
)
target_include_directories(Desktop PRIVATE
    Desktop/include
    ${SHARED_INCLUDE_DIR}
)
target_link_libraries(Desktop PRIVATE
    CoreIPC
    shell32
    gdi32
    ole32
)
if(NOT MSVC)
    target_link_options(Desktop PRIVATE -municode)
endif()

# --- Taskbar.exe ---
add_executable(Taskbar WIN32
    Taskbar/src/taskbar_main.cpp
)
target_include_directories(Taskbar PRIVATE
    Taskbar/include
    ${SHARED_INCLUDE_DIR}
)
target_link_libraries(Taskbar PRIVATE
    CoreIPC
    shell32
    gdi32
    comctl32
    shlwapi
)
if(NOT MSVC)
    target_link_options(Taskbar PRIVATE -municode)
endif()

# --- MainUI.exe ---
add_executable(MainUI WIN32
    MainUI/src/mainui_main.cpp
)
target_include_directories(MainUI PRIVATE
    MainUI/include
    ${SHARED_INCLUDE_DIR}
)
target_link_libraries(MainUI PRIVATE
    CoreIPC
    shell32
    shlwapi
    gdi32
    comctl32
    ole32
)
if(NOT MSVC)
    target_link_options(MainUI PRIVATE -municode)
endif()

# --- ShellHost.exe ---
add_executable(ShellHost WIN32
    ShellHost/src/shellhost_main.cpp
)
target_include_directories(ShellHost PRIVATE
    ${SHARED_INCLUDE_DIR}
)
target_link_libraries(ShellHost PRIVATE
    CoreIPC
    shell32
    shlwapi
    ole32
)
if(NOT MSVC)
    target_link_options(ShellHost PRIVATE -municode)
endif()

# --- MediaParser.exe ---
add_executable(MediaParser WIN32
    MediaParser/src/mediaparser_main.cpp
)
target_include_directories(MediaParser PRIVATE
    ${SHARED_INCLUDE_DIR}
)
target_link_libraries(MediaParser PRIVATE
    CoreIPC
    gdi32
    shell32
    shlwapi
    comctl32
)
if(NOT MSVC)
    target_link_options(MediaParser PRIVATE -municode)
endif()

# --- Updater.exe ---
add_executable(Updater WIN32
    Updater/src/updater_main.cpp
)
target_link_libraries(Updater PRIVATE
    shell32
    shlwapi
    wininet
)
if(NOT MSVC)
    target_link_options(Updater PRIVATE -municode)
endif()

# --- LlaShell.exe (主应用) ---
add_executable(LlaShell WIN32
    LlaShellApp/src/main.cpp
    resources/LlaShell.rc
)
target_include_directories(LlaShell PRIVATE
    ${SHARED_INCLUDE_DIR}
)
target_link_libraries(LlaShell PRIVATE
    CoreIPC
    shell32
    shlwapi
    comctl32
    wininet
    gdi32
    ole32
)
if(NOT MSVC)
    target_link_options(LlaShell PRIVATE -municode)
endif()

# --- Version Info RC for all exes (no icon, only version) ---
function(add_version_resource target)
    if(MSVC)
        target_sources(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/resources/version_only.rc)
    else()
        target_sources(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/resources/version_only.rc)
    endif()
endfunction()
