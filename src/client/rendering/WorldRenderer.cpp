#include <client/rendering/WorldRenderer.h>

#include <algorithm>
#include <chrono>

namespace
{
	float chunkCenterDistanceSqToCamera(const Camera &camera, int chunkX, int chunkZ)
	{
		float centerX = chunkX * CHUNK_SIZE_X + CHUNK_SIZE_X * 0.5f;
		float centerZ = chunkZ * CHUNK_SIZE_Z + CHUNK_SIZE_Z * 0.5f;
		float dx = centerX - camera.Position.x;
		float dz = centerZ - camera.Position.z;
		return dx * dx + dz * dz;
	}
}

bool WorldRenderer::usesIndirectRendering(TerrainRenderArchitecture architecture)
{
	return architecture == TerrainRenderArchitecture::BigGpuBufferIndirect;
}

float WorldRenderer::computeFarPlane(const RenderSettings &settings)
{
	float farPlane = static_cast<float>(settings.renderDistanceChunks * CHUNK_SIZE_X + CHUNK_SIZE_X * 2);
	if (farPlane < 64.0f)
	{
		farPlane = 64.0f;
	}
	return farPlane;
}

void WorldRenderer::computeFogRange(const RenderSettings &settings,
									float fogReferenceDistance,
									float &outFogStart,
									float &outFogEnd)
{
	if (!settings.minecraftFogByRenderDistance)
	{
		outFogStart = settings.fogStart;
		outFogEnd = settings.fogEnd;
		if (outFogEnd <= outFogStart + 0.1f)
		{
			outFogEnd = outFogStart + 0.1f;
		}
		return;
	}

	float startPercent = settings.minecraftFogStartPercent;
	if (startPercent < 0.05f)
	{
		startPercent = 0.05f;
	}
	if (startPercent > 0.98f)
	{
		startPercent = 0.98f;
	}

	outFogStart = fogReferenceDistance * startPercent;
	outFogEnd = fogReferenceDistance;
	if (outFogEnd <= outFogStart + 0.1f)
	{
		outFogEnd = outFogStart + 0.1f;
	}
}

RenderFrameContext WorldRenderer::buildFrameContext(const Camera &camera,
													int framebufferWidth,
													int framebufferHeight,
													const RenderSettings &settings)
{
	RenderFrameContext frameContext;
	frameContext.framebufferWidth = framebufferWidth;
	frameContext.framebufferHeight = framebufferHeight;
	frameContext.farPlane = computeFarPlane(settings);

	float fogReferenceDistance = static_cast<float>(settings.renderDistanceChunks * CHUNK_SIZE_X);
	if (fogReferenceDistance < static_cast<float>(CHUNK_SIZE_X))
	{
		fogReferenceDistance = static_cast<float>(CHUNK_SIZE_X);
	}
	fogReferenceDistance = frameContext.farPlane;
	computeFogRange(settings, fogReferenceDistance, frameContext.fogStart, frameContext.fogEnd);

	frameContext.projection = glm::perspective(
		glm::radians(camera.Zoom),
		static_cast<float>(framebufferWidth) / static_cast<float>(framebufferHeight),
		0.1f,
		frameContext.farPlane);
	Camera cameraCopy = camera;
	frameContext.view = cameraCopy.GetViewMatrix();
	frameContext.frustum.extractFromVP(frameContext.projection * frameContext.view);
	return frameContext;
}

void WorldRenderer::configureShader(Shader &shader,
									const RenderFrameContext &frameContext,
									const glm::vec3 &fogColor,
									const glm::vec3 &cameraPosition,
									const RenderSettings &settings)
{
	shader.use();
	shader.setMat4("projection", frameContext.projection);
	shader.setMat4("view", frameContext.view);
	shader.setFloat("fogStart", frameContext.fogStart);
	shader.setFloat("fogEnd", frameContext.fogEnd);
	shader.setVec3("fogColor", fogColor);
	shader.setVec3("cameraPos", cameraPosition);
	shader.setInt("useAO", settings.useAO ? 1 : 0);
	shader.setInt("debugSunblockOnly", settings.debugSunblockOnly ? 1 : 0);
	shader.setInt("useIndirectDraw", 0);
}

uint64_t WorldRenderer::computeTotalFaces(const std::unordered_map<int64_t, ClientChunk *> &chunkMap)
{
	uint64_t totalFaces = 0;
	for (const auto &[key, chunk] : chunkMap)
	{
		totalFaces += chunk->renderState.faceCount;
}
	return totalFaces;
}

WorldVisibilitySet WorldRenderer::collectVisibility(const std::unordered_map<int64_t, ClientChunk *> &chunkMap,
													const Camera &camera,
													const RenderFrameContext &frameContext,
													bool sortVisibleChunksFrontToBack)
{
	WorldVisibilitySet result;
	result.visibleChunks.reserve(chunkMap.size());
	result.rebuildCandidates.reserve(chunkMap.size());

	for (const auto &[key, chunk] : chunkMap)
	{
		float distSq = chunkCenterDistanceSqToCamera(camera, chunk->storage.chunkX, chunk->storage.chunkZ);
		bool inFrustum = frameContext.frustum.isChunkVisible(chunk->storage.chunkX, chunk->storage.chunkZ);

		if (chunk->renderState.needsMeshRebuild)
		{
			ChunkRebuildCandidate rebuildCandidate;
			rebuildCandidate.chunk = chunk;
			rebuildCandidate.distSq = distSq;
			rebuildCandidate.inFrustum = inFrustum;
			result.rebuildCandidates.push_back(rebuildCandidate);
		}

		if (!inFrustum)
		{
			continue;
		}

		ChunkDraw draw;
		draw.chunk = chunk;
		draw.distSq = distSq;
		result.visibleChunks.push_back(draw);
	}

	std::sort(result.rebuildCandidates.begin(), result.rebuildCandidates.end(),
			  [](const ChunkRebuildCandidate &left, const ChunkRebuildCandidate &right)
			  {
				  if (left.inFrustum != right.inFrustum)
				  {
					  return left.inFrustum > right.inFrustum;
				  }
				  return left.distSq < right.distSq;
			  });

	if (sortVisibleChunksFrontToBack)
	{
		std::sort(result.visibleChunks.begin(), result.visibleChunks.end(),
				  [](const ChunkDraw &left, const ChunkDraw &right)
				  { return left.distSq < right.distSq; });
	}

	return result;
}

float WorldRenderer::drawVisibleWorld(Shader &shader,
									  const WorldVisibilitySet &visibility,
									  ChunkIndirectRenderer &indirectRenderer,
									  TerrainRenderArchitecture architecture)
{
	auto renderStart = std::chrono::steady_clock::now();

	bool useIndirectRendering = usesIndirectRendering(architecture);
	if (useIndirectRendering)
	{
		std::vector<ClientChunk *> visibleChunksForIndirect;
		visibleChunksForIndirect.reserve(visibility.visibleChunks.size());
		for (const ChunkDraw &draw : visibility.visibleChunks)
		{
			visibleChunksForIndirect.push_back(draw.chunk);
		}

		indirectRenderer.buildVisibleDrawData(visibleChunksForIndirect);
		shader.setInt("useIndirectDraw", 1);
		indirectRenderer.draw();
		shader.setInt("useIndirectDraw", 0);
	}
	else
	{
		for (const ChunkDraw &draw : visibility.visibleChunks)
		{
			shader.setVec3("chunkPos", glm::vec3(
										 static_cast<float>(draw.chunk->storage.chunkX * CHUNK_SIZE_X),
										 0.0f,
										 static_cast<float>(draw.chunk->storage.chunkZ * CHUNK_SIZE_Z)));
			draw.chunk->render();
		}
	}

	auto renderEnd = std::chrono::steady_clock::now();
	return std::chrono::duration<float, std::milli>(renderEnd - renderStart).count();
}

void WorldRenderer::rebuildIndirectArenaFromLoadedChunks(
	ChunkIndirectRenderer &indirectRenderer,
	const std::unordered_map<int64_t, ClientChunk *> &chunkMap)
{
	indirectRenderer.cleanup();
	indirectRenderer.init();

	for (const auto &[key, chunk] : chunkMap)
	{
		if (chunk == nullptr)
		{
			continue;
		}
		indirectRenderer.upsertChunk(*chunk);
	}
}

void WorldRenderer::applyArchitectureSwitch(
	TerrainRenderArchitecture currentArchitecture,
	TerrainRenderArchitecture &previousArchitecture,
	ChunkIndirectRenderer &indirectRenderer,
	const std::unordered_map<int64_t, ClientChunk *> &chunkMap)
{
	if (currentArchitecture == previousArchitecture)
	{
		return;
	}

	if (currentArchitecture == TerrainRenderArchitecture::BigGpuBufferIndirect)
	{
		rebuildIndirectArenaFromLoadedChunks(indirectRenderer, chunkMap);
	}
	previousArchitecture = currentArchitecture;
}
