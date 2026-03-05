#version 460 core

// ============================================================================
// VERTEX PULLING SHADER — Chunk2 (uint32 blocks, full RGB)
//
// SSBO : 2 × uint32 par face
//   Word 0 : x(4) y(6) z(4) face(3) sun(7) ao0(2) ao1(2) ao2(2) ao3(2)
//   Word 1 : R(8) G(8) B(8) unused(8)
// ============================================================================

layout(std430, binding = 0) readonly buffer ChunkData {
    uint faces[];
};

uniform mat4 view;
uniform mat4 projection;
uniform vec3 chunkPos;

// Sorties vers le fragment shader
out flat vec3 vColor;
out flat int vFaceDir;
out float vSunblock;         // Sunblock gradient (0.29-1.0)
out vec3 vFragPos;
out flat vec3 vWorldBlockPos;
out float vAO;

// ============================================================================
// TABLES DE LOOKUP
// ============================================================================

const int QUAD_INDICES[6] = int[6](0, 2, 1, 0, 3, 2);

const vec3 FACE_OFFSETS[24] = vec3[24](
    // Face 0: TOP (+Y)
    vec3(0, 1, 0), vec3(1, 1, 0), vec3(1, 1, 1), vec3(0, 1, 1),
    // Face 1: BOTTOM (-Y)
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

const float AO_CURVE[4] = float[4](0.25, 0.50, 0.75, 1.00);

// ============================================================================
// MAIN
// ============================================================================

void main()
{
    int faceID = gl_VertexID / 6;
    int vertID = gl_VertexID % 6;

    // Lecture des 2 words du SSBO
    uint word0 = faces[faceID * 2];
    uint word1 = faces[faceID * 2 + 1];

    // Dépacking Word 0 : position + face + sunblock + AO
    int x       = int(word0 & 0xFu);
    int y       = int((word0 >> 4u) & 0x3Fu);
    int z       = int((word0 >> 10u) & 0xFu);
    int faceDir = int((word0 >> 14u) & 0x7u);
    int sunQ    = int((word0 >> 17u) & 0x7Fu);

    int ao0 = int((word0 >> 24u) & 0x3u);
    int ao1 = int((word0 >> 26u) & 0x3u);
    int ao2 = int((word0 >> 28u) & 0x3u);
    int ao3 = int((word0 >> 30u) & 0x3u);

    // Dépacking Word 1 : couleur RGB directe
    float r = float(word1 & 0xFFu) / 255.0;
    float g = float((word1 >> 8u) & 0xFFu) / 255.0;
    float b = float((word1 >> 16u) & 0xFFu) / 255.0;

    // Position du sommet
    int cornerIdx = QUAD_INDICES[vertID];
    vec3 offset = FACE_OFFSETS[faceDir * 4 + cornerIdx];
    vec3 localPos = vec3(float(x), float(y), float(z)) + offset;
    vec3 worldPos = chunkPos + localPos;

    // AO pour ce vertex
    int aoValues[4] = int[4](ao0, ao1, ao2, ao3);
    float ao = AO_CURVE[aoValues[cornerIdx]];

    // Envoi au fragment shader
    gl_Position = projection * view * vec4(worldPos, 1.0);
    vColor = vec3(r, g, b);
    vFaceDir = faceDir;
    vSunblock = float(sunQ) / 127.0;  // Parité BetterSpades (7-bit)
    vFragPos = worldPos;
    vWorldBlockPos = chunkPos + vec3(float(x), float(y), float(z));
    vAO = ao;
}
