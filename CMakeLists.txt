cmake_minimum_required(VERSION 3.10)
project(VoxPlace VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ENABLE_GL_DEBUG "Enable OpenGL debug output for native builds" ON)
option(ENABLE_ASAN "Enable Address Sanitizer for debugging" OFF)

if(NOT EMSCRIPTEN)
    # Disable optimizations and warnings for a raw build
    add_compile_options(-O0 -w)

    if(ENABLE_ASAN)
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g3)
        add_link_options(-fsanitize=address)
    endif()
endif()
# Native vs Emscripten handling
if(NOT EMSCRIPTEN)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GLFW REQUIRED glfw3)
    set(OpenGL_GL_PREFERENCE "GLVND")
    find_package(OpenGL REQUIRED)
else()
    # For Emscripten builds we won't use system pkg-config glfw3.
    # Use Emscripten ports via linker flags below instead.
    set(GLFW_LIBRARIES "")
    set(GLFW_INCLUDE_DIRS "")
    set(OPENGL_gl_LIBRARY "")
endif()

# sources
set(SOURCES
    src/main3.cpp
    src/profiler.cpp
    # ImGui core
    thirdparty/imgui/imgui.cpp
    thirdparty/imgui/imgui_draw.cpp
    thirdparty/imgui/imgui_tables.cpp
    thirdparty/imgui/imgui_widgets.cpp
    thirdparty/imgui/imgui_demo.cpp
    # ImGui backends (GLFW + OpenGL3)
    thirdparty/imgui/backends/imgui_impl_glfw.cpp
    thirdparty/imgui/backends/imgui_impl_opengl3.cpp
)
if(NOT EMSCRIPTEN)
    list(APPEND SOURCES src/glad.c)
    if(ENABLE_GL_DEBUG)
        list(APPEND SOURCES src/errorReporting.cpp)
    endif()
endif()

add_executable(VoxPlace ${SOURCES})

if(NOT EMSCRIPTEN)
    if(ENABLE_GL_DEBUG)
        target_compile_definitions(VoxPlace PRIVATE ENABLE_GL_DEBUG)
    endif()
endif()

# includes
target_include_directories(VoxPlace PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/dependencies
    ${CMAKE_SOURCE_DIR}/thirdparty/FastNoiseLite/Cpp
    ${CMAKE_SOURCE_DIR}/thirdparty/imgui
    ${CMAKE_SOURCE_DIR}/thirdparty/imgui/backends
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