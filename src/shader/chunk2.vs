#version 460 core

// ============================================================================
// VERTEX PULLING SHADER — Chunk2
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
out vec3 vFragPos;

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

// ============================================================================
// MAIN
// ============================================================================

void main()
{
    // A. Qui suis-je ?
    int faceID = gl_VertexID / 6;   // Quelle face dans le SSBO ?
    int vertID = gl_VertexID % 6;   // Quel sommet du triangle (0..5) ?

    // B. Récupération et dépacking des bits
    uint data = faces[faceID];

    int x        = int(data & 0xFu);           // Bits [0-3]   : X (0-15)
    int y        = int((data >> 4u) & 0xFFu);   // Bits [4-11]  : Y (0-255)
    int z        = int((data >> 12u) & 0xFu);   // Bits [12-15] : Z (0-15)
    int faceDir  = int((data >> 16u) & 0x7u);   // Bits [16-18] : Face (0-5)
    int colorIdx = int((data >> 19u) & 0x3Fu);  // Bits [19-24] : Couleur (0-63)

    // C. Générer la position du sommet
    int cornerIdx = QUAD_INDICES[vertID];
    vec3 offset = FACE_OFFSETS[faceDir * 4 + cornerIdx];
    vec3 localPos = vec3(float(x), float(y), float(z)) + offset;
    vec3 worldPos = chunkPos + localPos;

    // D. Envoi au fragment shader
    gl_Position = projection * view * vec4(worldPos, 1.0);
    vColorIndex = colorIdx;
    vFaceDir = faceDir;
    vFragPos = worldPos;
}
