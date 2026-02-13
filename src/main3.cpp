/**
 * main3.cpp - Test du système de chunks
 *
 * Affiche quelques chunks avec des blocs colorés
 */

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
#include <LowResRenderer.h>
#include <profiler.h>
#include <cmath>
#include <config.h>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/fwd.hpp>
#include <glm/trigonometric.hpp>
#include <iostream>
#include <vector>
#include <unordered_map>

// ============================================================================
// CONFIGURATION
// ============================================================================

const int SCREEN_WIDTH = 1920;
const int SCREEN_HEIGHT = 1080;

// Fog settings
const float FOG_START = 0.0f;							 // 40.0f
const float FOG_END = 0.0f;								 // 80.0f
const glm::vec3 FOG_COLOR = glm::vec3(0.6f, 0.7f, 0.9f); // Bleu ciel

// ============================================================================
// GLOBALS
// ============================================================================

GLFWwindow *g_window = nullptr;
Camera camera(glm::vec3(8.0f, 20.0f, 30.0f));
float lastX = SCREEN_WIDTH / 2.0f;
float lastY = SCREEN_HEIGHT / 2.0f;
bool firstMouse = true;
float deltaTime = 0.0f;
float lastFrame = 0.0f;

// Chunks : hashmap indexée par (cx, cz) pour accès O(1) aux voisins
// ┌────────┐  ┌────────┐  ┌────────┐
// │(-1,-1) │  │( 0,-1) │  │( 1,-1) │
// ├────────┤  ├────────┤  ├────────┤
// │(-1, 0) │  │( 0, 0) │  │( 1, 0) │
// ├────────┤  ├────────┤  ├────────┤
// │(-1, 1) │  │( 0, 1) │  │( 1, 1) │
// └────────┘  └────────┘  └────────┘
std::unordered_map<int64_t, Chunk2*> chunkMap;

// Convertit (cx, cz) en clé unique pour la hashmap
inline int64_t chunkKey(int cx, int cz) {
    return ((int64_t)cx << 32) | ((int64_t)cz & 0xFFFFFFFF);
}

// Récupère un chunk par position, nullptr si inexistant
inline Chunk2* getChunkAt(int cx, int cz) {
    auto it = chunkMap.find(chunkKey(cx, cz));
    return (it != chunkMap.end()) ? it->second : nullptr;
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

void scroll_callback(GLFWwindow *window, double xoffset, double yoffset)
{
	camera.ProcessMouseScroll(static_cast<float>(yoffset));
}

void processInput(GLFWwindow *window)
{
	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
		glfwSetWindowShouldClose(window, true);

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
}

// ============================================================================
// GÉNÉRATION DE TERRAIN SIMPLE
// ============================================================================

void generateTestTerrain(Chunk2 &chunk)
{
	// Générer un terrain simple avec des hauteurs aléatoires
	for (int x = 0; x < CHUNK_SIZE_X; x++)
	{
		for (int z = 0; z < CHUNK_SIZE_Z; z++)
		{
			// Hauteur basée sur une "vague" simple
			float nx = (float)(chunk.chunkX * CHUNK_SIZE_X + x) * 0.1f;
			float nz = (float)(chunk.chunkZ * CHUNK_SIZE_Z + z) * 0.1f;
			int height = 5 + (int)(sin(nx) * 2 + cos(nz) * 2);

			// Remplir jusqu'à cette hauteur
			for (int y = 1; y <= height && y < CHUNK_SIZE_Y; y++)
			{
				// Couleur basée sur la hauteur (indices palette r/place)
				if (y == height)
				{
					chunk.blocks[x][y][z] = 8; // Vert clair (herbe)
				}
				else if (y > height - 3)
				{
					chunk.blocks[x][y][z] = 26; // Marron (terre)
				}
				else
				{
					chunk.blocks[x][y][z] = 29; // Gris foncé (pierre)
				}
			}
		}
	}
	chunk.needsMeshRebuild = true;
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

	g_window = glfwCreateWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "VoxPlace - Chunk Test", NULL, NULL);
	if (!g_window)
	{
		std::cerr << "Failed to create GLFW window" << std::endl;
		glfwTerminate();
		return -1;
	}

	glfwMakeContextCurrent(g_window);
	glfwSetFramebufferSizeCallback(g_window, framebuffer_size_callback);
	glfwSetCursorPosCallback(g_window, mouse_callback);
	glfwSetScrollCallback(g_window, scroll_callback);
	glfwSetInputMode(g_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

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

	// Configuration OpenGL
	glEnable(GL_DEPTH_TEST);
	glClearColor(FOG_COLOR.r, FOG_COLOR.g, FOG_COLOR.b, 1.0f);

	// Charger le shader
	Shader chunkShader("src/shader/chunk2.vs", "src/shader/chunk2.fs");

	// Initialiser le rendu basse résolution
	LowResRenderer::init(SCREEN_WIDTH, SCREEN_HEIGHT);

	// 1. Créer tous les chunks et remplir le terrain
	std::cout << "Generating chunks..." << std::endl;
	for (int cx = -10; cx < 10; cx++)
	{
		for (int cz = -10; cz < 10; cz++)
		{
			// x[-10 a 10] z[-10 a 10]
			Chunk2 *chunk = new Chunk2(cx, cz);
			generateTestTerrain(*chunk);
			chunkMap[chunkKey(cx, cz)] = chunk;
		}
	}

	// 2. Générer les meshes APRÈS avoir créé tous les chunks
	//    (pour que les voisins existent au moment du face culling)
	for (auto& [key, chunk] : chunkMap)
	{
		int cx = chunk->chunkX;
		int cz = chunk->chunkZ;
		chunk->meshGenerate(
			getChunkAt(cx, cz + 1),  // north (+Z)
			getChunkAt(cx, cz - 1),  // south (-Z)
			getChunkAt(cx + 1, cz),  // east  (+X)
			getChunkAt(cx - 1, cz)   // west  (-X)
		);
	}
	std::cout << "Generated " << chunkMap.size() << " chunk(s)" << std::endl;

	// Afficher le profiler
	printChunkProfiler(chunkMap);

	// Render loop
	while (!glfwWindowShouldClose(g_window))
	{
		float currentFrame = static_cast<float>(glfwGetTime());
		deltaTime = currentFrame - lastFrame;
		lastFrame = currentFrame;

		processInput(g_window);

		// Rendu basse résolution
		LowResRenderer::beginFrame();

		// Configurer le shader
		chunkShader.use();

		glm::mat4 projection = glm::perspective(
			glm::radians(camera.Zoom),
			(float)SCREEN_WIDTH / (float)SCREEN_HEIGHT,
			0.1f, 1000.0f); // avant 200.0f // Far plane
		glm::mat4 view = camera.GetViewMatrix();

		chunkShader.setMat4("projection", projection);
		chunkShader.setMat4("view", view);

		// Fog uniforms
		chunkShader.setFloat("fogStart", FOG_START);
		chunkShader.setFloat("fogEnd", FOG_END);
		chunkShader.setVec3("fogColor", FOG_COLOR);
		chunkShader.setVec3("cameraPos", camera.Position);

		// Dessiner tous les chunks
		for (auto& [key, chunk] : chunkMap)
		{
			if (chunk->needsMeshRebuild)
			{
				int cx = chunk->chunkX;
				int cz = chunk->chunkZ;
				chunk->meshGenerate(
					getChunkAt(cx, cz + 1),
					getChunkAt(cx, cz - 1),
					getChunkAt(cx + 1, cz),
					getChunkAt(cx - 1, cz)
				);
			}
			chunkShader.setVec3("chunkPos", glm::vec3(
				chunk->chunkX * CHUNK_SIZE_X,
				0.0f,
				chunk->chunkZ * CHUNK_SIZE_Z));
			chunk->render();
		}

		// Afficher le résultat upscalé
		LowResRenderer::endFrame();

		glfwSwapBuffers(g_window);
		glfwPollEvents();
	}

	// Cleanup
	for (auto& [key, chunk] : chunkMap)
	{
		delete chunk;
	}
	LowResRenderer::cleanup();
	glfwTerminate();

	return 0;
}
