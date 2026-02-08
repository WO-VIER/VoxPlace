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
#include <Chunk.h>
#include <LowResRenderer.h>
#include <cmath>
#include <config.h>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/fwd.hpp>
#include <glm/trigonometric.hpp>
#include <iostream>
#include <vector>

// ============================================================================
// CONFIGURATION
// ============================================================================

const int SCREEN_WIDTH = 1920;
const int SCREEN_HEIGHT = 1080;

// Fog settings
const float FOG_START = 40.0f;
const float FOG_END = 80.0f;
const glm::vec3 FOG_COLOR = glm::vec3(0.6f, 0.7f, 0.9f);  // Bleu ciel

// ============================================================================
// GLOBALS
// ============================================================================

GLFWwindow* g_window = nullptr;
Camera camera(glm::vec3(8.0f, 20.0f, 30.0f));
float lastX = SCREEN_WIDTH / 2.0f;
float lastY = SCREEN_HEIGHT / 2.0f;
bool firstMouse = true;
float deltaTime = 0.0f;
float lastFrame = 0.0f;

// Chunks
std::vector<Chunk*> chunks;

// ============================================================================
// CALLBACKS
// ============================================================================

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

void mouse_callback(GLFWwindow* window, double xposIn, double yposIn) {
    float xpos = static_cast<float>(xposIn);
    float ypos = static_cast<float>(yposIn);

    if (firstMouse) {
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

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    camera.ProcessMouseScroll(static_cast<float>(yoffset));
}

void processInput(GLFWwindow* window) {
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
}

// ============================================================================
// GÉNÉRATION DE TERRAIN SIMPLE
// ============================================================================

void generateTestTerrain(Chunk& chunk) {
    // Générer un terrain simple avec des hauteurs aléatoires
    for (int x = 0; x < CHUNK_SIZE_X; x++) {
        for (int z = 0; z < CHUNK_SIZE_Z; z++) {
            // Hauteur basée sur une "vague" simple
            float nx = (float)(chunk.chunkX * CHUNK_SIZE_X + x) * 0.1f;
            float nz = (float)(chunk.chunkZ * CHUNK_SIZE_Z + z) * 0.1f;
            int height = 5 + (int)(sin(nx) * 2 + cos(nz) * 2);
            
            // Remplir jusqu'à cette hauteur
            for (int y = 1; y <= height && y < CHUNK_SIZE_Y; y++) {
                // Couleur basée sur la hauteur
                if (y == height) {
                    chunk.blocks[x][y][z] = 9;  // Vert (herbe)
                } else if (y > height - 3) {
                    chunk.blocks[x][y][z] = 15; // Marron (terre)
                } else {
                    chunk.blocks[x][y][z] = 3;  // Gris (pierre)
                }
            }
        }
    }
    chunk.needsMeshRebuild = true;
}

// ============================================================================
// MAIN
// ============================================================================

int main() {
    // Initialiser GLFW
    std::cout << "INITIALISATION de GLFW" << std::endl;
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    g_window = glfwCreateWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "VoxPlace - Chunk Test", NULL, NULL);
    if (!g_window) {
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
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
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
    Shader chunkShader("src/shader/chunk.vs", "src/shader/chunk.fs");

    // Initialiser le rendu basse résolution
    LowResRenderer::init(SCREEN_WIDTH, SCREEN_HEIGHT);

    // Créer quelques chunks de test (grille 3x3)
    std::cout << "Generating chunks..." << std::endl;
    for (int cx = -1; cx <= 1; cx++) {
        for (int cz = -1; cz <= 1; cz++) {
            Chunk* chunk = new Chunk(cx, cz);
            generateTestTerrain(*chunk);
            chunk->generateMesh();
            chunks.push_back(chunk);
            std::cout << "  Chunk (" << cx << ", " << cz << ") - " 
                      << chunk->indexCount << " indices" << std::endl;
        }
    }
    std::cout << "Generated " << chunks.size() << " chunks" << std::endl;

    // Render loop
    while (!glfwWindowShouldClose(g_window)) {
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
            0.1f, 200.0f
        );
        glm::mat4 view = camera.GetViewMatrix();
        glm::mat4 model = glm::mat4(1.0f);
        
        chunkShader.setMat4("projection", projection);
        chunkShader.setMat4("view", view);
        chunkShader.setMat4("model", model);
        
        // Fog uniforms
        chunkShader.setFloat("fogStart", FOG_START);
        chunkShader.setFloat("fogEnd", FOG_END);
        chunkShader.setVec3("fogColor", FOG_COLOR);
        chunkShader.setVec3("cameraPos", camera.Position);

        // Dessiner tous les chunks
        for (Chunk* chunk : chunks) {
            if (chunk->needsMeshRebuild) {
                chunk->generateMesh();
            }
            chunk->render();
        }

        // Afficher le résultat upscalé
        LowResRenderer::endFrame();

        glfwSwapBuffers(g_window);
        glfwPollEvents();
    }

    // Cleanup
    for (Chunk* chunk : chunks) {
        delete chunk;
    }
    LowResRenderer::cleanup();
    glfwTerminate();
    
    return 0;
}
