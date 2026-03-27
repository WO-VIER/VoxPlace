#ifndef CLIENT_UI_DEBUG_OVERLAY_BUILDER_H
#define CLIENT_UI_DEBUG_OVERLAY_BUILDER_H

#include <client/core/GameState.h>
#include <client/gameplay/ClientWorldState.h>
#include <client/rendering/Camera.h>
#include <client/rendering/ChunkIndirectRenderer.h>
#include <client/rendering/ClientChunkMesher.h>
#include <client/rendering/Crosshair.h>
#include <client/rendering/RenderFrameContext.h>
#include <client/ui/DebugOverlay.h>
#include <WorldClient.h>

#include <cstdint>

struct DebugOverlayBuildInputs
{
	const GameState *gameState = nullptr;
	const WorldClient *worldClient = nullptr;
	const ClientWorldState *worldState = nullptr;
	const Camera *camera = nullptr;
	const ChunkIndirectRenderer *chunkIndirectRenderer = nullptr;
	const ClientChunkMesher *chunkMesher = nullptr;
	const Crosshair *crosshair = nullptr;
	const RenderFrameContext *frameContext = nullptr;
	float deltaTime = 0.0f;
	float chunkRenderCpuMs = 0.0f;
	int visibleChunks = 0;
	uint64_t totalFaces = 0;
	int *terrainArchitectureIndex = nullptr;
	bool *compactArenaRequested = nullptr;
};

DebugOverlayData buildDebugOverlayData(const DebugOverlayBuildInputs &inputs);

#endif
