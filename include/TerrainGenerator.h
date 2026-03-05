#ifndef TERRAIN_GENERATOR_H
#define TERRAIN_GENERATOR_H

#include <FastNoiseLite.h>
#include <Chunk2.h>
#include <algorithm>
#include <cstdlib>
#include <cmath>

// ============================================================================
// TerrainGenerator — Génère un terrain naturel avec Simplex Noise
//
// Utilise 2 couches de bruit combinées :
//
//   Hauteur(x,z) = baseHeight
//                + continent(x,z) × continentAmp   ← collines douces
//                + detail(x,z)    × detailAmp       ← micro-variations
//
// Les couleurs sont stockées en RGB direct (uint32_t) avec :
//   - dirt_color_table[9] (gradient profondeur BetterSpades)
//   - Onde triangulaire + hash déterministe par position
//   - mod8() pour continuité aux coordonnées négatives
// ============================================================================

class TerrainGenerator
{
public:
	int baseHeight = 20;
	float continentAmp = 15.0f;
	float detailAmp = 3.0f;

	TerrainGenerator(int seed = 42)
	{
		continent.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
		continent.SetSeed(seed);
		continent.SetFrequency(0.005f);

		detail.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
		detail.SetSeed(seed + 1);
		detail.SetFrequency(0.02f);
	}

	int getHeight(int worldX, int worldZ) const
	{
		float wx = static_cast<float>(worldX);
		float wz = static_cast<float>(worldZ);
		float h = static_cast<float>(baseHeight);
		h += continent.GetNoise(wx, wz) * continentAmp;
		h += detail.GetNoise(wx, wz) * detailAmp;
		return std::clamp(static_cast<int>(h), 1, static_cast<int>(CHUNK_SIZE_Y) - 1);
	}

	// ════════════════════════════════════════════════════════════════════
	// dirt_color_table — exact copie de BetterSpades (map.c:681-682)
	// 9 couleurs interpolées par profondeur Y (surface → fond)
	// ════════════════════════════════════════════════════════════════════
	static constexpr int DIRT_COLORS[9] = {
		0x506050, // 0: Surface — gris-vert
		0x605848, // 1: Brun clair
		0x705040, // 2: Brun
		0x804838, // 3: Brun-rouge
		0x704030, // 4: Brun foncé
		0x603828, // 5: Terre foncée
		0x503020, // 6: Terre très foncée
		0x402818, // 7: Presque noir
		0x302010  // 8: Fond
	};

	// ════════════════════════════════════════════════════════════════════
	// Modulo positif — C++ : -1 % 8 = -1 (cassé pour la wave)
	// mod8 : -1 → 7 (continu à travers les coordonnées négatives)
	// ════════════════════════════════════════════════════════════════════
	static int mod8(int v)
	{
		return ((v % 8) + 8) % 8;
	}

	// ════════════════════════════════════════════════════════════════════
	// Hash déterministe par position monde — remplace rand()
	//
	// BetterSpades utilise ms_rand() (séquentiel) → chaque bloc a un rng
	// différent qui casse le pattern de l'onde triangulaire.
	// Nous utilisons un hash positionnel avec amplitude élargie (0-23)
	// pour que le bruit domine l'onde (amplitude 0-8 réduite).
	// ════════════════════════════════════════════════════════════════════
	static int posHash(int x, int y, int z)
	{
		int h = (x * 73856093) ^ (y * 19349663) ^ (z * 83492791);
		if (h < 0) h = -h;
		return h % 24; // 0-23 (3× l'amplitude originale de BetterSpades)
	}

	static int lerpChannel(int a, int b, int amt)
	{
		return a + (b - a) * amt / 8;
	}

	// ════════════════════════════════════════════════════════════════════
	// map_dirt_color — basé sur BetterSpades (map.c:685-700)
	//
	// Onde triangulaire RÉDUITE (2× au lieu de 4×) + hash ÉLARGI (0-23)
	// → le bruit domine le pattern répétitif → pas de lignes visibles
	// ════════════════════════════════════════════════════════════════════
	static uint32_t dirtColor(int x, int y, int z)
	{
		int invY = CHUNK_SIZE_Y - 1 - y;
		int slice = invY / 8;
		int lerp_amt = invY % 8;

		if (slice < 0) slice = 0;
		if (slice >= 8) slice = 7;

		int base = DIRT_COLORS[slice];
		int next = DIRT_COLORS[slice + 1];

		int red   = lerpChannel((base >> 16) & 0xFF, (next >> 16) & 0xFF, lerp_amt);
		int green = lerpChannel((base >> 8) & 0xFF, (next >> 8) & 0xFF, lerp_amt);
		int blue  = lerpChannel(base & 0xFF, next & 0xFF, lerp_amt);

		int rng = posHash(x, y, z);
		red   += 2 * std::abs(mod8(x) - 4) + rng;
		green += 2 * std::abs(mod8(z) - 4) + rng;
		blue  += 2 * std::abs(mod8(invY) - 4) + rng;

		return Chunk2::makeColor(red, green, blue);
	}

	// ════════════════════════════════════════════════════════════════════
	// Couleur herbe — surface verte avec variation
	// ════════════════════════════════════════════════════════════════════
	static uint32_t grassColor(int x, int y, int z)
	{
		// Base = DIRT_COLORS[0] = 0x506050 (R=80, G=96, B=80)
		int rng = posHash(x, y, z);
		int r = 75 + 2 * std::abs(mod8(x) - 4) + rng;
		int g = 92 + 2 * std::abs(mod8(z) - 4) + rng;
		int b = 75 + 2 * std::abs(mod8(CHUNK_SIZE_Y - 1 - y) - 4) + rng;
		return Chunk2::makeColor(r, g, b);
	}

	// ════════════════════════════════════════════════════════════════════
	// Couleur pierre — gris avec variation
	// ════════════════════════════════════════════════════════════════════
	static uint32_t stoneColor(int x, int y, int z)
	{
		int rng = posHash(x, y, z);
		int base = 80 + 2 * std::abs(mod8(x) - 4) + rng;
		int r = base;
		int g = base;
		int b = base + 5;
		return Chunk2::makeColor(r, g, b);
	}

	// ════════════════════════════════════════════════════════════════════
	// Remplit un chunk avec le terrain (herbe/terre/pierre en RGB direct)
	// ════════════════════════════════════════════════════════════════════
	void fillChunk(Chunk2 &chunk) const
	{
		for (int x = 0; x < CHUNK_SIZE_X; x++)
		{
			for (int z = 0; z < CHUNK_SIZE_Z; z++)
			{
				int worldX = chunk.chunkX * CHUNK_SIZE_X + x;
				int worldZ = chunk.chunkZ * CHUNK_SIZE_Z + z;
				int height = getHeight(worldX, worldZ);

				// Bedrock (y=0)
				chunk.blocks[x][0][z] = stoneColor(worldX, 0, worldZ);

				for (int y = 1; y <= height && y < CHUNK_SIZE_Y; y++)
				{
					if (y == height)
					{
						chunk.blocks[x][y][z] = grassColor(worldX, y, worldZ);
					}
					else if (y > height - 3)
					{
						chunk.blocks[x][y][z] = dirtColor(worldX, y, worldZ);
					}
					else
					{
						chunk.blocks[x][y][z] = stoneColor(worldX, y, worldZ);
					}
				}
			}
		}
		chunk.needsMeshRebuild = true;
		chunk.isEmpty = false;
	}

private:
	FastNoiseLite continent;
	FastNoiseLite detail;
};

#endif // TERRAIN_GENERATOR_H
