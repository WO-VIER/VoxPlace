#ifndef CLIENT_UI_PROFILER_WINDOWS_H
#define CLIENT_UI_PROFILER_WINDOWS_H

#include <client/core/ClientProfiler.h>
#include <client/core/GameState.h>
#include <client/gameplay/ClientWorldState.h>
#include <client/rendering/Camera.h>
#include <client/rendering/RenderFrameContext.h>

#include <GLFW/glfw3.h>

struct ProfilerWindowsData
{
	const GameState *gameState = nullptr;
	const ClientWorldState *worldState = nullptr;
	const ClientProfilerState *clientProfiler = nullptr;
	const Camera *camera = nullptr;
	const RenderFrameContext *frameContext = nullptr;
};

void updateProfilerWindowsToggle(GLFWwindow *window, bool inGame, bool &visible);
void renderProfilerWindows(bool visible, const ProfilerWindowsData &data);

#endif
