#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;

// Pour l'affine texture mapping (pas de correction de perspective)
out vec2 TexCoord;
out float vertexDepth;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform vec2 resolution;  // Résolution de l'écran (ex: 1920, 1080)

// ============================================================================
// PS1 VERTEX JITTERING
// Simule le manque de précision sous-pixel de la PlayStation 1
// ============================================================================

const float PS1_PRECISION = 1.0;  // Intensité du jitter (1.0 = subtil, 4.0 = fort)

vec4 snapToGrid(vec4 clipPos, float gridSize) {
    // Convertir en coordonnées écran normalisées
    vec2 ndc = clipPos.xy / clipPos.w;
    
    // Snapper sur une grille virtuelle basse résolution
    // Plus gridSize est petit, plus le jitter est visible
    vec2 snappedNDC = floor(ndc * gridSize + 0.5) / gridSize;
    
    // Reconvertir en clip space
    return vec4(snappedNDC * clipPos.w, clipPos.z, clipPos.w);
}

void main()
{
    vec4 clipPos = projection * view * model * vec4(aPos, 1.0f);
    
    // Calculer la grille de snap basée sur la résolution
    // PS1 avait environ 320x240, on simule ça
    float gridSize = min(resolution.x, resolution.y) / PS1_PRECISION;
    
    // Appliquer le vertex jittering
    gl_Position = snapToGrid(clipPos, gridSize);
    
    // ========================================================================
    // AFFINE TEXTURE MAPPING
    // La PS1 n'avait pas de correction de perspective pour les textures
    // On passe les UV multipliés par W, puis on divise dans le fragment shader
    // ========================================================================
    TexCoord = aTexCoord * clipPos.w;
    vertexDepth = clipPos.w;
}
