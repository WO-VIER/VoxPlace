// GLAD doit être inclus en premier !
#if defined(__EMSCRIPTEN__) || defined(EMSCRIPTEN_WEB)
#include <GLFW/glfw3.h>
#include <emscripten.h>
#else
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#endif

#include <client/rendering/ClientFrameRenderer.h>

#include <client/core/InputSystem.h>
#include <client/gameplay/ChunkStreamingSystem.h>
#include <client/gameplay/MeshBuildSystem.h>

ClientFrameRenderResult ClientFrameRenderer::renderWorldFrame(
	GLFWwindow *window,
	GameState &gameState,
	Camera &camera,
	WorldClient &worldClient,
	ClientWorldState &worldState,
	Shader &chunkShader,
	ClientChunkMesher &chunkMesher,
	ChunkIndirectRenderer &chunkIndirectRenderer,
	const ClientFrameRendererConfig &config,
	const glm::vec3 &fogColor)
{
	ClientFrameRenderResult result;

	int framebufferWidth = 0;
	int framebufferHeight = 0;
	getFramebufferSize(window, gameState, framebufferWidth, framebufferHeight);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, framebufferWidth, framebufferHeight);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);

	result.frameContext = WorldRenderer::buildFrameContext(
		camera,
		framebufferWidth,
		framebufferHeight,
		gameState.render);
	WorldRenderer::configureShader(
		chunkShader,
		result.frameContext,
		fogColor,
		camera.Position,
		gameState.render);

	ChunkStreamingSystem::syncChunkStreaming(
		worldClient,
		camera,
		result.frameContext.frustum,
		worldState.hasWorldFrontier,
		worldState.frontier,
		gameState.render.renderDistanceChunks,
		gameState.render.classicStreamingPaddingChunks,
		config.classicMaxInflightChunkRequests,
		config.classicMaxChunkRequestsPerFrame,
		worldState.streamedChunkKeys,
		worldState.chunkMap,
		worldState.profileChunkRequestsWindow,
		worldState.profileChunkDropsWindow,
		[&](int64_t key)
		{
			MeshBuildSystem::removeClientChunkByKey(
				key,
				worldState.chunkMap,
				worldState.pendingMeshRevisions,
				chunkIndirectRenderer);
		});

	result.visibility = WorldRenderer::collectVisibility(
		worldState.chunkMap,
		camera,
		result.frameContext,
		config.sortVisibleChunksFrontToBack);
	MeshBuildSystem::scheduleMeshBuilds(
		result.visibility,
		worldState.chunkMap,
		worldState.pendingMeshRevisions,
		chunkMesher);

	result.visibleChunkCount = result.visibility.visibleChunks.size();
	result.visibleChunks = static_cast<int>(result.visibleChunkCount);
	result.totalFaces = WorldRenderer::computeTotalFaces(worldState.chunkMap);
	result.chunkRenderCpuMs = WorldRenderer::drawVisibleWorld(
		chunkShader,
		result.visibility,
		chunkIndirectRenderer,
		gameState.terrainRenderArchitecture);

	return result;
}
