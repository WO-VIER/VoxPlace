#ifndef CLIENT_GAMEPLAY_CLIENT_WORLD_SYSTEM_H
#define CLIENT_GAMEPLAY_CLIENT_WORLD_SYSTEM_H

#include <client/rendering/ChunkIndirectRenderer.h>
#include <WorldClient.h>
#include <client/core/GameState.h>
#include <client/gameplay/ClientWorldState.h>
#include <client/ui/LoginScreen.h>

#include <GLFW/glfw3.h>

class ClientWorldSystem
{
public:
	static void clear(ClientWorldState &worldState, ChunkIndirectRenderer &indirectRenderer);
	static void handleEvents(WorldClient &worldClient,
							 ClientWorldState &worldState,
							 ChunkIndirectRenderer &indirectRenderer,
							 LoginScreen &loginScreen,
							 GLFWwindow *window,
							 GameState &gameState,
							 Camera &camera);
};

#endif
