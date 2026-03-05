// GLAD doit être inclus en premier !
#if defined(__EMSCRIPTEN__) || defined(EMSCRIPTEN_WEB)
#include <GLFW/glfw3.h>
#include <emscripten.h>
#else
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#endif

#include <Shader.h>
#include <Camera.h>
#include <Chunk2.h>
#include <TerrainGenerator.h>
#include <Frustum.h>
#include <LowResRenderer.h>
#include <profiler.h>
#include <config.h>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/fwd.hpp>
#include <glm/trigonometric.hpp>
#include <iostream>
#include <vector>
#include <thread>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

// ImGui
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

// ============================================================================
// CONFIGURATION
// ============================================================================

const int SCREEN_WIDTH = 1920;
const int SCREEN_HEIGHT = 1080;

// Fog settings (modifiable via ImGui)
float fogStart = 80.0f;
float fogEnd = 200.0f;
glm::vec3 FOG_COLOR = glm::vec3(0.6f, 0.7f, 0.9f); // Bleu ciel
bool minecraftFogByRenderDistance = true;
float minecraftFogStartPercent = 0.72f;

int renderDistanceChunks = 12;
bool limitToGeneratedWorld = true;
int worldBorderPaddingChunks = 2;

// Rendering toggles
bool useAO = true;
bool debugSunblockOnly = false;
float placedBlockColor[3] = {0.62f, 0.62f, 0.62f};
bool showCrosshair = true;

constexpr int WORLD_CHUNK_MIN_X = -10;
constexpr int WORLD_CHUNK_MAX_X_EXCLUSIVE = 10;
constexpr int WORLD_CHUNK_MIN_Z = -10;
constexpr int WORLD_CHUNK_MAX_Z_EXCLUSIVE = 10;

// ============================================================================
// GLOBALS
// ============================================================================

GLFWwindow *g_window = nullptr;
Camera camera(glm::vec3(0.0f, 35.0f, 0.0f)); // get value (0,0) genesis chunk et check max value block on y
float lastX = SCREEN_WIDTH / 2.0f;
float lastY = SCREEN_HEIGHT / 2.0f;
bool firstMouse = true;
float deltaTime = 0.0f;
float lastFrame = 0.0f;
bool gPlaceBlockRequested = false;
bool gBreakBlockRequested = false;
constexpr float PLACE_REACH = 8.0f;

const char *cameraHeadingCardinal(const glm::vec3 &front)
{
	float ax = std::abs(front.x);
	float az = std::abs(front.z);
	if (ax >= az)
	{
		if (front.x < 0.0f)
			return "West (-X)";
		return "East (+X)";
	}
	if (front.z < 0.0f)
		return "South (-Z)";
	return "North (+Z)";
}

float worldMinX()
{
	return static_cast<float>(WORLD_CHUNK_MIN_X * CHUNK_SIZE_X);
}

float worldMaxX()
{
	return static_cast<float>(WORLD_CHUNK_MAX_X_EXCLUSIVE * CHUNK_SIZE_X);
}

float worldMinZ()
{
	return static_cast<float>(WORLD_CHUNK_MIN_Z * CHUNK_SIZE_Z);
}

float worldMaxZ()
{
	return static_cast<float>(WORLD_CHUNK_MAX_Z_EXCLUSIVE * CHUNK_SIZE_Z);
}

float worldCenterX()
{
	return (worldMinX() + worldMaxX()) * 0.5f;
}

float worldCenterZ()
{
	return (worldMinZ() + worldMaxZ()) * 0.5f;
}

float worldRadius()
{
	float sizeX = worldMaxX() - worldMinX();
	float sizeZ = worldMaxZ() - worldMinZ();
	float minSize = sizeX;
	if (sizeZ < minSize)
		minSize = sizeZ;
	float padding = static_cast<float>(worldBorderPaddingChunks * CHUNK_SIZE_X);
	float radius = minSize * 0.5f - padding - 0.5f;
	if (radius < 8.0f)
		radius = 8.0f;
	return radius;
}

float chunkDistanceToCamera2D(int chunkX, int chunkZ, const glm::vec3 &cameraPos)
{
	float minX = static_cast<float>(chunkX * CHUNK_SIZE_X);
	float maxX = minX + static_cast<float>(CHUNK_SIZE_X);
	float minZ = static_cast<float>(chunkZ * CHUNK_SIZE_Z);
	float maxZ = minZ + static_cast<float>(CHUNK_SIZE_Z);

	float dx = 0.0f;
	if (cameraPos.x < minX)
		dx = minX - cameraPos.x;
	else if (cameraPos.x > maxX)
		dx = cameraPos.x - maxX;

	float dz = 0.0f;
	if (cameraPos.z < minZ)
		dz = minZ - cameraPos.z;
	else if (cameraPos.z > maxZ)
		dz = cameraPos.z - maxZ;

	return std::sqrt(dx * dx + dz * dz);
}

bool chunkCanRenderInFogRange(int chunkX, int chunkZ, const glm::vec3 &cameraPos, float fogEndDistance)
{
	float nearDistance = chunkDistanceToCamera2D(chunkX, chunkZ, cameraPos);
	float margin = static_cast<float>(CHUNK_SIZE_X);
	float maxVisibleDistance = fogEndDistance + margin;
	if (nearDistance > maxVisibleDistance)
		return false;
	return true;
}

void clampCameraToGeneratedWorld()
{
	if (!limitToGeneratedWorld)
		return;

	const float margin = 0.25f;
	float maxRadius = worldRadius() - margin;
	if (maxRadius < 1.0f)
		return;

	float dx = camera.Position.x - worldCenterX();
	float dz = camera.Position.z - worldCenterZ();
	float distCenter = std::sqrt(dx * dx + dz * dz);
	if (distCenter <= maxRadius)
		return;

	if (distCenter < 1e-6f)
	{
		camera.Position.x = worldCenterX() + maxRadius;
		camera.Position.z = worldCenterZ();
		return;
	}

	float scale = maxRadius / distCenter;
	camera.Position.x = worldCenterX() + dx * scale;
	camera.Position.z = worldCenterZ() + dz * scale;
}

void computeFrameFog(float farPlane, float &outFogStart, float &outFogEnd)
{
	if (!minecraftFogByRenderDistance)
	{
		outFogStart = fogStart;
		outFogEnd = fogEnd;
		if (outFogEnd <= outFogStart + 0.1f)
			outFogEnd = outFogStart + 0.1f;
		return;
	}

	float startPercent = minecraftFogStartPercent;
	if (startPercent < 0.40f)
		startPercent = 0.40f;
	if (startPercent > 0.98f)
		startPercent = 0.98f;

	outFogStart = farPlane * startPercent;
	outFogEnd = farPlane;

	if (outFogEnd <= outFogStart + 0.1f)
		outFogEnd = outFogStart + 0.1f;
}

// Chunks : hashmap indexée par (cx, cz) pour accès O(1) aux voisins
// ┌────────┐  ┌────────┐  ┌────────┐
// │(-1,-1) │  │( 0,-1) │  │( 1,-1) │
// ├────────┤  ├────────┤  ├────────┤
// │(-1, 0) │  │( 0, 0) │  │( 1, 0) │
// ├────────┤  ├────────┤  ├────────┤
// │(-1, 1) │  │( 0, 1) │  │( 1, 1) │
// └────────┘  └────────┘  └────────┘
std::unordered_map<int64_t, Chunk2 *> chunkMap; // O(1)

// Convertit (cx, cz) en clé unique 64 bits. En mémoire, cx occupe les 32 bits de poids fort
// et cz les 32 bits de poids faible. Le décalage (<< 32) place cx en tête, tandis que
// le masque (& 0xFFFFFFFF) isole cz pour éviter l'extension de signe lors du transtypage.

// On utilise bit packing 64bit pour stocker deux entiers 32bit(cx, cz) dans un seul entier 64bit
//  On convertit cx et cz en entier 64bit cx << 32 occupe les 32 bits de poids fort et cz & 0xFFFFFFFF occupe les 32 bits de poids faible la moitier basse le masque & 0xFFFFFFFF est pour éviter l'extension de signe lors du transtypage
//  64 bits le cpu fait CMP pour vérifier la clé alors que std::pair fait 2 CMP
inline int64_t chunkKey(int cx, int cz)
{
	return ((int64_t)cx << 32) | ((int64_t)cz & 0xFFFFFFFF);
}

// Récupère un chunk par position, nullptr si inexistant
inline Chunk2 *getChunkAt(int cx, int cz)
{
	auto it = chunkMap.find(chunkKey(cx, cz));
	if (it != chunkMap.end())
		return it->second;
	return nullptr;
}

inline int floorDiv(int value, int divisor)
{
	int q = value / divisor;
	int r = value % divisor;
	if (r < 0)
		q--;
	return q;
}

inline int floorMod(int value, int divisor)
{
	int r = value % divisor;
	if (r < 0)
		r += divisor;
	return r;
}

inline void markChunkNeighborhoodDirty(int cx, int cz)
{
	for (int dz = -1; dz <= 1; dz++)
	{
		for (int dx = -1; dx <= 1; dx++)
		{
			if (Chunk2 *neighbor = getChunkAt(cx + dx, cz + dz))
				neighbor->needsMeshRebuild = true;
		}
	}
}

uint32_t getBlockWorld(int wx, int wy, int wz)
{
	if (wy < 0 || wy >= CHUNK_SIZE_Y)
		return 0;

	int cx = floorDiv(wx, CHUNK_SIZE_X);
	int cz = floorDiv(wz, CHUNK_SIZE_Z);
	int lx = floorMod(wx, CHUNK_SIZE_X);
	int lz = floorMod(wz, CHUNK_SIZE_Z);
	Chunk2 *chunk = getChunkAt(cx, cz);
	if (chunk)
		return chunk->getBlock(lx, wy, lz);
	return 0;
}

bool setBlockWorld(int wx, int wy, int wz, uint32_t color)
{
	if (wy < 0 || wy >= CHUNK_SIZE_Y)
		return false;

	int cx = floorDiv(wx, CHUNK_SIZE_X);
	int cz = floorDiv(wz, CHUNK_SIZE_Z);
	int lx = floorMod(wx, CHUNK_SIZE_X);
	int lz = floorMod(wz, CHUNK_SIZE_Z);

	Chunk2 *chunk = getChunkAt(cx, cz);
	if (!chunk || !chunk->setBlock(lx, wy, lz, color))
		return false;

	markChunkNeighborhoodDirty(cx, cz);
	return true;
}

bool raycastPlaceTarget(const glm::vec3 &origin, const glm::vec3 &direction, float maxDist,
						glm::ivec3 &hitBlock, glm::ivec3 &placeBlock)
{
	const float inf = std::numeric_limits<float>::infinity();
	if (glm::length(direction) < 1e-6f)
		return false;

	glm::vec3 dir = glm::normalize(direction);
	int x = static_cast<int>(std::floor(origin.x));
	int y = static_cast<int>(std::floor(origin.y));
	int z = static_cast<int>(std::floor(origin.z));
	glm::ivec3 prevCell(x, y, z);

	int stepX = 0;
	int stepY = 0;
	int stepZ = 0;
	if (dir.x > 0.0f)
		stepX = 1;
	else if (dir.x < 0.0f)
		stepX = -1;
	if (dir.y > 0.0f)
		stepY = 1;
	else if (dir.y < 0.0f)
		stepY = -1;
	if (dir.z > 0.0f)
		stepZ = 1;
	else if (dir.z < 0.0f)
		stepZ = -1;

	float tDeltaX = inf;
	float tDeltaY = inf;
	float tDeltaZ = inf;
	if (stepX != 0)
		tDeltaX = std::abs(1.0f / dir.x);
	if (stepY != 0)
		tDeltaY = std::abs(1.0f / dir.y);
	if (stepZ != 0)
		tDeltaZ = std::abs(1.0f / dir.z);

	float nextX = 0.0f;
	float nextY = 0.0f;
	float nextZ = 0.0f;
	if (stepX > 0)
		nextX = std::floor(origin.x) + 1.0f - origin.x;
	else
		nextX = origin.x - std::floor(origin.x);
	if (stepY > 0)
		nextY = std::floor(origin.y) + 1.0f - origin.y;
	else
		nextY = origin.y - std::floor(origin.y);
	if (stepZ > 0)
		nextZ = std::floor(origin.z) + 1.0f - origin.z;
	else
		nextZ = origin.z - std::floor(origin.z);

	float tMaxX = inf;
	float tMaxY = inf;
	float tMaxZ = inf;
	if (stepX != 0)
		tMaxX = nextX * tDeltaX;
	if (stepY != 0)
		tMaxY = nextY * tDeltaY;
	if (stepZ != 0)
		tMaxZ = nextZ * tDeltaZ;

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
	glm::ivec3 hit;
	glm::ivec3 place;
	if (!raycastPlaceTarget(camera.Position, camera.Front, PLACE_REACH, hit, place))
		return;

	if (place.y <= 0 || place.y >= CHUNK_SIZE_Y)
		return;
	if (getBlockWorld(place.x, place.y, place.z) != 0)
		return;

	int r = std::clamp(static_cast<int>(placedBlockColor[0] * 255.0f), 0, 255);
	int g = std::clamp(static_cast<int>(placedBlockColor[1] * 255.0f), 0, 255);
	int b = std::clamp(static_cast<int>(placedBlockColor[2] * 255.0f), 0, 255);
	uint32_t base = Chunk2::makeColor(r, g, b);
	(void)setBlockWorld(place.x, place.y, place.z, base);
}

void tryBreakDebugBlock()
{
	glm::ivec3 hit;
	glm::ivec3 place;
	if (!raycastPlaceTarget(camera.Position, camera.Front, PLACE_REACH, hit, place))
		return;
	if (hit.y <= 0 || hit.y >= CHUNK_SIZE_Y)
		return;

	(void)setBlockWorld(hit.x, hit.y, hit.z, 0u);
}

// ============================================================================
// CALLBACKS
// ============================================================================

void framebuffer_size_callback(GLFWwindow *window, int width, int height)
{
	glViewport(0, 0, width, height);
}

void mouse_callback(GLFWwindow *window, double xposIn, double yposIn)
{
	// Modification pour le débogage : ignorer la souris si elle n'est pas capturée
	if (glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_NORMAL)
		return;

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
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	// glfwSetWindowShouldClose(window, true);

	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
		camera.ProcessKeyboard(FORWARD, deltaTime);
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
		camera.ProcessKeyboard(BACKWARD, deltaTime);
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
		camera.ProcessKeyboard(LEFT, deltaTime);
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
		camera.ProcessKeyboard(RIGHT, deltaTime);
	if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
		camera.ProcessKeyboard(UP, deltaTime);

	static bool prevLeftDown = false;
	static bool prevRightDown = false;
	bool leftDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
	bool rightDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
	bool cursorCaptured = glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED;
	if (leftDown && !prevLeftDown && cursorCaptured)
		gBreakBlockRequested = true;
	if (rightDown && !prevRightDown && cursorCaptured)
		gPlaceBlockRequested = true;
	prevLeftDown = leftDown;
	prevRightDown = rightDown;
}

// ============================================================================
// MAIN
// ============================================================================

int main()
{
	// Initialiser GLFW
	std::cout << "INITIALISATION de GLFW" << std::endl;
	if (!glfwInit())
	{
		std::cerr << "Failed to initialize GLFW" << std::endl;
		return -1;
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	g_window = glfwCreateWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "VoxPlace", NULL, NULL);
	if (!g_window)
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
	// glfwSetInputMode(g_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED); // Démarrer avec la souris libre

#ifndef __EMSCRIPTEN__
	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		std::cerr << "Failed to initialize GLAD" << std::endl;
		return -1;
	}
	std::cout << "GLAD initialized successfully" << std::endl;
#endif

	std::cout << "OpenGL Renderer: " << glGetString(GL_RENDERER) << std::endl;
	std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);       // Jeter les faces arrière (dos à la caméra)


	// Configuration OpenGL
	glEnable(GL_DEPTH_TEST);
	glClearColor(FOG_COLOR.r, FOG_COLOR.g, FOG_COLOR.b, 1.0f);
	glfwSwapInterval(0);
	// ============================================================================
	// IMGUI INIT
	// ============================================================================
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	ImGui::StyleColorsDark();
	ImGui_ImplGlfw_InitForOpenGL(g_window, true);
	ImGui_ImplOpenGL3_Init("#version 460");

	// Charger le shader
	Shader chunkShader("src/shader/chunk2.vs", "src/shader/chunk2.fs");
	std::vector<std::thread> threads;

	for (auto &thread : threads)
	{
	}

	// Initialiser le rendu basse résolution
	LowResRenderer::init(SCREEN_WIDTH, SCREEN_HEIGHT);

	// Terrain generator (Simplex Noise, seed 42)
	TerrainGenerator gen(42);

	// 1. Créer tous les chunks et remplir le terrain  Besoins de créer un thread pour generation simuler "serveur" ou passer direct sur serveur.cpp
	std::cout << "Generating chunks..." << std::endl;
	for (int cx = WORLD_CHUNK_MIN_X; cx < WORLD_CHUNK_MAX_X_EXCLUSIVE; cx++)
	{
		for (int cz = WORLD_CHUNK_MIN_Z; cz < WORLD_CHUNK_MAX_Z_EXCLUSIVE; cz++)
		{
			Chunk2 *chunk = new Chunk2(cx, cz);
			gen.fillChunk(*chunk);
			chunkMap[chunkKey(cx, cz)] = chunk; // cx cz -> key
		}
	}

	// 2. Générer les meshes APRÈS avoir créé tous les chunks
	//    (pour que les voisins existent au moment du face culling)
	/*
	for (auto &[key, chunk] : chunkMap)
	{
		int cx = chunk->chunkX;
		int cz = chunk->chunkZ;
		chunk->meshGenerate(
			getChunkAt(cx, cz + 1), // north (+Z)
			getChunkAt(cx, cz - 1), // south (-Z)
			getChunkAt(cx + 1, cz), // east  (+X)
			getChunkAt(cx - 1, cz)	// west  (-X)
		);
	}
	std::cout << "Generated " << chunkMap.size() << " chunk(s)" << std::endl;

	*/
	// Afficher le profiler
	// printChunkProfiler(chunkMap);

	// Render loop
	while (!glfwWindowShouldClose(g_window))
	{
		float currentFrame = static_cast<float>(glfwGetTime());
		deltaTime = currentFrame - lastFrame;
		lastFrame = currentFrame;

		processInput(g_window);
		clampCameraToGeneratedWorld();
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

		// Rendu basse résolution
		LowResRenderer::beginFrame();
		// Configurer le shader
		chunkShader.use();

		float farPlane = static_cast<float>(renderDistanceChunks * CHUNK_SIZE_X + CHUNK_SIZE_X * 2);
		if (farPlane < 64.0f)
			farPlane = 64.0f;
		float fogStartFrame = 0.0f;
		float fogEndFrame = 0.0f;
		computeFrameFog(farPlane, fogStartFrame, fogEndFrame);

		glm::mat4 projection = glm::perspective(
			glm::radians(camera.Zoom),
			(float)SCREEN_WIDTH / (float)SCREEN_HEIGHT,
			0.1f, farPlane);
		glm::mat4 view = camera.GetViewMatrix();

		chunkShader.setMat4("projection", projection);
		chunkShader.setMat4("view", view);

		// Fog uniforms
		chunkShader.setFloat("fogStart", fogStartFrame);
		chunkShader.setFloat("fogEnd", fogEndFrame);
		chunkShader.setVec3("fogColor", FOG_COLOR);
		chunkShader.setVec3("cameraPos", camera.Position);
		int useAOInt = 0;
		if (useAO)
			useAOInt = 1;
		chunkShader.setInt("useAO", useAOInt);
		int debugSunblockInt = 0;
		if (debugSunblockOnly)
			debugSunblockInt = 1;
		chunkShader.setInt("debugSunblockOnly", debugSunblockInt);

		// Frustum culling — extraire les plans depuis VP
		Frustum frustum;
		frustum.extractFromVP(projection * view);
		// frustum.frustumProfiler();

		// Collecter les chunks visibles + trier front-to-back (Early-Z)
		struct ChunkDraw
		{
			Chunk2 *chunk;
			float distSq;
		};
		std::vector<ChunkDraw> visibleList;
		visibleList.reserve(chunkMap.size());

		for (auto &[key, chunk] : chunkMap)
		{
			if (!chunkCanRenderInFogRange(chunk->chunkX, chunk->chunkZ, camera.Position, fogEndFrame))
				continue;

			if (chunk->needsMeshRebuild)
			{
				int cx = chunk->chunkX;
				int cz = chunk->chunkZ;
				chunk->meshGenerate(
					getChunkAt(cx, cz + 1),     // north
					getChunkAt(cx, cz - 1),     // south
					getChunkAt(cx + 1, cz),     // east
					getChunkAt(cx - 1, cz),     // west
					getChunkAt(cx + 1, cz + 1), // NE
					getChunkAt(cx - 1, cz + 1), // NW
					getChunkAt(cx + 1, cz - 1), // SE
					getChunkAt(cx - 1, cz - 1)  // SW
				);
			}

			// Skip si hors frustum
			if (!frustum.isChunkVisible(chunk->chunkX, chunk->chunkZ))
				continue;

			// Centre du chunk en monde
			float centerX = chunk->chunkX * CHUNK_SIZE_X + CHUNK_SIZE_X * 0.5f;
			float centerZ = chunk->chunkZ * CHUNK_SIZE_Z + CHUNK_SIZE_Z * 0.5f;
			float dx = centerX - camera.Position.x;
			float dz = centerZ - camera.Position.z;
			visibleList.push_back({chunk, dx * dx + dz * dz});
		}

		// Tri front-to-back : les chunks proches d'abord → meilleur Early-Z
		std::sort(visibleList.begin(), visibleList.end(),
			[](const ChunkDraw &a, const ChunkDraw &b) { return a.distSq < b.distSq; });

		int visibleChunks = (int)visibleList.size();
		for (auto &cd : visibleList)
		{
			chunkShader.setVec3("chunkPos", glm::vec3(
										cd.chunk->chunkX * CHUNK_SIZE_X,
										0.0f,
										cd.chunk->chunkZ * CHUNK_SIZE_Z));
			cd.chunk->render();
		}
		// Afficher le résultat upscalé
		LowResRenderer::endFrame();

		// ============================================================
		// IMGUI (rendu après LowResRenderer pour être en full res)
		// ============================================================
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		// Compter le total de faces
		uint64_t totalFaces = 0;
		for (auto &[k, c] : chunkMap)
			totalFaces += c->faceCount;

		ImGui::Begin("VoxPlace");
		ImGui::Text("FPS: %.0f (%.1f ms)", 1.0f / deltaTime, deltaTime * 1000.0f);
		ImGui::Separator();
		ImGui::Text("Chunks: %d / %zu visible", visibleChunks, chunkMap.size());
		ImGui::Text("Total faces: %llu", totalFaces);
		ImGui::Text("Total vertices: %llu", totalFaces * 6);
		ImGui::SliderInt("Render Dist (chunks)", &renderDistanceChunks, 2, 32);
		ImGui::Text("Far Plane: %.1f", farPlane);
		ImGui::Checkbox("Limit to generated world", &limitToGeneratedWorld);
		ImGui::SliderInt("World Border Pad (chunks)", &worldBorderPaddingChunks, 0, 8);
		ImGui::Text("World Center: (%.1f, %.1f)", worldCenterX(), worldCenterZ());
		ImGui::Text("World Radius: %.1f", worldRadius());
		ImGui::Separator();
		ImGui::Text("Camera: (%.1f, %.1f, %.1f)", camera.Position.x, camera.Position.y, camera.Position.z);
		ImGui::Text("Heading: %s", cameraHeadingCardinal(camera.Front));
		ImGui::SliderFloat("Speed", &camera.MovementSpeed, 1.0f, 100.0f);
		ImGui::Separator();
		ImGui::Text("Fog");
		ImGui::Checkbox("Minecraft Fog (Render Dist)", &minecraftFogByRenderDistance);
		if (minecraftFogByRenderDistance)
		{
			ImGui::SliderFloat("Fog Start %", &minecraftFogStartPercent, 0.40f, 0.98f);
			ImGui::Text("Fog End = Far Plane");
		}
		else
		{
			ImGui::SliderFloat("Fog Start", &fogStart, 0.0f, 500.0f);
			ImGui::SliderFloat("Fog End", &fogEnd, 0.0f, 500.0f);
		}
		ImGui::Text("Fog active: %.1f -> %.1f", fogStartFrame, fogEndFrame);
		ImGui::Separator();
		ImGui::Checkbox("Ambient Occlusion", &useAO);
		ImGui::Checkbox("Sunblock debug", &debugSunblockOnly);
		ImGui::ColorEdit3("Placed block color", placedBlockColor);
		ImGui::Checkbox("Crosshair", &showCrosshair);
		ImGui::Text("Left click: break block");
		ImGui::Text("Right click: place block");
		ImGui::End();

		if (showCrosshair)
		{
			ImDrawList *drawList = ImGui::GetForegroundDrawList();
			ImVec2 displaySize = ImGui::GetIO().DisplaySize;
			float centerX = displaySize.x * 0.5f;
			float centerY = displaySize.y * 0.5f;
			float halfLen = 7.0f;
			float gap = 3.0f;
			ImU32 colOutline = IM_COL32(0, 0, 0, 220);
			ImU32 colMain = IM_COL32(255, 255, 255, 220);

			drawList->AddLine(ImVec2(centerX - halfLen - 1.0f, centerY), ImVec2(centerX - gap + 1.0f, centerY), colOutline, 3.0f);
			drawList->AddLine(ImVec2(centerX + gap - 1.0f, centerY), ImVec2(centerX + halfLen + 1.0f, centerY), colOutline, 3.0f);
			drawList->AddLine(ImVec2(centerX, centerY - halfLen - 1.0f), ImVec2(centerX, centerY - gap + 1.0f), colOutline, 3.0f);
			drawList->AddLine(ImVec2(centerX, centerY + gap - 1.0f), ImVec2(centerX, centerY + halfLen + 1.0f), colOutline, 3.0f);

			drawList->AddLine(ImVec2(centerX - halfLen, centerY), ImVec2(centerX - gap, centerY), colMain, 1.6f);
			drawList->AddLine(ImVec2(centerX + gap, centerY), ImVec2(centerX + halfLen, centerY), colMain, 1.6f);
			drawList->AddLine(ImVec2(centerX, centerY - halfLen), ImVec2(centerX, centerY - gap), colMain, 1.6f);
			drawList->AddLine(ImVec2(centerX, centerY + gap), ImVec2(centerX, centerY + halfLen), colMain, 1.6f);
		}

		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		glfwSwapBuffers(g_window);
		glfwPollEvents();
	}

	// Cleanup
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	for (auto &[key, chunk] : chunkMap)
	{
		delete chunk;
	}
	LowResRenderer::cleanup();
	glfwTerminate();

	return 0;
}
