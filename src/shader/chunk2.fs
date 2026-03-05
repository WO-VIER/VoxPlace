#version 460 core

// ============================================================================
// FRAGMENT SHADER — Chunk2 (uint32 blocks, full RGB)
//
// Reçoit la couleur RGB directe du vertex shader.
// Applique : fondu → face_shading → sunblock → AO → fog
// ============================================================================

out vec4 FragColor;

in flat vec3 vColor;
in flat int vFaceDir;
in float vSunblock;          // Sunblock gradient continu (0.29-1.0)
in vec3 vFragPos;
in flat vec3 vWorldBlockPos;
in float vAO;

uniform vec3 cameraPos;
uniform float fogStart;
uniform float fogEnd;
uniform vec3 fogColor;
uniform int useAO;

// ============================================================================
// FACE SHADING — facteurs BetterSpades (chunk.c:555-560)
// ============================================================================

const float FACE_BRIGHTNESS[6] = float[6](
    1.000,  // 0: TOP
    0.500,  // 1: BOTTOM
    0.875,  // 2: NORTH
    0.625,  // 3: SOUTH
    0.750,  // 4: EAST
    0.750   // 5: WEST
);

// ============================================================================
// MAIN
// ============================================================================

void main()
{
    // 1. Couleur RGB directe
    vec3 color = vColor;

    // 2. Variation de couleur par position (fondu hash)
    //    Appliqué à tous les blocs terrain (couleur CPU-side déjà variée,
    //    le hash GPU ajoute un subtil grain supplémentaire)
    float h1 = fract(sin(dot(vWorldBlockPos.xyz, vec3(12.9898, 78.233, 45.164))) * 43758.5453);
    float h2 = fract(sin(dot(vWorldBlockPos.xyz, vec3(93.9898, 67.345, 18.724))) * 28461.6271);
    float h3 = fract(sin(dot(vWorldBlockPos.xyz, vec3(27.1653, 14.892, 91.537))) * 61539.2847);
    color += vec3(h1, h2, h3) * 0.03;  // ~1.5% variation GPU en plus

    // 3. Face shading
    color *= FACE_BRIGHTNESS[vFaceDir];

    // 4. Sunblock gradient continu (0.29-1.0 comme BetterSpades)
    color *= vSunblock;

    // 5. Ambient Occlusion
    if (useAO == 1)
        color *= vAO;

    // 6. Distance fog
    float dist = length(vFragPos - cameraPos);
    float fogFactor = clamp((dist - fogStart) / (fogEnd - fogStart), 0.0, 1.0);
    color = mix(color, fogColor, fogFactor);

    FragColor = vec4(color, 1.0);
}
