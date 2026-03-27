/**
 * TextureAtlas.h - Système de gestion de textures atlas pour cubes voxel
 * 
 * Atlas: terrain.png (256x256 pixels, grille 16x16 de textures 16x16)
 * 
 * Usage:
 *   1. Définir les positions des textures dans l'atlas
 *   2. Utiliser getUV() pour obtenir les coordonnées UV
 *   3. Les UV sont calculés avec flip vertical (stbi)
 */

#ifndef TEXTURE_ATLAS_H
#define TEXTURE_ATLAS_H

namespace TextureAtlas {

// ============================================================================
// CONFIGURATION DE L'ATLAS
// ============================================================================

constexpr int ATLAS_SIZE = 16;           // Nombre de textures par côté (16x16 = 256 textures)
constexpr float UV_SIZE = 1.0f / 16.0f;  // Taille d'une texture en coordonnées UV (0.0625)

// ============================================================================
// STRUCTURE POUR LES COORDONNÉES UV D'UNE TEXTURE
// ============================================================================

struct UV {
    float u0, v0;  // Coin bas-gauche
    float u1, v1;  // Coin haut-droite
};

// ============================================================================
// CALCUL DES UV POUR UNE POSITION (col, row) DANS L'ATLAS
// Note: stbi flip vertical donc row 0 → v proche de 1.0
// ============================================================================

constexpr UV getUV(int col, int row) {
    return {
        col * UV_SIZE,                    // u0 (gauche)
        1.0f - (row + 1) * UV_SIZE,       // v0 (bas, avec flip)
        (col + 1) * UV_SIZE,              // u1 (droite)
        1.0f - row * UV_SIZE              // v1 (haut, avec flip)
    };
}

// ============================================================================
// DÉFINITION DES TEXTURES POUR LE BLOC GRASS
// Modifie ces valeurs pour changer les textures utilisées
// ============================================================================

// Position dans l'atlas: (colonne, ligne)
constexpr int GRASS_SIDE_COL = 3;
constexpr int GRASS_SIDE_ROW = 0;

constexpr int DIRT_COL = 2;
constexpr int DIRT_ROW = 0;

constexpr int GRASS_TOP_COL = 8;   // Texture grise à colorier
constexpr int GRASS_TOP_ROW = 2;

// UV précalculés pour chaque face du bloc grass
constexpr UV GRASS_SIDE_UV = getUV(GRASS_SIDE_COL, GRASS_SIDE_ROW);
constexpr UV DIRT_UV = getUV(DIRT_COL, DIRT_ROW);
constexpr UV GRASS_TOP_UV = getUV(GRASS_TOP_COL, GRASS_TOP_ROW);

// ============================================================================
// COULEUR DE TINT POUR L'HERBE
// Extraite du vert des 3-4 premières lignes de grass_side
// Minecraft plains biome: #91BD59 = (145, 189, 89) / 255
// Format: RGB normalisé (0.0 - 1.0)
// ============================================================================

constexpr float GRASS_TINT_R = 0.57f;   // 145/255
constexpr float GRASS_TINT_G = 0.74f;   // 189/255
constexpr float GRASS_TINT_B = 0.35f;   // 89/255

} // namespace TextureAtlas

#endif // TEXTURE_ATLAS_H
