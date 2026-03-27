#ifndef CLIENT_RENDER_WORLD_RENDERER_H
#define CLIENT_RENDER_WORLD_RENDERER_H

#include <client/rendering/Camera.h>
#include <Chunk2.h>
#include <client/rendering/ChunkIndirectRenderer.h>
#include <client/rendering/Shader.h>
#include <client/rendering/RenderFrameContext.h>
#include <client/rendering/RenderSettings.h>

#include <glm/vec3.hpp>

#include <cstdint>
#include <unordered_map>
#include <vector>

struct ChunkDraw
{
	Chunk2 *chunk = nullptr;
	float distSq = 0.0f;
};

struct ChunkRebuildCandidate
{
	Chunk2 *chunk = nullptr;
	float distSq = 0.0f;
	bool inFrustum = false;
};

struct WorldVisibilitySet
{
	std::vector<ChunkDraw> visibleChunks;
	std::vector<ChunkRebuildCandidate> rebuildCandidates;
};

class WorldRenderer
{
public:
	static bool usesIndirectRendering(TerrainRenderArchitecture architecture);
	static float computeFarPlane(const RenderSettings &settings);
	static void computeFogRange(const RenderSettings &settings,
								float fogReferenceDistance,
								float &outFogStart,
								float &outFogEnd);
	static RenderFrameContext buildFrameContext(const Camera &camera,
												int framebufferWidth,
												int framebufferHeight,
												const RenderSettings &settings);
	static void configureShader(Shader &shader,
								const RenderFrameContext &frameContext,
								const glm::vec3 &fogColor,
								const glm::vec3 &cameraPosition,
								const RenderSettings &settings);
	static uint64_t computeTotalFaces(const std::unordered_map<int64_t, Chunk2 *> &chunkMap);
	static WorldVisibilitySet collectVisibility(const std::unordered_map<int64_t, Chunk2 *> &chunkMap,
											   const Camera &camera,
											   const RenderFrameContext &frameContext,
											   bool sortVisibleChunksFrontToBack);
	static float drawVisibleWorld(Shader &shader,
								  const WorldVisibilitySet &visibility,
								  ChunkIndirectRenderer &indirectRenderer,
								  TerrainRenderArchitecture architecture);
	static void rebuildIndirectArenaFromLoadedChunks(
		ChunkIndirectRenderer &indirectRenderer,
		const std::unordered_map<int64_t, Chunk2 *> &chunkMap);
	static void applyArchitectureSwitch(
		TerrainRenderArchitecture currentArchitecture,
		TerrainRenderArchitecture &previousArchitecture,
		ChunkIndirectRenderer &indirectRenderer,
		const std::unordered_map<int64_t, Chunk2 *> &chunkMap);
};

#endif
