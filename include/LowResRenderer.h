/**
 * LowResRenderer.h - Système de rendu basse résolution style rétro/PS1
 * 
 * Rend la scène dans un framebuffer basse résolution (ex: 320x240)
 * puis l'affiche en plein écran avec upscale nearest neighbor
 * 
 * Usage:
 *   1. Appeler init() au démarrage
 *   2. Appeler beginFrame() avant de rendre la scène
 *   3. Rendre la scène normalement
 *   4. Appeler endFrame() pour afficher le résultat upscalé
 */

#ifndef LOW_RES_RENDERER_H
#define LOW_RES_RENDERER_H

#include <glad/glad.h>
#include <iostream>

namespace LowResRenderer {

// ============================================================================
// CONFIGURATION
// Modifie ces valeurs pour changer la résolution de rendu
// ============================================================================

// Résolution de rendu interne (style PS1/rétro)
// Plus c'est petit, plus c'est pixelisé
// Scene → Framebuffer 320x240 → Upscale GL_NEAREST → Écran 1920x1080
// 480x270 → Un peu plus de détails, 640x360 → Style 16-bit/pixel art, 960x540 → Subtil, encore visible
constexpr int RENDER_WIDTH = 640;   // PS1 typique: 320
constexpr int RENDER_HEIGHT = 360;  // PS1 typique: 240

// ============================================================================
// VARIABLES INTERNES
// ============================================================================

static unsigned int framebuffer = 0;
static unsigned int textureColorbuffer = 0;
static unsigned int rbo = 0;  // Renderbuffer pour depth/stencil
static unsigned int quadVAO = 0;
static unsigned int quadVBO = 0;
static unsigned int screenShaderProgram = 0;

static int screenWidth = 1920;
static int screenHeight = 1080;

// ============================================================================
// SHADER POUR LE QUAD FULLSCREEN
// ============================================================================

static const char* screenVertexShader = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoords;

out vec2 TexCoords;

void main()
{
    TexCoords = aTexCoords;
    gl_Position = vec4(aPos.x, aPos.y, 0.0, 1.0);
}
)";

static const char* screenFragmentShader = R"(
#version 330 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D screenTexture;

void main()
{
    FragColor = texture(screenTexture, TexCoords);
}
)";

// Quad fullscreen (2 triangles)
static float quadVertices[] = {
    // positions   // texCoords
    -1.0f,  1.0f,  0.0f, 1.0f,
    -1.0f, -1.0f,  0.0f, 0.0f,
     1.0f, -1.0f,  1.0f, 0.0f,

    -1.0f,  1.0f,  0.0f, 1.0f,
     1.0f, -1.0f,  1.0f, 0.0f,
     1.0f,  1.0f,  1.0f, 1.0f
};

// ============================================================================
// FONCTIONS
// ============================================================================

inline unsigned int compileShader(const char* source, GLenum type) {
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    
    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, 512, NULL, log);
        std::cerr << "LowResRenderer: Shader compilation error: " << log << std::endl;
    }
    return shader;
}

/**
 * Initialise le système de rendu basse résolution
 * @param screenW Largeur de la fenêtre
 * @param screenH Hauteur de la fenêtre
 */
inline void init(int screenW, int screenH) {
    screenWidth = screenW;
    screenHeight = screenH;
    
    // ========================================================================
    // CRÉER LE FRAMEBUFFER BASSE RÉSOLUTION
    // ========================================================================
    
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    
    // Texture pour le color attachment (basse résolution)
    glGenTextures(1, &textureColorbuffer);
    glBindTexture(GL_TEXTURE_2D, textureColorbuffer);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, RENDER_WIDTH, RENDER_HEIGHT, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    
    // IMPORTANT: GL_NEAREST pour l'upscale pixelisé !
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureColorbuffer, 0);
    
    // Renderbuffer pour depth et stencil
    glGenRenderbuffers(1, &rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, RENDER_WIDTH, RENDER_HEIGHT);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo);
    
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "LowResRenderer: Framebuffer is not complete!" << std::endl;
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    // ========================================================================
    // CRÉER LE SHADER POUR LE QUAD FULLSCREEN
    // ========================================================================
    
    unsigned int vertexShader = compileShader(screenVertexShader, GL_VERTEX_SHADER);
    unsigned int fragmentShader = compileShader(screenFragmentShader, GL_FRAGMENT_SHADER);
    
    screenShaderProgram = glCreateProgram();
    glAttachShader(screenShaderProgram, vertexShader);
    glAttachShader(screenShaderProgram, fragmentShader);
    glLinkProgram(screenShaderProgram);
    
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    
    // ========================================================================
    // CRÉER LE VAO/VBO POUR LE QUAD FULLSCREEN
    // ========================================================================
    
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    
    std::cout << "LowResRenderer initialized: " << RENDER_WIDTH << "x" << RENDER_HEIGHT 
              << " -> " << screenWidth << "x" << screenHeight << std::endl;
}

/**
 * Commence le rendu dans le framebuffer basse résolution
 * Appeler avant de rendre la scène
 */
inline void beginFrame() {
    // Rendre dans le framebuffer basse résolution
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, RENDER_WIDTH, RENDER_HEIGHT);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
}

/**
 * Termine le rendu et affiche le résultat upscalé sur l'écran
 * Appeler après avoir rendu la scène
 */
inline void endFrame() {
    // Revenir au framebuffer par défaut (écran)
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, screenWidth, screenHeight);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    
    // Dessiner le quad avec la texture basse résolution
    glUseProgram(screenShaderProgram);
    glBindVertexArray(quadVAO);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureColorbuffer);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

/**
 * Libère les ressources
 */
inline void cleanup() {
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteTextures(1, &textureColorbuffer);
    glDeleteRenderbuffers(1, &rbo);
    glDeleteVertexArrays(1, &quadVAO);
    glDeleteBuffers(1, &quadVBO);
    glDeleteProgram(screenShaderProgram);
}

} // namespace LowResRenderer

#endif // LOW_RES_RENDERER_H
