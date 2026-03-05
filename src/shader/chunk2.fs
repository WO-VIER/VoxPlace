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
in float vAO;

uniform vec3 cameraPos;
uniform float fogStart;
uniform float fogEnd;
uniform vec3 fogColor;
uniform int useAO;
uniform int debugSunblockOnly;

// ============================================================================
// FACE SHADING — facteurs BetterSpades (chunk.c:555-560)
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
    // 1. Couleur RGB directe
    vec3 color = vColor;

    if (debugSunblockOnly == 1) {
        FragColor = vec4(vec3(vSunblock), 1.0);
        return;
    }

    // 2. Face shading
    color *= FACE_BRIGHTNESS[vFaceDir];

    // 3. Sunblock gradient continu (0.29-1.0 comme BetterSpades)
    color *= vSunblock;

    // 4. Ambient Occlusion
    if (useAO == 1)
        color *= vAO;

    // 5. Distance fog
    float dist = length(vFragPos - cameraPos);
    float fogFactor = clamp((dist - fogStart) / (fogEnd - fogStart), 0.0, 1.0);
    color = mix(color, fogColor, fogFactor);

    FragColor = vec4(color, 1.0);
}
