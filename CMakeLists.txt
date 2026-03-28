cmake_minimum_required(VERSION 3.10)
project(VoxPlace VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ENABLE_GL_DEBUG "Enable OpenGL debug output for native builds" ON)
option(ENABLE_ASAN "Enable Address Sanitizer for debugging" OFF)

add_compile_options(-w)

# Build-specific flags
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    message(STATUS "Configuring Release build with optimizations")
    add_compile_options(-O3 -march=native -flto -fno-plt)
    add_link_options(-flto)
    add_definitions(-DNDEBUG)
else()
    message(STATUS "Configuring Debug build")
    add_compile_options(-g3 -O0 -fbounds-check)
    add_definitions(-DDEBUG)
endif()

if(ENABLE_ASAN)
    message(STATUS "Address Sanitizer enabled")
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED IMPORTED_TARGET glfw3)
pkg_check_modules(ENET QUIET IMPORTED_TARGET libenet)
if(NOT ENET_FOUND)
    pkg_check_modules(ENET REQUIRED IMPORTED_TARGET enet)
endif()
pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)
pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)
pkg_check_modules(ZSTD REQUIRED IMPORTED_TARGET libzstd)
set(OpenGL_GL_PREFERENCE "GLVND")
find_package(OpenGL REQUIRED)

set(CORE_SOURCES
    src/WorldProtocol.cpp
)

add_library(voxplace_core STATIC ${CORE_SOURCES})

target_include_directories(voxplace_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/dependencies
    ${CMAKE_SOURCE_DIR}/thirdparty/FastNoiseLite/Cpp
)

set(CLIENT_SOURCES
    src/client/core/ClientApp.cpp
    src/client/core/ClientLaunch.cpp
    src/client/core/ClientProfiler.cpp
    src/client/core/InputSystem.cpp
    src/client/gameplay/CameraBoundsSystem.cpp
    src/client/rendering/ClientFrameRenderer.cpp
    src/client/rendering/WorldRenderer.cpp
    src/client/ui/CooldownHud.cpp
    src/client/ui/DebugOverlayBuilder.cpp
    src/client/ui/DebugOverlay.cpp
    src/client/ui/LoginScreen.cpp
    src/client/gameplay/BlockInteractionSystem.cpp
    src/client/gameplay/ClientWorldSystem.cpp
    src/client/gameplay/ChunkStreamingSystem.cpp
    src/client/gameplay/MeshBuildSystem.cpp
    src/client/gameplay/PlayerSyncSystem.cpp
    src/client/main.cpp
    src/WorldClient.cpp
    src/profiler.cpp
    thirdparty/imgui/imgui.cpp
    thirdparty/imgui/imgui_draw.cpp
    thirdparty/imgui/imgui_tables.cpp
    thirdparty/imgui/imgui_widgets.cpp
    thirdparty/imgui/imgui_demo.cpp
    thirdparty/imgui/backends/imgui_impl_glfw.cpp
    thirdparty/imgui/backends/imgui_impl_opengl3.cpp
)

list(APPEND CLIENT_SOURCES src/glad.c)
if(ENABLE_GL_DEBUG)
    list(APPEND CLIENT_SOURCES src/errorReporting.cpp)
endif()

add_executable(VoxPlace ${CLIENT_SOURCES})

if(ENABLE_GL_DEBUG)
    target_compile_definitions(VoxPlace PRIVATE ENABLE_GL_DEBUG)
endif()

target_include_directories(VoxPlace PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/dependencies
    ${CMAKE_SOURCE_DIR}/thirdparty/FastNoiseLite/Cpp
    ${CMAKE_SOURCE_DIR}/thirdparty/imgui
    ${CMAKE_SOURCE_DIR}/thirdparty/imgui/backends
)

target_link_libraries(VoxPlace PRIVATE voxplace_core)

target_link_libraries(VoxPlace PRIVATE
    PkgConfig::GLFW
    PkgConfig::ENET
    ${OPENGL_gl_LIBRARY}
)

set(SERVER_SOURCES
    src/PasswordHasher.cpp
    src/PlayerTable.cpp
    src/WorldTable.cpp
    src/server/core/ServerLaunch.cpp
    src/server/main.cpp
    src/WorldServer.cpp
)

add_executable(VoxPlaceServer ${SERVER_SOURCES})

target_include_directories(VoxPlaceServer PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/dependencies
    ${CMAKE_SOURCE_DIR}/thirdparty/FastNoiseLite/Cpp
)

target_link_libraries(VoxPlaceServer PRIVATE
    voxplace_core
    PkgConfig::ENET
    PkgConfig::SODIUM
    PkgConfig::SQLITE3
    PkgConfig::ZSTD
)
