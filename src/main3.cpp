// GLAD doit être inclus en premier !
#if defined(__EMSCRIPTEN__) || defined(EMSCRIPTEN_WEB)
#include <GLFW/glfw3.h>
#include <emscripten.h>
#else
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#endif

#include <Camera.h>
#include <ClientChunkMesher.h>
#include <Chunk2.h>
#include <ChunkIndirectRenderer.h>
#include <ChunkPalette.h>
#include <Crosshair.h>
#include <Frustum.h>
#include <LowResRenderer.h>
#include <Shader.h>
#include <WorldBounds.h>
#include <WorldClient.h>
#include <config.h>

#include <glm/ext/matrix_transform.hpp>
#include <glm/fwd.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

namespace
{
	constexpr int SCREEN_WIDTH = 1920;
	constexpr int SCREEN_HEIGHT = 1080;
	constexpr float PLACE_REACH = 8.0f;
	constexpr uint16_t SERVER_PORT = 28713;
	constexpr const char *SERVER_HOST = "127.0.0.1";

	enum class TerrainRenderArchitecture
	{
		ChunkSsboDirect = 0,
		BigGpuBufferIndirect = 1
	};

	const char *terrainRenderArchitectureName(TerrainRenderArchitecture architecture)
	{
		switch (architecture)
		{
		case TerrainRenderArchitecture::ChunkSsboDirect:
			return "Chunk SSBO Direct";
		case TerrainRenderArchitecture::BigGpuBufferIndirect:
			return "Big GPU Buffer Indirect";
		}
		return "Unknown";
	}

	bool envFlagEnabled(const char *name)
	{
		const char *value = std::getenv(name);
		if (value == nullptr)
		{
			return false;
		}
		if (value[0] == '\0')
		{
			return false;
		}
		if (value[0] == '0' && value[1] == '\0')
		{
			return false;
		}
		return true;
	}

	bool tryReadEnvInt(const char *name, int &value)
	{
		const char *rawValue = std::getenv(name);
		if (rawValue == nullptr)
		{
			return false;
		}
		if (rawValue[0] == '\0')
		{
			return false;
		}

		char *end = nullptr;
		long parsed = std::strtol(rawValue, &end, 10);
		if (end == rawValue)
		{
			return false;
		}
		if (end == nullptr || *end != '\0')
		{
			return false;
		}

		value = static_cast<int>(parsed);
		return true;
	}

	bool tryReadEnvFloat(const char *name, float &value)
	{
		const char *rawValue = std::getenv(name);
		if (rawValue == nullptr)
		{
			return false;
		}
		if (rawValue[0] == '\0')
		{
			return false;
		}

		char *end = nullptr;
		float parsed = std::strtof(rawValue, &end);
		if (end == rawValue)
		{
			return false;
		}
		if (end == nullptr || *end != '\0')
		{
			return false;
		}

		value = parsed;
		return true;
	}
}

float fogStart = 80.0f;
float fogEnd = 200.0f;
glm::vec3 FOG_COLOR = glm::vec3(0.6f, 0.7f, 0.9f);
bool minecraftFogByRenderDistance = true;
float minecraftFogStartPercent = 0.75f;

int renderDistanceChunks = 12;
bool limitToPlayableWorld = true;
int classicStreamingPaddingChunks = 4;

bool useAO = true;
bool debugSunblockOnly = false;
TerrainRenderArchitecture gTerrainRenderArchitecture = TerrainRenderArchitecture::ChunkSsboDirect;
TerrainRenderArchitecture gPreviousTerrainRenderArchitecture = TerrainRenderArchitecture::ChunkSsboDirect;
int selectedPaletteIndex = 32;
Crosshair crosshair;
float chunkRenderCpuMs = 0.0f;

GLFWwindow *g_window = nullptr;
Camera camera(glm::vec3(0.0f, 35.0f, 0.0f));
float lastX = SCREEN_WIDTH / 2.0f;
float lastY = SCREEN_HEIGHT / 2.0f;
bool firstMouse = true;
float deltaTime = 0.0f;
float lastFrame = 0.0f;
bool gPlaceBlockRequested = false;
bool gBreakBlockRequested = false;

WorldClient gWorldClient;
WorldFrontier gWorldFrontier;
bool gHasWorldFrontier = false;
ClientChunkMesher gChunkMesher;
ChunkIndirectRenderer gChunkIndirectRenderer;

std::unordered_map<int64_t, Chunk2 *> chunkMap;
std::unordered_set<int64_t> streamedChunkKeys;
std::unordered_map<int64_t, uint64_t> gPendingMeshRevisions;
size_t gProfileChunkRequestsWindow = 0;
size_t gProfileChunkDropsWindow = 0;
size_t gProfileChunkReceivesWindow = 0;

bool usesIndirectRendering()
{
	if (gTerrainRenderArchitecture == TerrainRenderArchitecture::BigGpuBufferIndirect)
	{
		return true;
	}
	return false;
}

void rebuildIndirectArenaFromLoadedChunks()
{
	gChunkIndirectRenderer.cleanup();
	gChunkIndirectRenderer.init();

	for (auto &[key, chunk] : chunkMap)
	{
		if (chunk == nullptr)
		{
			continue;
		}
		gChunkIndirectRenderer.upsertChunk(*chunk);
	}
}

const char *cameraHeadingCardinal(const glm::vec3 &front)
{
	float ax = std::abs(front.x);
	float az = std::abs(front.z);
	if (ax >= az)
	{
		if (front.x < 0.0f)
		{
			return "West (-X)";
		}
		return "East (+X)";
	}
	if (front.z < 0.0f)
	{
		return "South (-Z)";
	}
	return "North (+Z)";
}

float chunkBoundsMinX(const ChunkBounds &bounds)
{
	return static_cast<float>(bounds.minChunkX * CHUNK_SIZE_X);
}

float chunkBoundsMaxX(const ChunkBounds &bounds)
{
	return static_cast<float>(bounds.maxChunkXExclusive * CHUNK_SIZE_X);
}

float chunkBoundsMinZ(const ChunkBounds &bounds)
{
	return static_cast<float>(bounds.minChunkZ * CHUNK_SIZE_Z);
}

float chunkBoundsMaxZ(const ChunkBounds &bounds)
{
	return static_cast<float>(bounds.maxChunkZExclusive * CHUNK_SIZE_Z);
}

float computeRenderFarPlane()
{
	float farPlane = static_cast<float>(renderDistanceChunks * CHUNK_SIZE_X + CHUNK_SIZE_X * 2);
	if (farPlane < 64.0f)
	{
		farPlane = 64.0f;
	}
	return farPlane;
}

float chunkCenterDistanceSqToCamera(int chunkX, int chunkZ)
{
	float centerX = chunkX * CHUNK_SIZE_X + CHUNK_SIZE_X * 0.5f;
	float centerZ = chunkZ * CHUNK_SIZE_Z + CHUNK_SIZE_Z * 0.5f;
	float dx = centerX - camera.Position.x;
	float dz = centerZ - camera.Position.z;
	return dx * dx + dz * dz;
}

bool usesClassicStreaming()
{
	if (!gHasWorldFrontier)
	{
		return false;
	}
	if (gWorldFrontier.mode == WorldGenerationMode::ClassicStreaming)
	{
		return true;
	}
	return false;
}

bool canStreamChunk(int chunkX, int chunkZ)
{
	if (usesClassicStreaming())
	{
		return true;
	}
	return gWorldFrontier.generatedBounds.containsChunk(chunkX, chunkZ);
}

void clampCameraToPlayableWorld()
{
	if (!limitToPlayableWorld || !gHasWorldFrontier || usesClassicStreaming())
	{
		return;
	}

	const ChunkBounds &bounds = gWorldFrontier.playableBounds;
	const float margin = 0.25f;
	float minX = chunkBoundsMinX(bounds) + margin;
	float maxX = chunkBoundsMaxX(bounds) - margin;
	float minZ = chunkBoundsMinZ(bounds) + margin;
	float maxZ = chunkBoundsMaxZ(bounds) - margin;

	if (minX > maxX || minZ > maxZ)
	{
		return;
	}

	if (camera.Position.x < minX)
	{
		camera.Position.x = minX;
	}
	else if (camera.Position.x > maxX)
	{
		camera.Position.x = maxX;
	}

	if (camera.Position.z < minZ)
	{
		camera.Position.z = minZ;
	}
	else if (camera.Position.z > maxZ)
	{
		camera.Position.z = maxZ;
	}
}

void computeFrameFog(float fogReferenceDistance, float &outFogStart, float &outFogEnd)
{
	if (!minecraftFogByRenderDistance)
	{
		outFogStart = fogStart;
		outFogEnd = fogEnd;
		if (outFogEnd <= outFogStart + 0.1f)
		{
			outFogEnd = outFogStart + 0.1f;
		}
		return;
	}

	float startPercent = minecraftFogStartPercent;
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

Chunk2 *getChunkAt(int cx, int cz)
{
	auto it = chunkMap.find(chunkKey(cx, cz));
	if (it == chunkMap.end())
	{
		return nullptr;
	}
	return it->second;
}

bool isMeshBuildPendingForCurrentRevision(Chunk2 *chunk)
{
	int64_t key = chunkKey(chunk->chunkX, chunk->chunkZ);
	auto pendingIt = gPendingMeshRevisions.find(key);
	if (pendingIt == gPendingMeshRevisions.end())
	{
		return false;
	}
	if (pendingIt->second != chunk->revision)
	{
		gPendingMeshRevisions.erase(pendingIt);
		return false;
	}
	return true;
}

bool buildMeshJobForChunk(Chunk2 *chunk, ClientChunkMeshJob &outJob)
{
	if (chunk == nullptr)
	{
		return false;
	}

	int cx = chunk->chunkX;
	int cz = chunk->chunkZ;
	outJob.chunkX = cx;
	outJob.chunkZ = cz;
	outJob.revision = chunk->revision;
	outJob.center = static_cast<const VoxelChunkData &>(*chunk);
	Chunk2::captureMeshNeighborhood(
		outJob.neighbors,
		getChunkAt(cx, cz + 1),
		getChunkAt(cx, cz - 1),
		getChunkAt(cx + 1, cz),
		getChunkAt(cx - 1, cz),
		getChunkAt(cx + 1, cz + 1),
		getChunkAt(cx - 1, cz + 1),
		getChunkAt(cx + 1, cz - 1),
		getChunkAt(cx - 1, cz - 1));
	return true;
}

void drainCompletedMeshBuilds()
{
	std::vector<ClientChunkMeshResult> completedResults;
	gChunkMesher.drainCompleted(completedResults);

	for (ClientChunkMeshResult &result : completedResults)
	{
		int64_t key = chunkKey(result.chunkX, result.chunkZ);
		auto pendingIt = gPendingMeshRevisions.find(key);
		if (pendingIt != gPendingMeshRevisions.end() &&
			pendingIt->second == result.revision)
		{
			gPendingMeshRevisions.erase(pendingIt);
		}

		auto chunkIt = chunkMap.find(key);
		if (chunkIt == chunkMap.end())
		{
			continue;
		}

		Chunk2 *chunk = chunkIt->second;
		if (chunk == nullptr)
		{
			continue;
		}
		if (chunk->revision != result.revision)
		{
			continue;
		}

		chunk->uploadBuiltMesh(result.packedFaces);
		gChunkIndirectRenderer.upsertChunk(*chunk);
	}
}

void markChunkNeighborhoodDirty(int cx, int cz)
{
	for (int dz = -1; dz <= 1; dz++)
	{
		for (int dx = -1; dx <= 1; dx++)
		{
			Chunk2 *chunk = getChunkAt(cx + dx, cz + dz);
			if (chunk != nullptr)
			{
				chunk->needsMeshRebuild = true;
			}
		}
	}
}

uint32_t getBlockWorld(int wx, int wy, int wz)
{
	if (wy < 0 || wy >= CHUNK_SIZE_Y)
	{
		return 0;
	}

	int cx = floorDiv(wx, CHUNK_SIZE_X);
	int cz = floorDiv(wz, CHUNK_SIZE_Z);
	int lx = floorMod(wx, CHUNK_SIZE_X);
	int lz = floorMod(wz, CHUNK_SIZE_Z);

	Chunk2 *chunk = getChunkAt(cx, cz);
	if (chunk == nullptr)
	{
		return 0;
	}
	return chunk->getBlock(lx, wy, lz);
}

void applyBlockUpdateLocal(int wx, int wy, int wz, uint32_t color)
{
	int cx = floorDiv(wx, CHUNK_SIZE_X);
	int cz = floorDiv(wz, CHUNK_SIZE_Z);
	int lx = floorMod(wx, CHUNK_SIZE_X);
	int lz = floorMod(wz, CHUNK_SIZE_Z);

	Chunk2 *chunk = getChunkAt(cx, cz);
	if (chunk == nullptr)
	{
		return;
	}

	if (!chunk->setBlock(lx, wy, lz, color))
	{
		return;
	}
	markChunkNeighborhoodDirty(cx, cz);
}

void upsertChunkSnapshot(const VoxelChunkData &snapshot)
{
	int64_t key = chunkKey(snapshot.chunkX, snapshot.chunkZ);
	Chunk2 *chunk = nullptr;

	auto it = chunkMap.find(key);
	if (it == chunkMap.end())
	{
		chunk = new Chunk2(snapshot.chunkX, snapshot.chunkZ);
		chunkMap[key] = chunk;
	}
	else
	{
		chunk = it->second;
	}

	chunk->copyFromData(snapshot);
	markChunkNeighborhoodDirty(snapshot.chunkX, snapshot.chunkZ);
}

bool raycastPlaceTarget(const glm::vec3 &origin, const glm::vec3 &direction, float maxDist,
						glm::ivec3 &hitBlock, glm::ivec3 &placeBlock)
{
	const float inf = std::numeric_limits<float>::infinity();
	if (glm::length(direction) < 1e-6f)
	{
		return false;
	}

	glm::vec3 dir = glm::normalize(direction);
	int x = static_cast<int>(std::floor(origin.x));
	int y = static_cast<int>(std::floor(origin.y));
	int z = static_cast<int>(std::floor(origin.z));
	glm::ivec3 prevCell(x, y, z);

	int stepX = 0;
	int stepY = 0;
	int stepZ = 0;
	if (dir.x > 0.0f)
	{
		stepX = 1;
	}
	else if (dir.x < 0.0f)
	{
		stepX = -1;
	}
	if (dir.y > 0.0f)
	{
		stepY = 1;
	}
	else if (dir.y < 0.0f)
	{
		stepY = -1;
	}
	if (dir.z > 0.0f)
	{
		stepZ = 1;
	}
	else if (dir.z < 0.0f)
	{
		stepZ = -1;
	}

	float tDeltaX = inf;
	float tDeltaY = inf;
	float tDeltaZ = inf;
	if (stepX != 0)
	{
		tDeltaX = std::abs(1.0f / dir.x);
	}
	if (stepY != 0)
	{
		tDeltaY = std::abs(1.0f / dir.y);
	}
	if (stepZ != 0)
	{
		tDeltaZ = std::abs(1.0f / dir.z);
	}

	float nextX = 0.0f;
	float nextY = 0.0f;
	float nextZ = 0.0f;
	if (stepX > 0)
	{
		nextX = std::floor(origin.x) + 1.0f - origin.x;
	}
	else
	{
		nextX = origin.x - std::floor(origin.x);
	}
	if (stepY > 0)
	{
		nextY = std::floor(origin.y) + 1.0f - origin.y;
	}
	else
	{
		nextY = origin.y - std::floor(origin.y);
	}
	if (stepZ > 0)
	{
		nextZ = std::floor(origin.z) + 1.0f - origin.z;
	}
	else
	{
		nextZ = origin.z - std::floor(origin.z);
	}

	float tMaxX = inf;
	float tMaxY = inf;
	float tMaxZ = inf;
	if (stepX != 0)
	{
		tMaxX = nextX * tDeltaX;
	}
	if (stepY != 0)
	{
		tMaxY = nextY * tDeltaY;
	}
	if (stepZ != 0)
	{
		tMaxZ = nextZ * tDeltaZ;
	}

	float traveled = 0.0f;
	while (traveled <= maxDist)
	{
		if (y >= 0 && y < CHUNK_SIZE_Y && getBlockWorld(x, y, z) != 0)
		{
			hitBlock = glm::ivec3(x, y, z);
			placeBlock = prevCell;
			return true;
		}

		prevCell = glm::ivec3(x, y, z);
		if (tMaxX < tMaxY)
		{
			if (tMaxX < tMaxZ)
			{
				x += stepX;
				traveled = tMaxX;
				tMaxX += tDeltaX;
			}
			else
			{
				z += stepZ;
				traveled = tMaxZ;
				tMaxZ += tDeltaZ;
			}
		}
		else
		{
			if (tMaxY < tMaxZ)
			{
				y += stepY;
				traveled = tMaxY;
				tMaxY += tDeltaY;
			}
			else
			{
				z += stepZ;
				traveled = tMaxZ;
				tMaxZ += tDeltaZ;
			}
		}
	}

	return false;
}

void tryPlaceDebugBlock()
{
	if (!gHasWorldFrontier)
	{
		return;
	}

	glm::ivec3 hit;
	glm::ivec3 place;
	if (!raycastPlaceTarget(camera.Position, camera.Front, PLACE_REACH, hit, place))
	{
		return;
	}
	if (place.y <= 0 || place.y >= CHUNK_SIZE_Y)
	{
		return;
	}
	if (!usesClassicStreaming() &&
		!gWorldFrontier.playableBounds.containsWorldBlock(place.x, place.z))
	{
		return;
	}
	if (getBlockWorld(place.x, place.y, place.z) != 0)
	{
		return;
	}

	gWorldClient.sendPlaceBlock(place.x, place.y, place.z, static_cast<uint8_t>(selectedPaletteIndex - 1));
}

void tryBreakDebugBlock()
{
	if (!gHasWorldFrontier)
	{
		return;
	}

	glm::ivec3 hit;
	glm::ivec3 place;
	if (!raycastPlaceTarget(camera.Position, camera.Front, PLACE_REACH, hit, place))
	{
		return;
	}
	if (hit.y <= 0 || hit.y >= CHUNK_SIZE_Y)
	{
		return;
	}
	if (!usesClassicStreaming() &&
		!gWorldFrontier.playableBounds.containsWorldBlock(hit.x, hit.z))
	{
		return;
	}

	gWorldClient.sendBreakBlock(hit.x, hit.y, hit.z);
}

void handleWorldClientEvents()
{
	gWorldClient.service();

	WorldClientEvent event;
	while (gWorldClient.popEvent(event))
	{
		if (event.type == WorldClientEvent::Type::FrontierUpdated)
		{
			gWorldFrontier = event.frontier;
			gHasWorldFrontier = true;
			continue;
		}
		if (event.type == WorldClientEvent::Type::ChunkReceived)
		{
			upsertChunkSnapshot(event.chunk);
			gProfileChunkReceivesWindow++;
			continue;
		}
		if (event.type == WorldClientEvent::Type::BlockUpdated)
		{
			applyBlockUpdateLocal(
				event.blockUpdate.worldX,
				event.blockUpdate.worldY,
				event.blockUpdate.worldZ,
				event.blockUpdate.finalColor);
		}
	}
}

void syncChunkStreaming()
{
	if (!gWorldClient.isConnected() || !gHasWorldFrontier)
	{
		return;
	}

	float farPlane = computeRenderFarPlane();
	glm::mat4 projection = glm::perspective(
		glm::radians(camera.Zoom),
		static_cast<float>(SCREEN_WIDTH) / static_cast<float>(SCREEN_HEIGHT),
		0.1f,
		farPlane);
	glm::mat4 view = camera.GetViewMatrix();
	Frustum streamFrustum;
	streamFrustum.extractFromVP(projection * view);

	int streamDistanceChunks = renderDistanceChunks;
	if (usesClassicStreaming())
	{
		streamDistanceChunks += classicStreamingPaddingChunks;
	}

	int cameraChunkX = floorDiv(static_cast<int>(std::floor(camera.Position.x)), CHUNK_SIZE_X);
	int cameraChunkZ = floorDiv(static_cast<int>(std::floor(camera.Position.z)), CHUNK_SIZE_Z);
	int radiusSq = streamDistanceChunks * streamDistanceChunks;

	std::unordered_set<int64_t> desiredKeys;
	struct ChunkRequestCandidate
	{
		int chunkX = 0;
		int chunkZ = 0;
		float distSq = 0.0f;
		bool inFrustum = false;
	};
	std::vector<ChunkRequestCandidate> requestCandidates;

	for (int dz = -streamDistanceChunks; dz <= streamDistanceChunks; dz++)
	{
		for (int dx = -streamDistanceChunks; dx <= streamDistanceChunks; dx++)
		{
			if (dx * dx + dz * dz > radiusSq)
			{
				continue;
			}

			int cx = cameraChunkX + dx;
			int cz = cameraChunkZ + dz;
			if (!canStreamChunk(cx, cz))
			{
				continue;
			}

			int64_t key = chunkKey(cx, cz);
			desiredKeys.insert(key);
			if (streamedChunkKeys.find(key) == streamedChunkKeys.end())
			{
				ChunkRequestCandidate candidate;
				candidate.chunkX = cx;
				candidate.chunkZ = cz;
				candidate.distSq = chunkCenterDistanceSqToCamera(cx, cz);
				candidate.inFrustum = streamFrustum.isChunkVisible(cx, cz);
				requestCandidates.push_back(candidate);
			}
		}
	}

	std::sort(requestCandidates.begin(), requestCandidates.end(),
			  [](const ChunkRequestCandidate &left, const ChunkRequestCandidate &right)
			  {
				  if (left.inFrustum != right.inFrustum)
				  {
					  return left.inFrustum > right.inFrustum;
				  }
				  return left.distSq < right.distSq;
			  });

	for (const ChunkRequestCandidate &candidate : requestCandidates)
	{
		gWorldClient.sendChunkRequest(candidate.chunkX, candidate.chunkZ);
		streamedChunkKeys.insert(chunkKey(candidate.chunkX, candidate.chunkZ));
		gProfileChunkRequestsWindow++;
	}

	std::vector<int64_t> keysToDrop;
	for (int64_t key : streamedChunkKeys)
	{
		if (desiredKeys.find(key) == desiredKeys.end())
		{
			keysToDrop.push_back(key);
		}
	}

	for (int64_t key : keysToDrop)
	{
		int cx = static_cast<int>(key >> 32);
		int cz = static_cast<int>(key & 0xFFFFFFFF);
		gWorldClient.sendChunkDrop(cx, cz);
		streamedChunkKeys.erase(key);
		gProfileChunkDropsWindow++;
		gPendingMeshRevisions.erase(key);
		gChunkIndirectRenderer.removeChunk(key);
		markChunkNeighborhoodDirty(cx, cz);

		auto it = chunkMap.find(key);
		if (it != chunkMap.end())
		{
			delete it->second;
			chunkMap.erase(it);
		}
	}
}

void framebuffer_size_callback(GLFWwindow *window, int width, int height)
{
	glViewport(0, 0, width, height);
}

void mouse_callback(GLFWwindow *window, double xposIn, double yposIn)
{
	if (glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_NORMAL)
	{
		return;
	}

	float xpos = static_cast<float>(xposIn);
	float ypos = static_cast<float>(yposIn);
	if (firstMouse)
	{
		lastX = xpos;
		lastY = ypos;
		firstMouse = false;
	}

	float xoffset = xpos - lastX;
	float yoffset = lastY - ypos;
	lastX = xpos;
	lastY = ypos;
	camera.ProcessMouseMovement(xoffset, yoffset);
}

void mouse_button_callback(GLFWwindow *window, int button, int action, int mods)
{
	if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
	{
		if (glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_NORMAL)
		{
			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
			firstMouse = true;
		}
	}
}

void scroll_callback(GLFWwindow *window, double xoffset, double yoffset)
{
	camera.ProcessMouseScroll(static_cast<float>(yoffset));
}

void processInput(GLFWwindow *window)
{
	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
	{
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	}

	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
	{
		camera.ProcessKeyboard(FORWARD, deltaTime);
	}
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
	{
		camera.ProcessKeyboard(BACKWARD, deltaTime);
	}
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
	{
		camera.ProcessKeyboard(LEFT, deltaTime);
	}
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
	{
		camera.ProcessKeyboard(RIGHT, deltaTime);
	}
	if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
	{
		camera.ProcessKeyboard(UP, deltaTime);
	}

	static bool prevLeftDown = false;
	static bool prevRightDown = false;
	bool leftDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
	bool rightDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
	bool cursorCaptured = glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED;
	if (leftDown && !prevLeftDown && cursorCaptured)
	{
		gBreakBlockRequested = true;
	}
	if (rightDown && !prevRightDown && cursorCaptured)
	{
		gPlaceBlockRequested = true;
	}
	prevLeftDown = leftDown;
	prevRightDown = rightDown;
}

int main()
{
	bool profileWorkersEnabled = envFlagEnabled("VOXPLACE_PROFILE_WORKERS");
	int envRenderDistance = 0;
	if (tryReadEnvInt("VOXPLACE_RENDER_DISTANCE", envRenderDistance))
	{
		envRenderDistance = std::clamp(envRenderDistance, 2, 32);
		renderDistanceChunks = envRenderDistance;
	}

	int envClassicPadding = 0;
	if (tryReadEnvInt("VOXPLACE_CLASSIC_STREAM_PAD", envClassicPadding))
	{
		envClassicPadding = std::clamp(envClassicPadding, 0, 8);
		classicStreamingPaddingChunks = envClassicPadding;
	}

	size_t requestedMeshWorkers = 0;
	int envMeshWorkers = 0;
	if (tryReadEnvInt("VOXPLACE_MESH_WORKERS", envMeshWorkers))
	{
		if (envMeshWorkers > 0)
		{
			requestedMeshWorkers = static_cast<size_t>(envMeshWorkers);
		}
	}

	bool benchFlyEnabled = envFlagEnabled("VOXPLACE_BENCH_FLY");
	float benchFlySpeed = 220.0f;
	float envBenchFlySpeed = 0.0f;
	if (tryReadEnvFloat("VOXPLACE_BENCH_FLY_SPEED", envBenchFlySpeed))
	{
		if (envBenchFlySpeed > 0.0f)
		{
			benchFlySpeed = envBenchFlySpeed;
		}
	}

	float benchDurationSeconds = 0.0f;
	float envBenchDuration = 0.0f;
	if (tryReadEnvFloat("VOXPLACE_BENCH_SECONDS", envBenchDuration))
	{
		if (envBenchDuration > 0.0f)
		{
			benchDurationSeconds = envBenchDuration;
		}
	}

	std::cout << "INITIALISATION de GLFW" << std::endl;
	if (!glfwInit())
	{
		std::cerr << "Failed to initialize GLFW" << std::endl;
		return -1;
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	g_window = glfwCreateWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "VoxPlace", nullptr, nullptr);
	if (g_window == nullptr)
	{
		std::cerr << "Failed to create GLFW window" << std::endl;
		glfwTerminate();
		return -1;
	}

	glfwMakeContextCurrent(g_window);
	glfwSetFramebufferSizeCallback(g_window, framebuffer_size_callback);
	glfwSetCursorPosCallback(g_window, mouse_callback);
	glfwSetMouseButtonCallback(g_window, mouse_button_callback);
	glfwSetScrollCallback(g_window, scroll_callback);

#ifndef __EMSCRIPTEN__
	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		std::cerr << "Failed to initialize GLAD" << std::endl;
		return -1;
	}
#endif

	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glEnable(GL_DEPTH_TEST);
	glClearColor(FOG_COLOR.r, FOG_COLOR.g, FOG_COLOR.b, 1.0f);

	crosshair.init("assets/crosshair.png");
	glfwSwapInterval(0);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	ImGui::StyleColorsDark();
	ImGui_ImplGlfw_InitForOpenGL(g_window, true);
	ImGui_ImplOpenGL3_Init("#version 460");

	Shader chunkShader("src/shader/chunk2.vs", "src/shader/chunk2.fs");
	LowResRenderer::init(SCREEN_WIDTH, SCREEN_HEIGHT);
	gChunkMesher.start(requestedMeshWorkers);
	gChunkIndirectRenderer.init();
	std::cout << "Client mesh workers: " << gChunkMesher.workerCount() << std::endl;
	if (requestedMeshWorkers > 0)
	{
		std::cout << "Client mesh workers override: " << requestedMeshWorkers << std::endl;
	}
	if (profileWorkersEnabled)
	{
		std::cout << "Client worker profiling enabled" << std::endl;
	}
	if (benchFlyEnabled)
	{
		camera.Yaw = 0.0f;
		camera.Pitch = 0.0f;
		camera.ProcessMouseMovement(0.0f, 0.0f);
		camera.MovementSpeed = benchFlySpeed;
		std::cout << "Client bench fly enabled at speed " << benchFlySpeed << std::endl;
		if (benchDurationSeconds > 0.0f)
		{
			std::cout << "Client bench duration: " << benchDurationSeconds << " s" << std::endl;
		}
	}

	if (!gWorldClient.connectToServer(SERVER_HOST, SERVER_PORT))
	{
		std::cerr << "Failed to connect to server at " << SERVER_HOST << ":" << SERVER_PORT << std::endl;
	}

	auto profileWindowStart = std::chrono::steady_clock::now();
	double profileAccumFrameMs = 0.0;
	double profileMaxFrameMs = 0.0;
	double profileAccumRenderMs = 0.0;
	size_t profileSampleCount = 0;
	size_t profileAccumTracked = 0;
	size_t profileMaxTracked = 0;
	size_t profileAccumQueued = 0;
	size_t profileMaxQueued = 0;
	size_t profileAccumReady = 0;
	size_t profileMaxReady = 0;
	size_t profileAccumVisible = 0;
	size_t profileMaxVisible = 0;
	size_t profileAccumStreamed = 0;
	size_t profileMaxStreamed = 0;
	size_t profileAccumRequests = 0;
	size_t profileMaxRequests = 0;
	size_t profileAccumDrops = 0;
	size_t profileMaxDrops = 0;
	size_t profileAccumReceives = 0;
	size_t profileMaxReceives = 0;
	auto benchStartTime = std::chrono::steady_clock::now();

	while (!glfwWindowShouldClose(g_window))
	{
		float currentFrame = static_cast<float>(glfwGetTime());
		deltaTime = currentFrame - lastFrame;
		lastFrame = currentFrame;

		processInput(g_window);
		if (benchFlyEnabled)
		{
			camera.ProcessKeyboard(FORWARD, deltaTime);
		}
		handleWorldClientEvents();
		drainCompletedMeshBuilds();
		clampCameraToPlayableWorld();
		syncChunkStreaming();

		if (gBreakBlockRequested)
		{
			tryBreakDebugBlock();
			gBreakBlockRequested = false;
		}
		if (gPlaceBlockRequested)
		{
			tryPlaceDebugBlock();
			gPlaceBlockRequested = false;
		}

		LowResRenderer::beginFrame();
		chunkShader.use();

		float fogReferenceDistance = static_cast<float>(renderDistanceChunks * CHUNK_SIZE_X);
		if (fogReferenceDistance < static_cast<float>(CHUNK_SIZE_X))
		{
			fogReferenceDistance = static_cast<float>(CHUNK_SIZE_X);
		}
		float farPlane = computeRenderFarPlane();
		fogReferenceDistance = farPlane;
		float fogStartFrame = 0.0f;
		float fogEndFrame = 0.0f;
		computeFrameFog(fogReferenceDistance, fogStartFrame, fogEndFrame);

		glm::mat4 projection = glm::perspective(
			glm::radians(camera.Zoom),
			static_cast<float>(SCREEN_WIDTH) / static_cast<float>(SCREEN_HEIGHT),
			0.1f,
			farPlane);
		glm::mat4 view = camera.GetViewMatrix();

		chunkShader.setMat4("projection", projection);
		chunkShader.setMat4("view", view);
		chunkShader.setFloat("fogStart", fogStartFrame);
		chunkShader.setFloat("fogEnd", fogEndFrame);
		chunkShader.setVec3("fogColor", FOG_COLOR);
		chunkShader.setVec3("cameraPos", camera.Position);
		chunkShader.setInt("useAO", useAO ? 1 : 0);
		chunkShader.setInt("debugSunblockOnly", debugSunblockOnly ? 1 : 0);
		chunkShader.setInt("useIndirectDraw", 0);

		Frustum frustum;
		frustum.extractFromVP(projection * view);

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

		std::vector<ChunkDraw> visibleList;
		visibleList.reserve(chunkMap.size());
		std::vector<ChunkRebuildCandidate> rebuildCandidates;
		rebuildCandidates.reserve(chunkMap.size());

		for (auto &[key, chunk] : chunkMap)
		{
			float distSq = chunkCenterDistanceSqToCamera(chunk->chunkX, chunk->chunkZ);
			bool inFrustum = frustum.isChunkVisible(chunk->chunkX, chunk->chunkZ);

			if (chunk->needsMeshRebuild)
			{
				ChunkRebuildCandidate rebuildCandidate;
				rebuildCandidate.chunk = chunk;
				rebuildCandidate.distSq = distSq;
				rebuildCandidate.inFrustum = inFrustum;
				rebuildCandidates.push_back(rebuildCandidate);
			}

			if (!inFrustum)
			{
				continue;
			}

			visibleList.push_back({chunk, distSq});
		}

		std::sort(rebuildCandidates.begin(), rebuildCandidates.end(),
				  [](const ChunkRebuildCandidate &left, const ChunkRebuildCandidate &right)
				  {
					  if (left.inFrustum != right.inFrustum)
					  {
						  return left.inFrustum > right.inFrustum;
					  }
					  return left.distSq < right.distSq;
				  });

		size_t meshWorkerCount = gChunkMesher.workerCount();
		if (meshWorkerCount < 1)
		{
			meshWorkerCount = 1;
		}

		size_t maxPendingMeshJobs = meshWorkerCount * 2;
		if (maxPendingMeshJobs < 2)
		{
			maxPendingMeshJobs = 2;
		}

		size_t scheduleBudget = 0;
		if (gPendingMeshRevisions.size() < maxPendingMeshJobs)
		{
			scheduleBudget = maxPendingMeshJobs - gPendingMeshRevisions.size();
		}

		for (const ChunkRebuildCandidate &candidate : rebuildCandidates)
		{
			if (scheduleBudget == 0)
			{
				break;
			}
			if (isMeshBuildPendingForCurrentRevision(candidate.chunk))
			{
				continue;
			}

			ClientChunkMeshJob job;
			if (!buildMeshJobForChunk(candidate.chunk, job))
			{
				continue;
			}

			int64_t key = chunkKey(candidate.chunk->chunkX, candidate.chunk->chunkZ);
			gPendingMeshRevisions[key] = candidate.chunk->revision;
			gChunkMesher.enqueue(std::move(job));
			scheduleBudget--;
		}

		std::sort(visibleList.begin(), visibleList.end(),
				  [](const ChunkDraw &left, const ChunkDraw &right)
				  { return left.distSq < right.distSq; });

		int visibleChunks = static_cast<int>(visibleList.size());
		auto chunkRenderCpuStart = std::chrono::steady_clock::now();
		if (usesIndirectRendering())
		{
			std::vector<Chunk2 *> visibleChunksForIndirect;
			visibleChunksForIndirect.reserve(visibleList.size());
			for (const ChunkDraw &draw : visibleList)
			{
				visibleChunksForIndirect.push_back(draw.chunk);
			}

			gChunkIndirectRenderer.buildVisibleDrawData(visibleChunksForIndirect);
			chunkShader.setInt("useIndirectDraw", 1);
			gChunkIndirectRenderer.draw();
			chunkShader.setInt("useIndirectDraw", 0);
		}
		else
		{
			for (const ChunkDraw &draw : visibleList)
			{
				chunkShader.setVec3("chunkPos", glm::vec3(
											 static_cast<float>(draw.chunk->chunkX * CHUNK_SIZE_X),
											 0.0f,
											 static_cast<float>(draw.chunk->chunkZ * CHUNK_SIZE_Z)));
				draw.chunk->render();
			}
		}
		auto chunkRenderCpuEnd = std::chrono::steady_clock::now();
		chunkRenderCpuMs = std::chrono::duration<float, std::milli>(
			chunkRenderCpuEnd - chunkRenderCpuStart)
							   .count();

		int currentFbW = 0;
		int currentFbH = 0;
		glfwGetFramebufferSize(g_window, &currentFbW, &currentFbH);
		LowResRenderer::endFrame(currentFbW, currentFbH);

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		uint64_t totalFaces = 0;
		for (auto &[key, chunk] : chunkMap)
		{
			totalFaces += chunk->faceCount;
		}

		ImGui::Begin("VoxPlace");
		if (deltaTime > 0.0f)
		{
			ImGui::Text("FPS: %.0f (%.1f ms)", 1.0f / deltaTime, deltaTime * 1000.0f);
		}
		ImGui::Text("Server: %s:%d", SERVER_HOST, SERVER_PORT);
		ImGui::Text("Network: %s", gWorldClient.isConnected() ? "Connected" : "Disconnected");
		if (gHasWorldFrontier)
		{
			ImGui::Text("World mode: %s", worldGenerationModeName(gWorldFrontier.mode));
		}
		ImGui::Separator();
		ImGui::Text("Chunks: %d / %zu visible", visibleChunks, chunkMap.size());
		ImGui::Text("Streamed chunks: %zu", streamedChunkKeys.size());
		ImGui::Text("Chunk render CPU: %.3f ms", chunkRenderCpuMs);
		if (usesIndirectRendering())
		{
			int cpuDrawCalls = 0;
			if (gChunkIndirectRenderer.drawCount > 0)
			{
				cpuDrawCalls = 1;
			}
			ImGui::Text("Draw mode: Indirect");
			ImGui::Text("CPU draw calls: %d", cpuDrawCalls);
			ImGui::Text("Indirect commands: %zu", gChunkIndirectRenderer.drawCount);
			ImGui::Text("Indirect faces: %zu", gChunkIndirectRenderer.faceCount);
			ImGui::Text("Indirect draw data: %s (%llu rebuilds)",
						gChunkIndirectRenderer.lastBuildReused ? "reused" : "rebuilt",
						static_cast<unsigned long long>(gChunkIndirectRenderer.drawDataBuildCount));
			ImGui::Text("Arena usage: %.2f / %.2f MB",
						static_cast<float>(gChunkIndirectRenderer.arenaReservedFaces * sizeof(ChunkFaceInstanceGpu)) / (1024.0f * 1024.0f),
						static_cast<float>(gChunkIndirectRenderer.arenaFaceCapacity * sizeof(ChunkFaceInstanceGpu)) / (1024.0f * 1024.0f));
			ImGui::Text("Arena used faces: %zu", gChunkIndirectRenderer.arenaUsedFaces);
			ImGui::Text("Largest free span: %zu faces", gChunkIndirectRenderer.largestFreeFaceSpan);
			if (ImGui::Button("Compact Arena"))
			{
				gChunkIndirectRenderer.compact();
			}
		}
		else
		{
			ImGui::Text("Draw mode: Direct");
			ImGui::Text("CPU draw calls: %d", visibleChunks);
		}
		ImGui::Text("Mesh workers: %zu", gChunkMesher.workerCount());
		ImGui::Text("Mesh jobs: %zu tracked / %zu queued / %zu ready",
					gPendingMeshRevisions.size(),
					gChunkMesher.pendingJobCount(),
					gChunkMesher.completedJobCount());
		ImGui::Text("Total faces: %llu", totalFaces);
		ImGui::Text("Total vertices: %llu", totalFaces * 6);
		ImGui::SliderInt("Render Dist (chunks)", &renderDistanceChunks, 2, 32);
		ImGui::Text("Far Plane: %.1f", farPlane);
		if (gHasWorldFrontier)
		{
			if (usesClassicStreaming())
			{
				ImGui::Text("Classic streaming enabled");
				ImGui::Text("Playable bounds clamp disabled in this mode");
				ImGui::SliderInt("Classic Stream Pad", &classicStreamingPaddingChunks, 0, 8);
			}
			else
			{
				ImGui::Checkbox("Limit to playable world", &limitToPlayableWorld);
				ImGui::Text("Playable chunks: %d x %d",
							gWorldFrontier.playableBounds.widthChunks(),
							gWorldFrontier.playableBounds.depthChunks());
				ImGui::Text("Generated chunks: %d x %d",
							gWorldFrontier.generatedBounds.widthChunks(),
							gWorldFrontier.generatedBounds.depthChunks());
				ImGui::Text("Padding chunks: %d", gWorldFrontier.paddingChunks);
				ImGui::Text("Expansion progress: %d / %d",
							gWorldFrontier.activePlayableChunkCount,
							gWorldFrontier.requiredActiveChunkCount);
			}
		}
		else
		{
			ImGui::Checkbox("Limit to playable world", &limitToPlayableWorld);
		}
		ImGui::Separator();
		ImGui::Text("Camera: (%.1f, %.1f, %.1f)", camera.Position.x, camera.Position.y, camera.Position.z);
		ImGui::Text("Heading: %s", cameraHeadingCardinal(camera.Front));
		ImGui::SliderFloat("Speed", &camera.MovementSpeed, 1.0f, 100.0f);
		ImGui::Separator();
		ImGui::Text("Fog");
		ImGui::Checkbox("Minecraft Fog (Render Dist)", &minecraftFogByRenderDistance);
		if (minecraftFogByRenderDistance)
		{
			ImGui::SliderFloat("Fog Start %%", &minecraftFogStartPercent, 0.55f, 0.98f);
			ImGui::Text("Fog End = Far Plane");
		}
		else
		{
			float fogSliderMax = farPlane;
			if (fogSliderMax < 500.0f)
			{
				fogSliderMax = 500.0f;
			}
			ImGui::SliderFloat("Fog Start", &fogStart, 0.0f, fogSliderMax);
			ImGui::SliderFloat("Fog End", &fogEnd, 0.0f, fogSliderMax);
		}
		ImGui::Text("Fog active: %.1f -> %.1f", fogStartFrame, fogEndFrame);
		ImGui::Separator();
		ImGui::Checkbox("Ambient Occlusion", &useAO);
		ImGui::Checkbox("Sunblock debug", &debugSunblockOnly);
		ImGui::Text("Terrain Architecture: %s", terrainRenderArchitectureName(gTerrainRenderArchitecture));
		int terrainArchitectureIndex = static_cast<int>(gTerrainRenderArchitecture);
		ImGui::SliderInt("Terrain Arch", &terrainArchitectureIndex, 0, 1);
		gTerrainRenderArchitecture = static_cast<TerrainRenderArchitecture>(terrainArchitectureIndex);
		if (gTerrainRenderArchitecture != gPreviousTerrainRenderArchitecture)
		{
			if (usesIndirectRendering())
			{
				rebuildIndirectArenaFromLoadedChunks();
			}
			gPreviousTerrainRenderArchitecture = gTerrainRenderArchitecture;
		}
		ImGui::SliderInt("Palette index", &selectedPaletteIndex, 1, static_cast<int>(PLAYER_COLOR_PALETTE_SIZE));
		uint32_t selectedColor = playerPaletteColor(static_cast<uint8_t>(selectedPaletteIndex - 1));
		ImVec4 previewColor(
			static_cast<float>(VoxelChunkData::colorR(selectedColor)) / 255.0f,
			static_cast<float>(VoxelChunkData::colorG(selectedColor)) / 255.0f,
			static_cast<float>(VoxelChunkData::colorB(selectedColor)) / 255.0f,
			1.0f);
		ImGui::ColorButton("Palette preview", previewColor);
		ImGui::Checkbox("Crosshair", &crosshair.visible);
		ImGui::Text("Left click: break block");
		ImGui::Text("Right click: place block");
		ImGui::End();

		crosshair.render(g_window);

		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		if (profileWorkersEnabled)
		{
			double frameMs = static_cast<double>(deltaTime) * 1000.0;
			size_t trackedJobs = gPendingMeshRevisions.size();
			size_t queuedJobs = gChunkMesher.pendingJobCount();
			size_t readyJobs = gChunkMesher.completedJobCount();
			size_t visibleChunkCount = static_cast<size_t>(visibleChunks);
			size_t streamedChunkCount = streamedChunkKeys.size();
			size_t requestedChunks = gProfileChunkRequestsWindow;
			size_t droppedChunks = gProfileChunkDropsWindow;
			size_t receivedChunks = gProfileChunkReceivesWindow;

			profileAccumFrameMs += frameMs;
			profileAccumRenderMs += static_cast<double>(chunkRenderCpuMs);
			profileSampleCount++;
			profileAccumTracked += trackedJobs;
			profileAccumQueued += queuedJobs;
			profileAccumReady += readyJobs;
			profileAccumVisible += visibleChunkCount;
			profileAccumStreamed += streamedChunkCount;
			profileAccumRequests += requestedChunks;
			profileAccumDrops += droppedChunks;
			profileAccumReceives += receivedChunks;
			if (frameMs > profileMaxFrameMs)
			{
				profileMaxFrameMs = frameMs;
			}
			if (trackedJobs > profileMaxTracked)
			{
				profileMaxTracked = trackedJobs;
			}
			if (queuedJobs > profileMaxQueued)
			{
				profileMaxQueued = queuedJobs;
			}
			if (readyJobs > profileMaxReady)
			{
				profileMaxReady = readyJobs;
			}
			if (visibleChunkCount > profileMaxVisible)
			{
				profileMaxVisible = visibleChunkCount;
			}
			if (streamedChunkCount > profileMaxStreamed)
			{
				profileMaxStreamed = streamedChunkCount;
			}
			if (requestedChunks > profileMaxRequests)
			{
				profileMaxRequests = requestedChunks;
			}
			if (droppedChunks > profileMaxDrops)
			{
				profileMaxDrops = droppedChunks;
			}
			if (receivedChunks > profileMaxReceives)
			{
				profileMaxReceives = receivedChunks;
			}

			auto profileNow = std::chrono::steady_clock::now();
			auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
				profileNow - profileWindowStart);
			if (elapsed.count() >= 2000 && profileSampleCount > 0)
			{
				double avgFrameMs = profileAccumFrameMs / static_cast<double>(profileSampleCount);
				double avgFps = 0.0;
				if (avgFrameMs > 0.0)
				{
					avgFps = 1000.0 / avgFrameMs;
				}
				double avgRenderMs = profileAccumRenderMs / static_cast<double>(profileSampleCount);
				double avgTracked = static_cast<double>(profileAccumTracked) / static_cast<double>(profileSampleCount);
				double avgQueued = static_cast<double>(profileAccumQueued) / static_cast<double>(profileSampleCount);
				double avgReady = static_cast<double>(profileAccumReady) / static_cast<double>(profileSampleCount);
				double avgVisible = static_cast<double>(profileAccumVisible) / static_cast<double>(profileSampleCount);
				double avgStreamed = static_cast<double>(profileAccumStreamed) / static_cast<double>(profileSampleCount);
				double avgRequests = static_cast<double>(profileAccumRequests) / static_cast<double>(profileSampleCount);
				double avgDrops = static_cast<double>(profileAccumDrops) / static_cast<double>(profileSampleCount);
				double avgReceives = static_cast<double>(profileAccumReceives) / static_cast<double>(profileSampleCount);
				int cameraChunkX = floorDiv(static_cast<int>(std::floor(camera.Position.x)), CHUNK_SIZE_X);
				int cameraChunkZ = floorDiv(static_cast<int>(std::floor(camera.Position.z)), CHUNK_SIZE_Z);

				std::cout << "[client-profile] workers=" << gChunkMesher.workerCount()
						  << " renderDist=" << renderDistanceChunks
						  << " camera_chunk=(" << cameraChunkX << "," << cameraChunkZ << ")"
						  << " fps_avg=" << avgFps
						  << " frame_ms_avg=" << avgFrameMs
						  << " frame_ms_max=" << profileMaxFrameMs
						  << " render_cpu_ms_avg=" << avgRenderMs
						  << " tracked_avg=" << avgTracked
						  << " tracked_max=" << profileMaxTracked
						  << " queued_avg=" << avgQueued
						  << " queued_max=" << profileMaxQueued
						  << " ready_avg=" << avgReady
						  << " ready_max=" << profileMaxReady
						  << " visible_avg=" << avgVisible
						  << " visible_max=" << profileMaxVisible
						  << " streamed_avg=" << avgStreamed
						  << " streamed_max=" << profileMaxStreamed
						  << " requests_avg=" << avgRequests
						  << " requests_max=" << profileMaxRequests
						  << " drops_avg=" << avgDrops
						  << " drops_max=" << profileMaxDrops
						  << " receives_avg=" << avgReceives
						  << " receives_max=" << profileMaxReceives
						  << std::endl;

				profileWindowStart = profileNow;
				profileAccumFrameMs = 0.0;
				profileMaxFrameMs = 0.0;
				profileAccumRenderMs = 0.0;
				profileSampleCount = 0;
				profileAccumTracked = 0;
				profileMaxTracked = 0;
				profileAccumQueued = 0;
				profileMaxQueued = 0;
				profileAccumReady = 0;
				profileMaxReady = 0;
				profileAccumVisible = 0;
				profileMaxVisible = 0;
				profileAccumStreamed = 0;
				profileMaxStreamed = 0;
				profileAccumRequests = 0;
				profileMaxRequests = 0;
				profileAccumDrops = 0;
				profileMaxDrops = 0;
				profileAccumReceives = 0;
				profileMaxReceives = 0;
				gProfileChunkRequestsWindow = 0;
				gProfileChunkDropsWindow = 0;
				gProfileChunkReceivesWindow = 0;
			}
		}

		if (benchFlyEnabled && benchDurationSeconds > 0.0f)
		{
			auto benchNow = std::chrono::steady_clock::now();
			auto benchElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
				benchNow - benchStartTime);
			double benchElapsedSeconds = static_cast<double>(benchElapsed.count()) / 1000.0;
			if (benchElapsedSeconds >= static_cast<double>(benchDurationSeconds))
			{
				glfwSetWindowShouldClose(g_window, GLFW_TRUE);
			}
		}

		glfwSwapBuffers(g_window);
		glfwPollEvents();
	}

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	gWorldClient.disconnect();
	gChunkMesher.stop();
	gChunkIndirectRenderer.cleanup();
	gPendingMeshRevisions.clear();
	for (auto &[key, chunk] : chunkMap)
	{
		delete chunk;
	}
	chunkMap.clear();
	streamedChunkKeys.clear();

	crosshair.cleanup();
	LowResRenderer::cleanup();
	glfwTerminate();
	return 0;
}
