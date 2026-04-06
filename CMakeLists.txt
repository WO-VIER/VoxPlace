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
	include(FetchContent)

	# --- GLFW ---
	set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
	set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
	set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
	set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
	FetchContent_Declare(glfw
		GIT_REPOSITORY https://github.com/glfw/glfw.git
		GIT_TAG 3.4
		GIT_SHALLOW TRUE
	)
	FetchContent_MakeAvailable(glfw)

	# --- ENet ---
	set(ENET_STATIC_LIBRARY ON CACHE BOOL "" FORCE)
	set(ENET_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
	FetchContent_Declare(enet
		GIT_REPOSITORY https://github.com/lsalzman/enet.git
		GIT_TAG v1.3.18
		GIT_SHALLOW TRUE
	)
	FetchContent_MakeAvailable(enet)

	# --- Zstd ---
	set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
	set(ZSTD_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
	set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
	set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
	set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "" FORCE)
	FetchContent_Declare(zstd
		GIT_REPOSITORY https://github.com/facebook/zstd.git
		GIT_TAG v1.5.6
		GIT_SHALLOW TRUE
	)
	FetchContent_MakeAvailable(zstd)
	# zstd CMake is in build/cmake subdirectory
	add_subdirectory(${zstd_SOURCE_DIR}/build/cmake ${zstd_BINARY_DIR}/build_cmake EXCLUDE_FROM_ALL)

	# --- libsodium ---
	FetchContent_Declare(sodium
		URL https://download.libsodium.org/libsodium/releases/libsodium-1.0.21-stable.tar.gz
	)
	FetchContent_GetProperties(sodium)
	if(NOT sodium_POPULATED)
		FetchContent_Populate(sodium)
	endif()
	add_library(sodium STATIC
		${sodium_SOURCE_DIR}/src/libsodium/crypto_aead/aegis128l/aead_aegis128l.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_aead/aegis128l/aead_aegis128l_aesni.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_aead/aegis128l/aead_aegis128l_armcrypto.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_aead/aegis128l/aead_aegis128l_soft.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_aead/aegis256/aead_aegis256.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_aead/aegis256/aegis256_aesni.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_aead/aegis256/aegis256_armcrypto.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_aead/aegis256/aegis256_soft.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_aead/aes256gcm/aesni/aead_aes256gcm_aesni.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_aead/aes256gcm/aead_aes256gcm.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_aead/chacha20poly1305/aead_chacha20poly1305.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_aead/xchacha20poly1305/aead_xchacha20poly1305.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_auth/crypto_auth.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_auth/hmacsha256/auth_hmacsha256.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_auth/hmacsha512/auth_hmacsha512.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_auth/hmacsha512256/auth_hmacsha512256.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_box/crypto_box.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_box/crypto_box_easy.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_box/crypto_box_seal.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_box/curve25519xsalsa20poly1305/box_curve25519xsalsa20poly1305.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_core/ed25519/ref10/ed25519_ref10.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_core/hchacha20/core_hchacha20.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_core/hsalsa20/core_hsalsa20.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_core/hsalsa20/ref2/core_hsalsa20_ref2.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_core/salsa/ref/core_salsa_ref.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_core/softaes/softaes.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_generichash/blake2b/generichash_blake2.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_generichash/blake2b/ref/blake2b-compress-avx2.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_generichash/blake2b/ref/blake2b-compress-ref.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_generichash/blake2b/ref/blake2b-compress-sse41.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_generichash/blake2b/ref/blake2b-compress-ssse3.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_generichash/blake2b/ref/blake2b-ref.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_generichash/blake2b/ref/generichash_blake2b.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_generichash/crypto_generichash.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_hash/crypto_hash.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_hash/sha256/cp/hash_sha256_cp.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_hash/sha256/hash_sha256.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_hash/sha512/cp/hash_sha512_cp.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_hash/sha512/hash_sha512.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_kdf/blake2b/kdf_blake2b.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_kdf/crypto_kdf.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_kdf/hkdf/kdf_hkdf_sha256.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_kdf/hkdf/kdf_hkdf_sha512.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_kx/crypto_kx.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_onetimeauth/crypto_onetimeauth.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_onetimeauth/poly1305/donna/poly1305_donna.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_onetimeauth/poly1305/onetimeauth_poly1305.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_onetimeauth/poly1305/sse2/poly1305_sse2.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_pwhash/argon2/argon2-core.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_pwhash/argon2/argon2-encoding.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_pwhash/argon2/argon2-fill-block-avx2.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_pwhash/argon2/argon2-fill-block-avx512f.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_pwhash/argon2/argon2-fill-block-ref.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_pwhash/argon2/argon2-fill-block-ssse3.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_pwhash/argon2/argon2.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_pwhash/argon2/blake2b-long.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_pwhash/argon2/pwhash_argon2i.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_pwhash/argon2/pwhash_argon2id.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_pwhash/crypto_pwhash.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_scalarmult/crypto_scalarmult.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_scalarmult/curve25519/ref10/x25519_ref10.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_scalarmult/curve25519/sandy2x/curve25519_sandy2x.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_scalarmult/curve25519/sandy2x/fe51_invert.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_scalarmult/curve25519/sandy2x/fe_frombytes_sandy2x.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_scalarmult/curve25519/scalarmult_curve25519.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_secretbox/crypto_secretbox.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_secretbox/crypto_secretbox_easy.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_secretbox/xsalsa20poly1305/secretbox_xsalsa20poly1305.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_secretstream/xchacha20poly1305/secretstream_xchacha20poly1305.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_shorthash/crypto_shorthash.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_shorthash/siphash24/ref/shorthash_siphash24_ref.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_shorthash/siphash24/shorthash_siphash24.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_sign/crypto_sign.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_sign/ed25519/ref10/keypair.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_sign/ed25519/ref10/open.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_sign/ed25519/ref10/sign.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_sign/ed25519/sign_ed25519.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_stream/chacha20/dolbeau/chacha20_dolbeau-avx2.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_stream/chacha20/dolbeau/chacha20_dolbeau-ssse3.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_stream/chacha20/ref/chacha20_ref.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_stream/chacha20/stream_chacha20.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_stream/crypto_stream.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_stream/salsa20/ref/salsa20_ref.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_stream/salsa20/stream_salsa20.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_stream/salsa20/xmm6/salsa20_xmm6.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_stream/salsa20/xmm6int/salsa20_xmm6int-avx2.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_stream/salsa20/xmm6int/salsa20_xmm6int-sse2.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_stream/xsalsa20/stream_xsalsa20.c
		${sodium_SOURCE_DIR}/src/libsodium/crypto_verify/verify.c
		${sodium_SOURCE_DIR}/src/libsodium/randombytes/internal/randombytes_internal_random.c
		${sodium_SOURCE_DIR}/src/libsodium/randombytes/randombytes.c
		${sodium_SOURCE_DIR}/src/libsodium/randombytes/sysrandom/randombytes_sysrandom.c
		${sodium_SOURCE_DIR}/src/libsodium/sodium/codecs.c
		${sodium_SOURCE_DIR}/src/libsodium/sodium/core.c
		${sodium_SOURCE_DIR}/src/libsodium/sodium/runtime.c
		${sodium_SOURCE_DIR}/src/libsodium/sodium/utils.c
		${sodium_SOURCE_DIR}/src/libsodium/sodium/version.c
	)
	target_include_directories(sodium PUBLIC
		${sodium_SOURCE_DIR}/src/libsodium/include
	)
	target_include_directories(sodium PRIVATE
		${sodium_SOURCE_DIR}/src/libsodium/include/sodium
	)
	target_compile_definitions(sodium PRIVATE CONFIGURED SODIUM_STATIC _CRT_SECURE_NO_WARNINGS)
	if(WIN32)
		target_link_libraries(sodium PRIVATE bcrypt advapi32)
	endif()

	# --- SQLite3 (amalgamation) ---
	FetchContent_Declare(sqlite3
		URL https://www.sqlite.org/2024/sqlite-amalgamation-3450300.zip
	)
	FetchContent_GetProperties(sqlite3)
	if(NOT sqlite3_POPULATED)
		FetchContent_Populate(sqlite3)
	endif()
	add_library(sqlite3 STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
	target_include_directories(sqlite3 PUBLIC ${sqlite3_SOURCE_DIR})
	target_compile_definitions(sqlite3 PRIVATE SQLITE_THREADSAFE=1 SQLITE_DEFAULT_MEMSTATUS=0 SQLITE_OMIT_DEPRECATED)

	# --- GLM ---
	FetchContent_Declare(glm
		GIT_REPOSITORY https://github.com/g-truc/glm.git
		GIT_TAG 1.0.1
		GIT_SHALLOW TRUE
	)
	FetchContent_MakeAvailable(glm)

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
	${glm_SOURCE_DIR}
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
	${glm_SOURCE_DIR}
)

target_link_libraries(VoxPlace PRIVATE voxplace_core)

target_link_libraries(VoxPlace PRIVATE
	PkgConfig::GLFW
	PkgConfig::ENET
	${OPENGL_gl_LIBRARY}
)

# Copy assets to output directory
add_custom_command(TARGET VoxPlace POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${CMAKE_SOURCE_DIR}/assets"
		"$<TARGET_FILE_DIR:VoxPlace>/assets"
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${CMAKE_SOURCE_DIR}/src/shader"
		"$<TARGET_FILE_DIR:VoxPlace>/shader"
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
	${glm_SOURCE_DIR}
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
	${glm_SOURCE_DIR}
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
