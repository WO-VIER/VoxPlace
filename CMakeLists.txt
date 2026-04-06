cmake_minimum_required(VERSION 3.20)
project(VoxPlace VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ENABLE_GL_DEBUG "Enable OpenGL debug output for native builds" ON)
option(ENABLE_ASAN "Enable Address Sanitizer for debugging" OFF)
if(WIN32)
	option(ENABLE_TRACY "Enable Tracy Profiler" OFF)
else()
	option(ENABLE_TRACY "Enable Tracy Profiler" ON)
endif()

# ============================================================
# Compiler-specific flags
# ============================================================
if(MSVC)
	add_compile_options(/W0)
	if(CMAKE_BUILD_TYPE STREQUAL "Release")
		message(STATUS "Configuring Release build with optimizations (MSVC)")
		add_compile_options(/O2 /GL)
		add_link_options(/LTCG)
		add_definitions(-DNDEBUG)
	elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
		message(STATUS "Configuring RelWithDebInfo build (optimizations + debug info, MSVC)")
		add_compile_options(/O2 /GL /Zi)
		add_link_options(/LTCG /DEBUG)
		add_definitions(-DNDEBUG)
	else()
		message(STATUS "Configuring Debug build (MSVC)")
		add_compile_options(/Od /Zi)
		add_definitions(-DDEBUG)
	endif()

	if(ENABLE_ASAN)
		message(STATUS "Address Sanitizer enabled (MSVC)")
		add_compile_options(/fsanitize=address)
		add_link_options(/fsanitize=address)
	endif()
else()
	# GCC / Clang
	add_compile_options(-w)
	if(CMAKE_BUILD_TYPE STREQUAL "Release")
		message(STATUS "Configuring Release build with optimizations")
		add_compile_options(-O3 -march=native -flto -fno-plt)
		add_link_options(-flto)
		add_definitions(-DNDEBUG)
	elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
		message(STATUS "Configuring RelWithDebInfo build (optimizations + debug info)")
		add_compile_options(-O3 -march=native -flto -fno-plt -g3)
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
endif()

# ============================================================
# Dependency resolution
# ============================================================
if(WIN32)
	# --- GLFW ---
	set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
	set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
	set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
	set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
	add_subdirectory(thirdparty/glfw)

	# --- ENet ---
	set(ENET_STATIC_LIBRARY ON CACHE BOOL "" FORCE)
	set(ENET_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
	add_subdirectory(thirdparty/enet)

	# --- Zstd ---
	set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
	set(ZSTD_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
	set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
	set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
	set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "" FORCE)
	add_subdirectory(thirdparty/zstd/build/cmake)

	# --- libsodium ---
	set(SODIUM_DISABLE_TESTS ON CACHE BOOL "" FORCE)
	set(SODIUM_MINIMAL OFF CACHE BOOL "" FORCE)
	add_subdirectory(thirdparty/libsodium)

	# --- SQLite3 ---
	add_subdirectory(thirdparty/sqlite3)

	# --- OpenGL ---
	find_package(OpenGL REQUIRED)

	# --- Create PkgConfig-compatible aliases ---
	add_library(PkgConfig::GLFW INTERFACE IMPORTED)
	target_link_libraries(PkgConfig::GLFW INTERFACE glfw)

	add_library(PkgConfig::ENET INTERFACE IMPORTED)
	target_link_libraries(PkgConfig::ENET INTERFACE enet ws2_32 winmm)

	add_library(PkgConfig::SODIUM INTERFACE IMPORTED)
	target_link_libraries(PkgConfig::SODIUM INTERFACE sodium)

	add_library(PkgConfig::SQLITE3 INTERFACE IMPORTED)
	target_link_libraries(PkgConfig::SQLITE3 INTERFACE sqlite3)

	add_library(PkgConfig::ZSTD INTERFACE IMPORTED)
	target_link_libraries(PkgConfig::ZSTD INTERFACE libzstd_static)
else()
	# Linux / macOS: use pkg-config (original path, unchanged)
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
endif()

# ============================================================
# Core library
# ============================================================
set(CORE_SOURCES
	src/WorldProtocol.cpp
)

add_library(voxplace_core STATIC ${CORE_SOURCES})

target_include_directories(voxplace_core PUBLIC
	${CMAKE_SOURCE_DIR}/include
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/dependencies
	${CMAKE_SOURCE_DIR}/thirdparty/FastNoiseLite/Cpp
	${CMAKE_SOURCE_DIR}/thirdparty/glm
)

target_link_libraries(voxplace_core PUBLIC
	PkgConfig::ZSTD
)

# ============================================================
# Client executable
# ============================================================
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

# Copy assets to output directory
add_custom_command(TARGET VoxPlace POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${CMAKE_SOURCE_DIR}/assets"
		"$<TARGET_FILE_DIR:VoxPlace>/assets"
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${CMAKE_SOURCE_DIR}/src/shader"
		"$<TARGET_FILE_DIR:VoxPlace>/shader"
)

if(ENABLE_GL_DEBUG)
	target_compile_definitions(VoxPlace PRIVATE ENABLE_GL_DEBUG)
endif()

if(WIN32)
	target_compile_definitions(VoxPlace PRIVATE SHADER_PATH_PREFIX="shader/")
else()
	target_compile_definitions(VoxPlace PRIVATE SHADER_PATH_PREFIX="src/shader/")
endif()

target_include_directories(VoxPlace PRIVATE
	${CMAKE_SOURCE_DIR}/include
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/dependencies
	${CMAKE_SOURCE_DIR}/thirdparty/FastNoiseLite/Cpp
	${CMAKE_SOURCE_DIR}/thirdparty/imgui
	${CMAKE_SOURCE_DIR}/thirdparty/imgui/backends
	${CMAKE_SOURCE_DIR}/thirdparty/enet/include
)

target_link_libraries(VoxPlace PRIVATE voxplace_core)

target_link_libraries(VoxPlace PRIVATE
	PkgConfig::GLFW
	PkgConfig::ENET
	${OPENGL_gl_LIBRARY}
)

# ============================================================
# Tracy Profiler
# ============================================================
if(ENABLE_TRACY)
	message(STATUS "Integrating Tracy Profiler...")
	add_subdirectory(thirdparty/tracy)
endif()

# ============================================================
# Server executable
# ============================================================
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
	${CMAKE_SOURCE_DIR}/thirdparty/enet/include
)

target_link_libraries(VoxPlaceServer PRIVATE
	voxplace_core
	PkgConfig::ENET
	PkgConfig::SODIUM
	PkgConfig::SQLITE3
	PkgConfig::ZSTD
)

# ============================================================
# World storage benchmark
# ============================================================
set(WORLD_STORAGE_BENCH_SOURCES
	src/WorldTable.cpp
	src/server/world_storage_bench.cpp
)

add_executable(VoxPlaceWorldStorageBench ${WORLD_STORAGE_BENCH_SOURCES})

target_include_directories(VoxPlaceWorldStorageBench PRIVATE
	${CMAKE_SOURCE_DIR}/include
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/dependencies
	${CMAKE_SOURCE_DIR}/thirdparty/FastNoiseLite/Cpp
)

target_link_libraries(VoxPlaceWorldStorageBench PRIVATE
	voxplace_core
	PkgConfig::SQLITE3
	PkgConfig::ZSTD
)

# ============================================================
# Pre-generation tool
# ============================================================
set(PREGEN_SOURCES
	src/tools/pregen_main.cpp
	src/WorldClient.cpp
)

add_executable(VoxPlacePregen ${PREGEN_SOURCES})

target_include_directories(VoxPlacePregen PRIVATE
	${CMAKE_SOURCE_DIR}/include
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/dependencies
	${CMAKE_SOURCE_DIR}/thirdparty/FastNoiseLite/Cpp
	${CMAKE_SOURCE_DIR}/thirdparty/enet/include
)

target_link_libraries(VoxPlacePregen PRIVATE
	voxplace_core
	PkgConfig::ENET
)

# ============================================================
# Tracy linkage (after all targets)
# ============================================================
if(ENABLE_TRACY)
	target_link_libraries(VoxPlaceServer PRIVATE Tracy::TracyClient)
	target_compile_definitions(VoxPlaceServer PRIVATE TRACY_ENABLE)
endif()
