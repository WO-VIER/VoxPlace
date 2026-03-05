#version 460 core

// ============================================================================
// FRAGMENT SHADER — Fondu avec onde triangulaire
//
// Variation de couleur par position monde :
//   R += 4 * abs((x%8) - 4) + rand    onde triangulaire sur X
//   G += 4 * abs((z%8) - 4) + rand    onde triangulaire sur Z
//   B += 4 * abs(((63-y)%8) - 4) + rand  onde triangulaire sur Y inversé
//
// Range total : 0 à 24 par composante sur 255 ≈ 9% variation
// Donne un dégradé naturel entre blocs adjacents.
// ============================================================================

out vec4 FragColor;

// Données du vertex shader
in flat int vColorIndex;
in flat int vFaceDir;
in flat int vShade;
in vec3 vFragPos;
in flat vec3 vWorldBlockPos;
in float vAO;

// Uniforms
uniform vec3 cameraPos;
uniform float fogStart;
uniform float fogEnd;
uniform vec3 fogColor;
uniform int useAO;

// ============================================================================
// PALETTE (65 couleurs : 0 = air, 1-32 = r/place, 33-64 = terrain)
// ============================================================================

const vec3 PALETTE[65] = vec3[65](
    vec3(0.000, 0.000, 0.000),  //  0: AIR (jamais rendu)
    // --- r/place 2023 (1-32) ---
    vec3(0.420, 0.004, 0.098),  //  1: Bordeaux
    vec3(0.741, 0.000, 0.216),  //  2: Rouge
    vec3(1.000, 0.271, 0.000),  //  3: Rouge-Orange
    vec3(0.996, 0.659, 0.000),  //  4: Orange
    vec3(1.000, 0.831, 0.208),  //  5: Jaune
    vec3(0.996, 0.973, 0.725),  //  6: Jaune pâle
    vec3(0.004, 0.635, 0.404),  //  7: Vert
    vec3(0.035, 0.800, 0.463),  //  8: Vert clair
    vec3(0.494, 0.925, 0.341),  //  9: Vert lime
    vec3(0.008, 0.459, 0.427),  // 10: Teal foncé
    vec3(0.000, 0.616, 0.667),  // 11: Teal
    vec3(0.000, 0.800, 0.745),  // 12: Cyan
    vec3(0.141, 0.310, 0.643),  // 13: Bleu foncé
    vec3(0.216, 0.565, 0.918),  // 14: Bleu
    vec3(0.322, 0.910, 0.953),  // 15: Bleu clair
    vec3(0.282, 0.224, 0.749),  // 16: Indigo
    vec3(0.412, 0.357, 1.000),  // 17: Violet
    vec3(0.580, 0.702, 1.000),  // 18: Lavande
    vec3(0.502, 0.114, 0.624),  // 19: Violet foncé
    vec3(0.706, 0.286, 0.749),  // 20: Magenta
    vec3(0.894, 0.671, 0.992),  // 21: Rose clair
    vec3(0.867, 0.067, 0.494),  // 22: Rose vif
    vec3(0.996, 0.216, 0.506),  // 23: Pink
    vec3(0.996, 0.600, 0.663),  // 24: Saumon
    vec3(0.427, 0.275, 0.184),  // 25: Marron foncé
    vec3(0.608, 0.412, 0.149),  // 26: Marron
    vec3(0.996, 0.706, 0.439),  // 27: Pêche
    vec3(0.000, 0.000, 0.000),  // 28: Noir
    vec3(0.322, 0.322, 0.322),  // 29: Gris foncé
    vec3(0.533, 0.553, 0.565),  // 30: Gris
    vec3(0.835, 0.839, 0.847),  // 31: Gris clair
    vec3(1.000, 1.000, 1.000),  // 32: Blanc
    // --- Terrain nuances (33-64) ---
    vec3(0.314, 0.376, 0.314),  // 33: Herbe sombre
    vec3(0.376, 0.345, 0.282),  // 34: Herbe-terre
    vec3(0.439, 0.314, 0.251),  // 35: Terre claire
    vec3(0.502, 0.282, 0.220),  // 36: Terre
    vec3(0.439, 0.251, 0.188),  // 37: Terre-rouge
    vec3(0.376, 0.220, 0.157),  // 38: Terre foncée
    vec3(0.314, 0.188, 0.125),  // 39: Terre sombre
    vec3(0.251, 0.157, 0.094),  // 40: Très foncé
    vec3(0.188, 0.125, 0.063),  // 41: Fond
    vec3(0.200, 0.400, 0.200),  // 42: Herbe vive
    vec3(0.180, 0.350, 0.180),  // 43: Herbe moyenne
    vec3(0.160, 0.300, 0.160),  // 44: Herbe sombre 2
    vec3(0.400, 0.400, 0.420),  // 45: Pierre claire
    vec3(0.350, 0.350, 0.370),  // 46: Pierre
    vec3(0.300, 0.300, 0.320),  // 47: Pierre sombre
    vec3(0.580, 0.520, 0.380),  // 48: Sable clair
    vec3(0.520, 0.460, 0.340),  // 49: Sable
    vec3(0.460, 0.400, 0.300),  // 50: Sable foncé
    vec3(0.700, 0.700, 0.700),  // 51: Neige sale
    vec3(0.900, 0.920, 0.940),  // 52: Neige
    vec3(0.180, 0.280, 0.180),  // 53: Feuillage
    vec3(0.220, 0.320, 0.120),  // 54: Mousse
    vec3(0.150, 0.200, 0.350),  // 55: Eau profonde
    vec3(0.200, 0.350, 0.500),  // 56: Eau
    vec3(0.400, 0.250, 0.150),  // 57: Bois foncé
    vec3(0.550, 0.380, 0.220),  // 58: Bois
    vec3(0.650, 0.500, 0.300),  // 59: Bois clair
    vec3(0.600, 0.300, 0.200),  // 60: Brique
    vec3(0.700, 0.350, 0.250),  // 61: Brique claire
    vec3(0.250, 0.250, 0.300),  // 62: Ardoise
    vec3(0.350, 0.200, 0.180),  // 63: Terre rouge
    vec3(0.450, 0.420, 0.350)   // 64: Grès
);

// ============================================================================
// FACE SHADING (ombre directionnelle)
// ============================================================================

const float FACE_BRIGHTNESS[6] = float[6](
    1.000,  // 0: TOP
    0.500,  // 1: BOTTOM
    0.625,  // 2: +Z (north in VoxPlace naming), BetterSpades parity
    0.875,  // 3: -Z (south in VoxPlace naming), BetterSpades parity
    0.750,  // 4: EAST
    0.750   // 5: WEST
);

// ============================================================================
// MAIN
// ============================================================================

void main()
{
    // 1. Couleur depuis la palette
    vec3 color = PALETTE[vColorIndex];

    // 2. Variation de couleur — onde triangulaire + hash par bloc
    //    R: wave sur X, G: wave sur Z, B: wave sur Y inversé
    //    + random indépendant par composante (0-7, comme rand()%8)
    float waveR = 4.0 * abs(mod(vWorldBlockPos.x, 8.0) - 4.0);
    float waveG = 4.0 * abs(mod(vWorldBlockPos.z, 8.0) - 4.0);
    float waveB = 4.0 * abs(mod((63.0 - vWorldBlockPos.y), 8.0) - 4.0);
    float rng1 = fract(sin(dot(vWorldBlockPos.xyz, vec3(12.9898, 78.233, 45.164))) * 43758.5453) * 8.0;
    float rng2 = fract(sin(dot(vWorldBlockPos.xyz, vec3(93.9898, 67.345, 18.724))) * 28461.6271) * 8.0;
    float rng3 = fract(sin(dot(vWorldBlockPos.xyz, vec3(27.1653, 14.892, 91.537))) * 61539.2847) * 8.0;
    color += vec3(waveR + rng1, waveG + rng2, waveB + rng3) / 255.0;

    // 3. Face shading
    color *= FACE_BRIGHTNESS[vFaceDir];

    // 4. Sunblock (ombre diagonale)
    if (vShade == 0)
        color *= 0.7;

    // 5. Ambient Occlusion
    if (useAO == 1)
        color *= vAO;

    // 6. Distance fog
    float dist = length(vFragPos - cameraPos);
    float fogFactor = clamp((dist - fogStart) / (fogEnd - fogStart), 0.0, 1.0);
    color = mix(color, fogColor, fogFactor);

    FragColor = vec4(color, 1.0);
}
