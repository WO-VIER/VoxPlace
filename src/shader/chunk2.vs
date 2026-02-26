#version 460 core

// ============================================================================
// VERTEX PULLING SHADER — Chunk2 (Phase 2 : Y=64, 64 couleurs, sunblock)
// Pas de VBO, pas d'attributs. Tout vient du SSBO.
// ============================================================================

// SSBO : tableau de faces packées (1 uint32 par face)
layout(std430, binding = 0) readonly buffer ChunkData {
    uint faces[];
};

uniform mat4 view;
uniform mat4 projection;
uniform vec3 chunkPos; // Position monde du chunk (remplace model matrix)

// Sorties vers le fragment shader
out flat int vColorIndex;
out flat int vFaceDir;
out flat int vShade;    // 1 = éclairé, 0 = ombré (sunblock)
out vec3 vFragPos;
out flat vec3 vWorldBlockPos; // Position monde du bloc (flat = même pour tous les fragments d'une face)
out float vAO;  // Valeur AO interpolée (0.0 = sombre, 1.0 = lumineux)

// ============================================================================
// TABLES DE LOOKUP (pas de if/else, tout est précalculé)
// ============================================================================

// 2 triangles par face = 6 sommets, indices dans le quad (0,1,2,3)
const int QUAD_INDICES[6] = int[6](0, 1, 2, 0, 2, 3);

// Offsets des 4 coins pour chaque direction de face (6 faces × 4 coins)
// Chaque vec3 = décalage depuis la position du bloc (x, y, z)
const vec3 FACE_OFFSETS[24] = vec3[24](
    // Face 0: TOP (+Y) — le dessus du cube
    vec3(0, 1, 0), vec3(1, 1, 0), vec3(1, 1, 1), vec3(0, 1, 1),
    // Face 1: BOTTOM (-Y) — le dessous
    vec3(0, 0, 1), vec3(1, 0, 1), vec3(1, 0, 0), vec3(0, 0, 0),
    // Face 2: NORTH (+Z)
    vec3(0, 0, 1), vec3(0, 1, 1), vec3(1, 1, 1), vec3(1, 0, 1),
    // Face 3: SOUTH (-Z)
    vec3(1, 0, 0), vec3(1, 1, 0), vec3(0, 1, 0), vec3(0, 0, 0),
    // Face 4: EAST (+X)
    vec3(1, 0, 1), vec3(1, 1, 1), vec3(1, 1, 0), vec3(1, 0, 0),
    // Face 5: WEST (-X)
    vec3(0, 0, 0), vec3(0, 1, 0), vec3(0, 1, 1), vec3(0, 0, 1)
);

// AO : 4 niveaux → facteur de luminosité
// AO=0 → très sombre (coin occluded), AO=3 → plein éclairage
const float AO_CURVE[4] = float[4](0.20, 0.50, 0.75, 1.00);

// ============================================================================
// MAIN
// ============================================================================

void main()
{
    // A. Qui suis-je ?
    int faceID = gl_VertexID / 6;   // Quelle face dans le SSBO ?
    int vertID = gl_VertexID % 6;   // Quel sommet du triangle (0..5) ?

    // B. Récupération et dépacking des bits
    //    Phase 2 layout : x(4) y(6) z(4) face(3) color(6) shade(1) ao(8)
    uint data = faces[faceID];

    int x        = int(data & 0xFu);             // Bits [0-3]   : X (0-15)
    int y        = int((data >> 4u) & 0x3Fu);     // Bits [4-9]   : Y (0-63)
    int z        = int((data >> 10u) & 0xFu);     // Bits [10-13] : Z (0-15)
    int faceDir  = int((data >> 14u) & 0x7u);     // Bits [14-16] : Face (0-5)
    int colorIdx = int((data >> 17u) & 0x3Fu) + 1; // Bits [17-22] : Color (+1 car packed color-1)
    int shade    = int((data >> 23u) & 0x1u);     // Bits [23]    : Shade (sunblock)

    // AO per-vertex : 4 valeurs × 2 bits chacune
    int ao0 = int((data >> 24u) & 0x3u);  // Bits [24-25]
    int ao1 = int((data >> 26u) & 0x3u);  // Bits [26-27]
    int ao2 = int((data >> 28u) & 0x3u);  // Bits [28-29]
    int ao3 = int((data >> 30u) & 0x3u);  // Bits [30-31]

    // C. Générer la position du sommet
    int cornerIdx = QUAD_INDICES[vertID];
    vec3 offset = FACE_OFFSETS[faceDir * 4 + cornerIdx];
    vec3 localPos = vec3(float(x), float(y), float(z)) + offset;
    vec3 worldPos = chunkPos + localPos;

    // D. AO pour ce vertex spécifique
    // cornerIdx indique quel coin du quad (0-3) → quelle valeur AO
    int aoValues[4] = int[4](ao0, ao1, ao2, ao3);
    float ao = AO_CURVE[aoValues[cornerIdx]];

    // E. Envoi au fragment shader
    gl_Position = projection * view * vec4(worldPos, 1.0);
    vColorIndex = colorIdx;
    vFaceDir = faceDir;
    vShade = shade;
    vFragPos = worldPos;
    vWorldBlockPos = chunkPos + vec3(float(x), float(y), float(z));
    vAO = ao;  // Interpolé par le rasterizer entre les 3 sommets du triangle
}
