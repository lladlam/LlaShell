cmake_minimum_required(VERSION 3.20)
project(LlaShell LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_definitions(
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if(MSVC)
    add_compile_options(/W4 /utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -finput-charset=UTF-8)
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

target_link_libraries(CoreIPC PRIVATE
    kernel32
    advapi32
    ole32
    ntdll
)

if(MSVC)
    target_link_options(CoreIPC PRIVATE /DEF:${CMAKE_CURRENT_SOURCE_DIR}/CoreIPC/coreipc.def)
else()
    target_link_options(CoreIPC PRIVATE -Wl,--def,${CMAKE_CURRENT_SOURCE_DIR}/CoreIPC/coreipc.def)
endif()
