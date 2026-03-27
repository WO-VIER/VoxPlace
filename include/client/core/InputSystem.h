#ifndef CLIENT_CORE_INPUT_SYSTEM_H
#define CLIENT_CORE_INPUT_SYSTEM_H

#include <client/rendering/Camera.h>
#include <client/core/GameState.h>

#include <GLFW/glfw3.h>

void installClientInputCallbacks(GLFWwindow *window, GameState &gameState, Camera &camera);
void processGameplayInput(GLFWwindow *window, GameState &gameState, Camera &camera, float deltaTime);
void getFramebufferSize(GLFWwindow *window, const GameState &gameState, int &outWidth, int &outHeight);
float framebufferAspectRatio(GLFWwindow *window, const GameState &gameState);

#endif
