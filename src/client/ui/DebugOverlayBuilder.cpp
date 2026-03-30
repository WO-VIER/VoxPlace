#include <client/ui/DebugOverlayBuilder.h>

#include <ChunkPalette.h>
#include <client/core/ClientLaunch.h>
#include <client/gameplay/CameraBoundsSystem.h>
#include <client/gameplay/ChunkStreamingSystem.h>
#include <client/rendering/WorldRenderer.h>

DebugOverlayData buildDebugOverlayData(const DebugOverlayBuildInputs &inputs)
{
	DebugOverlayData data;
	if (inputs.gameState == nullptr ||
		inputs.worldClient == nullptr ||
		inputs.worldState == nullptr ||
		inputs.camera == nullptr ||
		inputs.chunkIndirectRenderer == nullptr ||
		inputs.chunkMesher == nullptr ||
		inputs.crosshair == nullptr ||
		inputs.frameContext == nullptr)
	{
		return data;
	}

	const GameState &gameState = *inputs.gameState;
	const WorldClient &worldClient = *inputs.worldClient;
	const ClientWorldState &worldState = *inputs.worldState;
	const Camera &camera = *inputs.camera;
	const ChunkIndirectRenderer &chunkIndirectRenderer = *inputs.chunkIndirectRenderer;
	const ClientChunkMesher &chunkMesher = *inputs.chunkMesher;
	const Crosshair &crosshair = *inputs.crosshair;
	const RenderFrameContext &frameContext = *inputs.frameContext;

	const uint32_t selectedColor = playerPaletteColor(
		static_cast<uint8_t>(gameState.render.selectedPaletteIndex - 1));
	const ImVec4 previewColor(
		static_cast<float>(VoxelChunkData::colorR(selectedColor)) / 255.0f,
		static_cast<float>(VoxelChunkData::colorG(selectedColor)) / 255.0f,
		static_cast<float>(VoxelChunkData::colorB(selectedColor)) / 255.0f,
		1.0f);
	const bool usingIndirectRendering = WorldRenderer::usesIndirectRendering(
		gameState.terrainRenderArchitecture);

	data.deltaTime = inputs.deltaTime;
	data.serverHost = gameState.connection.serverHost.c_str();
	data.serverPort = static_cast<int>(gameState.connection.serverPort);
	data.username = worldClient.localPlayer().profile.username.c_str();
	data.connected = worldClient.isConnected();
	data.hasWorldFrontier = worldState.hasWorldFrontier;
	data.frontier = &worldState.frontier;
	data.visibleChunks = inputs.visibleChunks;
	data.loadedChunkCount = worldState.chunkMap.size();
	data.chunkRenderCpuMs = inputs.chunkRenderCpuMs;
	data.usesIndirectRendering = usingIndirectRendering;
	data.cpuDrawCalls = usingIndirectRendering
							 ? (chunkIndirectRenderer.drawCount > 0 ? 1 : 0)
							 : inputs.visibleChunks;
	data.indirectDrawCount = chunkIndirectRenderer.drawCount;
	data.indirectFaceCount = chunkIndirectRenderer.faceCount;
	data.indirectReused = chunkIndirectRenderer.lastBuildReused;
	data.indirectBuildCount = chunkIndirectRenderer.drawDataBuildCount;
	data.indirectArenaReservedMb =
		static_cast<float>(chunkIndirectRenderer.arenaReservedFaces * sizeof(ChunkFaceInstanceGpu)) / (1024.0f * 1024.0f);
	data.indirectArenaCapacityMb =
		static_cast<float>(chunkIndirectRenderer.arenaFaceCapacity * sizeof(ChunkFaceInstanceGpu)) / (1024.0f * 1024.0f);
	data.indirectArenaUsedFaces = chunkIndirectRenderer.arenaUsedFaces;
	data.indirectLargestFreeSpan = chunkIndirectRenderer.largestFreeFaceSpan;
	data.compactArenaRequested = inputs.compactArenaRequested;
	data.meshWorkerCount = chunkMesher.workerCount();
	data.trackedJobs = worldState.pendingMeshRevisions.size();
	data.queuedJobs = chunkMesher.pendingJobCount();
	data.readyJobs = chunkMesher.completedJobCount();
	data.totalFaces = inputs.totalFaces;
	data.renderDistanceChunks = const_cast<int *>(&gameState.render.renderDistanceChunks);
	data.farPlane = frameContext.farPlane;
	data.limitToPlayableWorld = const_cast<bool *>(&gameState.render.limitToPlayableWorld);
	data.usesClassicStreaming = ChunkStreamingSystem::usesClassicStreaming(
		worldState.hasWorldFrontier,
		worldState.frontier);
	data.classicStreamingPaddingChunks = const_cast<int *>(&gameState.render.classicStreamingPaddingChunks);
	data.cameraX = camera.Position.x;
	data.cameraY = camera.Position.y;
	data.cameraZ = camera.Position.z;
	data.headingText = cameraHeadingCardinal(camera.Front);
	data.cameraSpeed = const_cast<float *>(&camera.MovementSpeed);
	data.minecraftFogByRenderDistance = const_cast<bool *>(&gameState.render.minecraftFogByRenderDistance);
	data.minecraftFogStartPercent = const_cast<float *>(&gameState.render.minecraftFogStartPercent);
	data.fogStart = const_cast<float *>(&gameState.render.fogStart);
	data.fogEnd = const_cast<float *>(&gameState.render.fogEnd);
	data.fogStartFrame = frameContext.fogStart;
	data.fogEndFrame = frameContext.fogEnd;
	data.useAO = const_cast<bool *>(&gameState.render.useAO);
	data.debugSunblockOnly = const_cast<bool *>(&gameState.render.debugSunblockOnly);
	data.terrainArchitectureName = terrainRenderArchitectureName(gameState.terrainRenderArchitecture);
	data.terrainArchitectureIndex = inputs.terrainArchitectureIndex;
	data.terrainArchitectureMin = 0;
	data.terrainArchitectureMax = 1;
	data.selectedPaletteIndex = const_cast<int *>(&gameState.render.selectedPaletteIndex);
	data.paletteMax = static_cast<int>(PLAYER_COLOR_PALETTE_SIZE);
	data.previewColor = previewColor;
	data.crosshairVisible = const_cast<bool *>(&crosshair.visible);
	return data;
}
