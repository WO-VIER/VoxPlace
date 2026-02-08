/**
 * Chunk.h - Structure de données pour un chunk de blocs
 * 
 * Un chunk est une section du monde de taille fixe.
 * Chaque bloc est stocké comme un uint8_t (index de couleur 0-15, 0 = air)
 */

#ifndef CHUNK_H
#define CHUNK_H

#include <glad/glad.h>
#include <vector>
#include <cstdint>
#include <cstring>

// ============================================================================
// CONFIGURATION DU CHUNK
// ============================================================================

constexpr int CHUNK_SIZE_X = 16;
constexpr int CHUNK_SIZE_Y = 32;  // Hauteur (pas de caves, juste profondeur)
constexpr int CHUNK_SIZE_Z = 16;

// Couche incassable (bedrock)
constexpr int BEDROCK_LAYER = 0;

// ============================================================================
// PALETTE DE COULEURS (16 couleurs style r/place)
// ============================================================================

constexpr int PALETTE_SIZE = 16;

// Format: RGB normalisé (0.0 - 1.0)
// Index 0 = AIR (transparent, pas rendu)
const float PALETTE[PALETTE_SIZE][3] = {
    {0.0f, 0.0f, 0.0f},       // 0: AIR (pas utilisé pour le rendu)
    {1.0f, 1.0f, 1.0f},       // 1: Blanc
    {0.9f, 0.9f, 0.9f},       // 2: Gris clair
    {0.53f, 0.53f, 0.53f},    // 3: Gris
    {0.2f, 0.2f, 0.2f},       // 4: Gris foncé
    {0.0f, 0.0f, 0.0f},       // 5: Noir
    {1.0f, 0.27f, 0.0f},      // 6: Rouge-Orange
    {1.0f, 0.6f, 0.0f},       // 7: Orange
    {1.0f, 0.84f, 0.0f},      // 8: Jaune
    {0.0f, 0.8f, 0.2f},       // 9: Vert
    {0.0f, 0.6f, 0.4f},       // 10: Vert foncé/Teal
    {0.0f, 0.62f, 1.0f},      // 11: Bleu clair
    {0.14f, 0.3f, 0.75f},     // 12: Bleu
    {0.38f, 0.18f, 0.6f},     // 13: Violet
    {1.0f, 0.4f, 0.7f},       // 14: Rose
    {0.6f, 0.4f, 0.2f},       // 15: Marron
};

// ============================================================================
// STRUCTURE VERTEX POUR LE MESH
// ============================================================================

struct ChunkVertex {
    float x, y, z;      // Position
    float r, g, b;      // Couleur (vertex color)
    float ao;           // Ambient Occlusion (0.0 - 1.0)
};

// ============================================================================
// DIRECTIONS DES FACES
// ============================================================================

enum class FaceDirection : uint8_t {
    TOP = 0,     // +Y
    BOTTOM = 1,  // -Y
    NORTH = 2,   // +Z
    SOUTH = 3,   // -Z
    EAST = 4,    // +X
    WEST = 5     // -X
};

// ============================================================================
// CLASSE CHUNK
// ============================================================================

class Chunk {
public:
    // Position du chunk dans le monde (en coordonnées de chunk)
    int chunkX, chunkZ;
    
    // Données des blocs : [x][y][z]
    // 0 = air, 1-15 = index de couleur dans la palette
    uint8_t blocks[CHUNK_SIZE_X][CHUNK_SIZE_Y][CHUNK_SIZE_Z];
    
    // Mesh OpenGL
    unsigned int VAO = 0, VBO = 0, EBO = 0;
    int vertexCount = 0;
    int indexCount = 0;
    
    // État
    bool needsMeshRebuild = true;
    bool isEmpty = true;
    
    // ========================================================================
    // CONSTRUCTEUR
    // ========================================================================
    
    Chunk(int cx = 0, int cz = 0) : chunkX(cx), chunkZ(cz) {
        // Initialiser tous les blocs à AIR
        memset(blocks, 0, sizeof(blocks));
        
        // Créer la couche bedrock (incassable)
        for (int x = 0; x < CHUNK_SIZE_X; x++) {
            for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                blocks[x][BEDROCK_LAYER][z] = 3;  // Gris = bedrock
            }
        }
    }
    
    // ========================================================================
    // ACCESSEURS
    // ========================================================================
    
    // Obtenir un bloc (avec vérification des limites)
    uint8_t getBlock(int x, int y, int z) const {
        if (x < 0 || x >= CHUNK_SIZE_X ||
            y < 0 || y >= CHUNK_SIZE_Y ||
            z < 0 || z >= CHUNK_SIZE_Z) {
            return 0;  // Air si hors limites
        }
        return blocks[x][y][z];
    }
    
    // Modifier un bloc
    bool setBlock(int x, int y, int z, uint8_t blockType) {
        // Vérifier les limites
        if (x < 0 || x >= CHUNK_SIZE_X ||
            y < 0 || y >= CHUNK_SIZE_Y ||
            z < 0 || z >= CHUNK_SIZE_Z) {
            return false;
        }
        
        // Empêcher de casser le bedrock
        if (y == BEDROCK_LAYER && blockType == 0) {
            return false;
        }
        
        blocks[x][y][z] = blockType;
        needsMeshRebuild = true;
        isEmpty = false;
        return true;
    }
    
    // ========================================================================
    // GÉNÉRATION DU MESH (Face Culling Basique)
    // ========================================================================
    
    void generateMesh() {
        std::vector<ChunkVertex> vertices;
        std::vector<unsigned int> indices;
        
        // Parcourir tous les blocs
        for (int x = 0; x < CHUNK_SIZE_X; x++) {
            for (int y = 0; y < CHUNK_SIZE_Y; y++) {
                for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                    uint8_t block = blocks[x][y][z];
                    if (block == 0) continue;  // Skip air
                    
                    // Récupérer la couleur du bloc
                    float r = PALETTE[block][0];
                    float g = PALETTE[block][1];
                    float b = PALETTE[block][2];
                    
                    // Position du bloc dans le monde
                    float wx = static_cast<float>(chunkX * CHUNK_SIZE_X + x);
                    float wy = static_cast<float>(y);
                    float wz = static_cast<float>(chunkZ * CHUNK_SIZE_Z + z);
                    
                    // Vérifier chaque face et l'ajouter si le voisin est de l'air
                    
                    // TOP (+Y)
                    if (getBlock(x, y + 1, z) == 0) {
                        addFace(vertices, indices, wx, wy, wz, FaceDirection::TOP, r, g, b);
                    }
                    
                    // BOTTOM (-Y)
                    if (getBlock(x, y - 1, z) == 0) {
                        addFace(vertices, indices, wx, wy, wz, FaceDirection::BOTTOM, r, g, b);
                    }
                    
                    // NORTH (+Z)
                    if (getBlock(x, y, z + 1) == 0) {
                        addFace(vertices, indices, wx, wy, wz, FaceDirection::NORTH, r, g, b);
                    }
                    
                    // SOUTH (-Z)
                    if (getBlock(x, y, z - 1) == 0) {
                        addFace(vertices, indices, wx, wy, wz, FaceDirection::SOUTH, r, g, b);
                    }
                    
                    // EAST (+X)
                    if (getBlock(x + 1, y, z) == 0) {
                        addFace(vertices, indices, wx, wy, wz, FaceDirection::EAST, r, g, b);
                    }
                    
                    // WEST (-X)
                    if (getBlock(x - 1, y, z) == 0) {
                        addFace(vertices, indices, wx, wy, wz, FaceDirection::WEST, r, g, b);
                    }
                }
            }
        }
        
        vertexCount = static_cast<int>(vertices.size());
        indexCount = static_cast<int>(indices.size());
        
        // Upload vers le GPU
        uploadMesh(vertices, indices);
        needsMeshRebuild = false;
    }
    
    // ========================================================================
    // RENDU
    // ========================================================================
    
    void render() const {
        if (indexCount == 0) return;
        
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0);
    }
    
    // ========================================================================
    // NETTOYAGE
    // ========================================================================
    
    void cleanup() {
        if (VAO) glDeleteVertexArrays(1, &VAO);
        if (VBO) glDeleteBuffers(1, &VBO);
        if (EBO) glDeleteBuffers(1, &EBO);
        VAO = VBO = EBO = 0;
    }
    
    ~Chunk() {
        cleanup();
    }

private:
    // ========================================================================
    // AJOUTER UNE FACE AU MESH
    // ========================================================================
    
    void addFace(std::vector<ChunkVertex>& vertices,
                 std::vector<unsigned int>& indices,
                 float x, float y, float z,
                 FaceDirection dir,
                 float r, float g, float b) {
        
        unsigned int baseIndex = static_cast<unsigned int>(vertices.size());
        float ao = 1.0f;  // TODO: Calculer l'AO
        
        // Définir les 4 vertices de la face selon la direction
        switch (dir) {
            case FaceDirection::TOP:  // +Y
                vertices.push_back({x,     y + 1, z,     r, g, b, ao});
                vertices.push_back({x + 1, y + 1, z,     r, g, b, ao});
                vertices.push_back({x + 1, y + 1, z + 1, r, g, b, ao});
                vertices.push_back({x,     y + 1, z + 1, r, g, b, ao});
                break;
                
            case FaceDirection::BOTTOM:  // -Y
                vertices.push_back({x,     y, z + 1, r, g, b, ao});
                vertices.push_back({x + 1, y, z + 1, r, g, b, ao});
                vertices.push_back({x + 1, y, z,     r, g, b, ao});
                vertices.push_back({x,     y, z,     r, g, b, ao});
                break;
                
            case FaceDirection::NORTH:  // +Z
                vertices.push_back({x,     y,     z + 1, r, g, b, ao});
                vertices.push_back({x,     y + 1, z + 1, r, g, b, ao});
                vertices.push_back({x + 1, y + 1, z + 1, r, g, b, ao});
                vertices.push_back({x + 1, y,     z + 1, r, g, b, ao});
                break;
                
            case FaceDirection::SOUTH:  // -Z
                vertices.push_back({x + 1, y,     z, r, g, b, ao});
                vertices.push_back({x + 1, y + 1, z, r, g, b, ao});
                vertices.push_back({x,     y + 1, z, r, g, b, ao});
                vertices.push_back({x,     y,     z, r, g, b, ao});
                break;
                
            case FaceDirection::EAST:  // +X
                vertices.push_back({x + 1, y,     z + 1, r, g, b, ao});
                vertices.push_back({x + 1, y + 1, z + 1, r, g, b, ao});
                vertices.push_back({x + 1, y + 1, z,     r, g, b, ao});
                vertices.push_back({x + 1, y,     z,     r, g, b, ao});
                break;
                
            case FaceDirection::WEST:  // -X
                vertices.push_back({x, y,     z,     r, g, b, ao});
                vertices.push_back({x, y + 1, z,     r, g, b, ao});
                vertices.push_back({x, y + 1, z + 1, r, g, b, ao});
                vertices.push_back({x, y,     z + 1, r, g, b, ao});
                break;
        }
        
        // Indices pour 2 triangles (quad)
        indices.push_back(baseIndex + 0);
        indices.push_back(baseIndex + 1);
        indices.push_back(baseIndex + 2);
        indices.push_back(baseIndex + 0);
        indices.push_back(baseIndex + 2);
        indices.push_back(baseIndex + 3);
    }
    
    // ========================================================================
    // UPLOAD DU MESH VERS LE GPU
    // ========================================================================
    
    void uploadMesh(const std::vector<ChunkVertex>& vertices,
                    const std::vector<unsigned int>& indices) {
        // Supprimer l'ancien mesh si existant
        cleanup();
        
        if (vertices.empty()) return;
        
        // Créer VAO, VBO, EBO
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);
        
        glBindVertexArray(VAO);
        
        // VBO
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, 
                     vertices.size() * sizeof(ChunkVertex), 
                     vertices.data(), 
                     GL_STATIC_DRAW);
        
        // EBO
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     indices.size() * sizeof(unsigned int),
                     indices.data(),
                     GL_STATIC_DRAW);
        
        // Position (location = 0)
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex), (void*)0);
        glEnableVertexAttribArray(0);
        
        // Color (location = 1)
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex), 
                              (void*)offsetof(ChunkVertex, r));
        glEnableVertexAttribArray(1);
        
        // AO (location = 2)
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex),
                              (void*)offsetof(ChunkVertex, ao));
        glEnableVertexAttribArray(2);
        
        glBindVertexArray(0);
    }
};

#endif // CHUNK_H
