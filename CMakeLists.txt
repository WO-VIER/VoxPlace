cmake_minimum_required(VERSION 3.10)
project(VoxPlace VERSION 0.1.0)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED glfw3)
find_package(OpenGL REQUIRED)

add_executable(VoxPlace 
    src/main.cpp 
    src/glad.c
    )

target_include_directories(VoxPlace PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/dependencies
    ${GLFW_INCLUDE_DIRS}
)

target_link_libraries(VoxPlace PRIVATE
    ${GLFW_LIBRARIES}
    ${OPENGL_gl_LIBRARY}
)

add_definitions(${GLFW_CFLAGS_OTHER})