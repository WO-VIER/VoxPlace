#version 330 core
out vec4 FragColor;

// Affine texture mapping (UV non-corrigés)
in vec2 TexCoord;
in float vertexDepth;

uniform sampler2D texture1;

// ============================================================================
// PS1 STYLE CONFIGURATION
// ============================================================================

// Couleur de tint pour l'herbe (grass_top)
const vec3 grassTint = vec3(0.57, 0.74, 0.35);

// Plage UV de grass_top dans l'atlas (col=8, row=2)
const float GRASS_TOP_U_MIN = 0.5;
const float GRASS_TOP_U_MAX = 0.5625;
const float GRASS_TOP_V_MIN = 0.8125;
const float GRASS_TOP_V_MAX = 0.875;

// Nombre de niveaux de couleur par canal (PS1 ~= 32 niveaux)
const float COLOR_LEVELS = 32.0;

// Intensité du dithering (0.0 = off, 0.1 = subtil, 0.2 = visible)
const float DITHER_STRENGTH = 0.0;

// ============================================================================
// DITHERING PATTERN (Bayer 2x2)
// Simule le tramage utilisé sur PS1 pour plus de couleurs apparentes
// ============================================================================

float getDither(vec2 fragCoord) {
    // Bayer matrix 2x2
    int x = int(mod(fragCoord.x, 2.0));
    int y = int(mod(fragCoord.y, 2.0));
    
    // Pattern: [0, 2]
    //          [3, 1]
    float pattern[4] = float[4](0.0, 0.5, 0.75, 0.25);
    int index = x + y * 2;
    
    return (pattern[index] - 0.5) * DITHER_STRENGTH;
}

// ============================================================================
// COLOR QUANTIZATION
// Réduit le nombre de couleurs comme sur PS1
// ============================================================================

vec3 quantizeColor(vec3 color) {
    return floor(color * COLOR_LEVELS + 0.5) / COLOR_LEVELS;
}

void main()
{
    // ========================================================================
    // AFFINE TEXTURE MAPPING
    // Diviser par W pour annuler la correction de perspective
    // Crée cette distorsion caractéristique des textures PS1
    // ========================================================================
    vec2 affineUV = TexCoord / vertexDepth;
    
    vec4 texColor = texture(texture1, affineUV);
    
    // ========================================================================
    // GRASS TOP TINTING
    // Appliquer le tint vert si on est sur la texture grass_top
    // ========================================================================
    bool isGrassTop = (affineUV.x >= GRASS_TOP_U_MIN && affineUV.x <= GRASS_TOP_U_MAX &&
                       affineUV.y >= GRASS_TOP_V_MIN && affineUV.y <= GRASS_TOP_V_MAX);
    
    vec3 finalColor = texColor.rgb;
    if (isGrassTop) {
        finalColor *= grassTint;
    }
    
    // ========================================================================
    // PS1 POST-PROCESSING
    // ========================================================================
    
    // 1. Appliquer le dithering
    float dither = getDither(gl_FragCoord.xy);
    finalColor += dither;
    
    // 2. Quantifier les couleurs
    finalColor = quantizeColor(finalColor);
    
    // 3. Clamper pour éviter les artefacts
    finalColor = clamp(finalColor, 0.0, 1.0);
    
    FragColor = vec4(finalColor, texColor.a);
}
