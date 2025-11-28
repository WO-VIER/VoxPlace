#pragma once
#include <iostream>
#if defined(__EMSCRIPTEN__) || defined(EMSCRIPTEN_WEB)
#include <GLFW/glfw3.h>
#else
#include <glad/glad.h>
#include <GL/gl.h>
#include <GLFW/glfw3.h>
#endif

//iostream
//glfw3 Cross plateform window lib
//glad Opengl 