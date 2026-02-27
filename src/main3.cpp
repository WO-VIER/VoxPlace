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

// Rendering toggles
bool useAO = true;

// ============================================================================
// GLOBALS
// ============================================================================

GLFWwindow *g_window = nullptr;
Camera camera(glm::vec3(8.0f, 35.0f, 30.0f)); // get value (0,0) genesis chunk et check max value block on y
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
	for (int cx = -10; cx < 10; cx++)
	{
		for (int cz = -10; cz < 10; cz++)
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

		// Rendu basse résolution
		LowResRenderer::beginFrame();
		// Configurer le shader
		chunkShader.use();

		glm::mat4 projection = glm::perspective(
			glm::radians(camera.Zoom),
			(float)SCREEN_WIDTH / (float)SCREEN_HEIGHT,
			0.1f, 200.0f); // avant 200.0f // Far plane 200 / 16 = ~12 chunks 512 = 32chunks
		glm::mat4 view = camera.GetViewMatrix();

		chunkShader.setMat4("projection", projection);
		chunkShader.setMat4("view", view);

		// Fog uniforms
		chunkShader.setFloat("fogStart", fogStart);
		chunkShader.setFloat("fogEnd", fogEnd);
		chunkShader.setVec3("fogColor", FOG_COLOR);
		chunkShader.setVec3("cameraPos", camera.Position);
		chunkShader.setInt("useAO", useAO ? 1 : 0);

		// Frustum culling — extraire les plans depuis VP
		Frustum frustum;
		frustum.extractFromVP(projection * view);
		// frustum.frustumProfiler();

		// Dessiner les chunks visibles
		int visibleChunks = 0;
		for (auto &[key, chunk] : chunkMap)
		{
			if (chunk->needsMeshRebuild)
			{
				int cx = chunk->chunkX;
				int cz = chunk->chunkZ;
				chunk->meshGenerate(
					getChunkAt(cx, cz + 1),
					getChunkAt(cx, cz - 1),
					getChunkAt(cx + 1, cz),
					getChunkAt(cx - 1, cz));
			}

			// Skip si hors frustum
			if (!frustum.isChunkVisible(chunk->chunkX, chunk->chunkZ))
				continue;

			chunkShader.setVec3("chunkPos", glm::vec3(
												chunk->chunkX * CHUNK_SIZE_X,
												0.0f,
												chunk->chunkZ * CHUNK_SIZE_Z));
			chunk->render();
			visibleChunks++;
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

		ImGui::Begin("VoxPlace Debug");
		ImGui::Text("FPS: %.0f (%.1f ms)", 1.0f / deltaTime, deltaTime * 1000.0f);
		ImGui::Separator();
		ImGui::Text("Chunks: %d / %zu visible", visibleChunks, chunkMap.size());
		ImGui::Text("Total faces: %llu", totalFaces);
		ImGui::Text("Total vertices: %llu", totalFaces * 6);
		ImGui::Separator();
		ImGui::Text("Camera: (%.1f, %.1f, %.1f)", camera.Position.x, camera.Position.y, camera.Position.z);
		ImGui::SliderFloat("Speed", &camera.MovementSpeed, 1.0f, 100.0f);
		ImGui::Separator();
		ImGui::Text("Fog");
		ImGui::SliderFloat("Fog Start", &fogStart, 0.0f, 500.0f);
		ImGui::SliderFloat("Fog End", &fogEnd, 0.0f, 500.0f);
		ImGui::Separator();
		ImGui::Checkbox("Ambient Occlusion", &useAO);
		ImGui::End();

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
