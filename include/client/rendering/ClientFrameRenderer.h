#ifndef CLIENT_RENDERING_CLIENT_FRAME_RENDERER_H
#define CLIENT_RENDERING_CLIENT_FRAME_RENDERER_H

#include <client/core/GameState.h>
#include <client/gameplay/ClientWorldState.h>
#include <client/rendering/Camera.h>
#include <client/rendering/ChunkIndirectRenderer.h>
#include <client/rendering/ClientChunkMesher.h>
#include <client/rendering/Shader.h>
#include <client/rendering/WorldRenderer.h>
#include <WorldClient.h>

#include <GLFW/glfw3.h>

#include <cstddef>
#include <cstdint>

struct ClientFrameRendererConfig
{
	size_t classicMaxInflightChunkRequests = 192;
	size_t classicMaxChunkRequestsPerFrame = 16;
	bool sortVisibleChunksFrontToBack = true;
};

struct ClientFrameRenderResult
{
	RenderFrameContext frameContext;
	WorldVisibilitySet visibility;
	size_t visibleChunkCount = 0;
	int visibleChunks = 0;
	uint64_t totalFaces = 0;
	float chunkRenderCpuMs = 0.0f;
};

class ClientFrameRenderer
{
public:
	static ClientFrameRenderResult renderWorldFrame(
		GLFWwindow *window,
		GameState &gameState,
		Camera &camera,
		WorldClient &worldClient,
		ClientWorldState &worldState,
		Shader &chunkShader,
		ClientChunkMesher &chunkMesher,
		ChunkIndirectRenderer &chunkIndirectRenderer,
		const ClientFrameRendererConfig &config,
		const glm::vec3 &fogColor,
		float deltaTime);
};

#endif
