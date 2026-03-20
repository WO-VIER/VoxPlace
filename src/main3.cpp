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
int selectedPaletteIndex = 32;
Crosshair crosshair;

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

std::unordered_map<int64_t, Chunk2 *> chunkMap;
std::unordered_set<int64_t> streamedChunkKeys;
std::unordered_map<int64_t, uint64_t> gPendingMeshRevisions;

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

void copyChunkSnapshot(const Chunk2 *chunk, ChunkMeshSnapshot &snapshot)
{
	if (chunk == nullptr)
	{
		snapshot.hasChunk = false;
		return;
	}

	snapshot.hasChunk = true;
	snapshot.chunk = static_cast<const VoxelChunkData &>(*chunk);
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

	copyChunkSnapshot(getChunkAt(cx, cz + 1), outJob.north);
	copyChunkSnapshot(getChunkAt(cx, cz - 1), outJob.south);
	copyChunkSnapshot(getChunkAt(cx + 1, cz), outJob.east);
	copyChunkSnapshot(getChunkAt(cx - 1, cz), outJob.west);
	copyChunkSnapshot(getChunkAt(cx + 1, cz + 1), outJob.ne);
	copyChunkSnapshot(getChunkAt(cx - 1, cz + 1), outJob.nw);
	copyChunkSnapshot(getChunkAt(cx + 1, cz - 1), outJob.se);
	copyChunkSnapshot(getChunkAt(cx - 1, cz - 1), outJob.sw);
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
		gPendingMeshRevisions.erase(key);
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
	gChunkMesher.start();
	std::cout << "Client mesh workers: " << gChunkMesher.workerCount() << std::endl;

	if (!gWorldClient.connectToServer(SERVER_HOST, SERVER_PORT))
	{
		std::cerr << "Failed to connect to server at " << SERVER_HOST << ":" << SERVER_PORT << std::endl;
	}

	while (!glfwWindowShouldClose(g_window))
	{
		float currentFrame = static_cast<float>(glfwGetTime());
		deltaTime = currentFrame - lastFrame;
		lastFrame = currentFrame;

		processInput(g_window);
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
		for (const ChunkDraw &draw : visibleList)
		{
			chunkShader.setVec3("chunkPos", glm::vec3(
										 static_cast<float>(draw.chunk->chunkX * CHUNK_SIZE_X),
										 0.0f,
										 static_cast<float>(draw.chunk->chunkZ * CHUNK_SIZE_Z)));
			draw.chunk->render();
		}

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

		glfwSwapBuffers(g_window);
		glfwPollEvents();
	}

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	gWorldClient.disconnect();
	gChunkMesher.stop();
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
