#ifndef DEBUG_OVERLAY_H
#define DEBUG_OVERLAY_H

#include <WorldBounds.h>

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <cstddef>
#include <cstdint>

struct DebugOverlayData
{
	float deltaTime = 0.0f;
	const char *serverHost = "";
	int serverPort = 0;
	uint32_t roundTripTimeMs = 0;
	const char *username = "";
	bool isAdmin = false;
	bool connected = false;
	bool hasWorldFrontier = false;
	const WorldFrontier *frontier = nullptr;
	int visibleChunks = 0;
	size_t loadedChunkCount = 0;
	float chunkRenderCpuMs = 0.0f;
	bool usesIndirectRendering = false;
	int cpuDrawCalls = 0;
	size_t indirectDrawCount = 0;
	size_t indirectFaceCount = 0;
	bool indirectReused = false;
	uint64_t indirectBuildCount = 0;
	float indirectArenaReservedMb = 0.0f;
	float indirectArenaCapacityMb = 0.0f;
	size_t indirectArenaUsedFaces = 0;
	size_t indirectLargestFreeSpan = 0;
	bool *compactArenaRequested = nullptr;
	bool *resetExpansionCooldownRequested = nullptr;
	bool *resetBlockCooldownRequested = nullptr;
	bool *toggleBlockCooldownRequested = nullptr;
	bool blockCooldownDisabled = false;
	size_t meshWorkerCount = 0;
	size_t trackedJobs = 0;
	size_t queuedJobs = 0;
	size_t readyJobs = 0;
	uint64_t totalFaces = 0;
	int *renderDistanceChunks = nullptr;
	float farPlane = 0.0f;
	bool *limitToPlayableWorld = nullptr;
	bool usesClassicStreaming = false;
	int *classicStreamingPaddingChunks = nullptr;
	float cameraX = 0.0f;
	float cameraY = 0.0f;
	float cameraZ = 0.0f;
	const char *headingText = "";
	float *cameraSpeed = nullptr;
	bool *minecraftFogByRenderDistance = nullptr;
	float *minecraftFogStartPercent = nullptr;
	float *fogStart = nullptr;
	float *fogEnd = nullptr;
	float fogStartFrame = 0.0f;
	float fogEndFrame = 0.0f;
	bool *useAO = nullptr;
	bool *debugSunblockOnly = nullptr;
	const char *terrainArchitectureName = "";
	int *terrainArchitectureIndex = nullptr;
	int terrainArchitectureMin = 0;
	int terrainArchitectureMax = 0;
	bool *crosshairVisible = nullptr;
};

void updateDebugOverlayToggle(GLFWwindow *window, bool inGame, bool &visible);
void renderDebugOverlay(bool visible, const DebugOverlayData &data);

#endif
