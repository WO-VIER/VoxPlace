cmake_minimum_required(VERSION 3.10)
project(VoxPlace VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ENABLE_GL_DEBUG "Enable OpenGL debug output for native builds" ON)
# Native vs Emscripten handling
if(NOT EMSCRIPTEN)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GLFW REQUIRED glfw3)
    find_package(OpenGL REQUIRED)
else()
    # For Emscripten builds we won't use system pkg-config glfw3.
    # Use Emscripten ports via linker flags below instead.
    set(GLFW_LIBRARIES "")
    set(GLFW_INCLUDE_DIRS "")
    set(OPENGL_gl_LIBRARY "")
endif()

# sources: exclude glad for Emscripten (glad can cause symbol issues on WASM)
set(SOURCES
    src/main.cpp
)
if(NOT EMSCRIPTEN)
    list(APPEND SOURCES src/glad.c)
    # Only include errorReporting if the GL headers provide the debug symbols (GL_DEBUG_OUTPUT or GL_KHR_debug or GL_VERSION_4_3)
    include(CheckCSourceCompiles)
    set(CMAKE_REQUIRED_INCLUDES ${CMAKE_SOURCE_DIR}/src)
    check_c_source_compiles(
        "#include <glad/glad.h>\n#ifdef GL_DEBUG_OUTPUT\nint main() { return 0; }\n#else\n#error no\n#endif\n"
        HAVE_GL_DEBUG_SUPPORT
    )
    if(HAVE_GL_DEBUG_SUPPORT)
        list(APPEND SOURCES src/errorReporting.cpp)
    endif()
endif()

add_executable(VoxPlace ${SOURCES})

if(NOT EMSCRIPTEN)
    if(ENABLE_GL_DEBUG)
        target_compile_definitions(VoxPlace PRIVATE ENABLE_GL_DEBUG)
    endif()
    if(HAVE_GL_DEBUG_SUPPORT)
        target_compile_definitions(VoxPlace PRIVATE HAVE_GLREPORT)
    endif()
endif()

# includes
target_include_directories(VoxPlace PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/dependencies
    ${GLFW_INCLUDE_DIRS}
)

if(EMSCRIPTEN)
    # build for web: prefer WebGL2 and use the GLFW port
    set_target_properties(VoxPlace PROPERTIES SUFFIX ".html")
    target_compile_definitions(VoxPlace PRIVATE EMSCRIPTEN_WEB)
    target_link_options(VoxPlace PRIVATE
        "-sUSE_WEBGL2=1"
        "-sFULL_ES3=1"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sWASM=1"
        "-sUSE_GLFW=3"
    )
else()
    target_link_libraries(VoxPlace PRIVATE
        ${GLFW_LIBRARIES}
        ${OPENGL_gl_LIBRARY}
    )
    add_definitions(${GLFW_CFLAGS_OTHER})
endif()