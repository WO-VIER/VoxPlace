#ifndef TERRAIN_GENERATOR_H
#define TERRAIN_GENERATOR_H

#include <FastNoiseLite.h>
#include <Chunk2.h>
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <cstdint>

// ============================================================================
// TerrainGenerator — Génère un relief naturel (style "elevation" organique)
//
// Utilise plusieurs couches de bruit combinées :
//
//   1) Domain warp (déforme les coordonnées pour casser les motifs répétitifs)
//   2) Continent noise (macro relief)
//   3) Hill noise (collines intermédiaires)
//   4) Ridged noise (massifs et crêtes)
//   5) Detail noise (micro-relief)
//
// Les couleurs sont stockées en RGB direct (uint32_t) avec :
//   - dirt_color_table[9] (gradient profondeur BetterSpades)
//   - Onde triangulaire + hash déterministe par position
//   - mod8() pour continuité aux coordonnées négatives
// ============================================================================

class TerrainGenerator
{
public:
	int baseHeight = 22;
	float continentAmp = 14.0f;
	float hillAmp = 4.0f;
	float mountainAmp = 10.0f;
	float detailAmp = 0.8f;

	TerrainGenerator(int seed = 42)
	{
		continent.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
		continent.SetSeed(seed);
		continent.SetFrequency(0.0018f);
		continent.SetFractalType(FastNoiseLite::FractalType_FBm);
		continent.SetFractalOctaves(4);
		continent.SetFractalLacunarity(2.0f);
		continent.SetFractalGain(0.5f);

		hills.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
		hills.SetSeed(seed + 11);
		hills.SetFrequency(0.0075f);
		hills.SetFractalType(FastNoiseLite::FractalType_FBm);
		hills.SetFractalOctaves(3);
		hills.SetFractalLacunarity(2.0f);
		hills.SetFractalGain(0.55f);

		ridges.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
		ridges.SetSeed(seed + 23);
		ridges.SetFrequency(0.0042f);
		ridges.SetFractalType(FastNoiseLite::FractalType_Ridged);
		ridges.SetFractalOctaves(4);
		ridges.SetFractalLacunarity(2.0f);
		ridges.SetFractalGain(0.5f);

		detail.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
		detail.SetSeed(seed + 1);
		detail.SetFrequency(0.028f);
		detail.SetFractalType(FastNoiseLite::FractalType_FBm);
		detail.SetFractalOctaves(2);
		detail.SetFractalLacunarity(2.0f);
		detail.SetFractalGain(0.6f);

		biome.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
		biome.SetSeed(seed + 57);
		biome.SetFrequency(0.0028f);
		biome.SetFractalType(FastNoiseLite::FractalType_FBm);
		biome.SetFractalOctaves(2);
		biome.SetFractalLacunarity(2.0f);
		biome.SetFractalGain(0.6f);

		warp.SetSeed(seed + 101);
		warp.SetDomainWarpType(FastNoiseLite::DomainWarpType_OpenSimplex2);
		warp.SetFractalType(FastNoiseLite::FractalType_DomainWarpProgressive);
		warp.SetFractalOctaves(2);
		warp.SetFrequency(0.0025f);
		warp.SetDomainWarpAmp(18.0f);
	}

	static float saturate(float v)
	{
		if (v < 0.0f)
			return 0.0f;
		if (v > 1.0f)
			return 1.0f;
		return v;
	}

	static float smoothstep01(float t)
	{
		t = saturate(t);
		return t * t * (3.0f - 2.0f * t);
	}

	int getHeight(int worldX, int worldZ) const
	{
		float wx = static_cast<float>(worldX);
		float wz = static_cast<float>(worldZ);

		float warpedX = wx;
		float warpedZ = wz;
		warp.DomainWarp(warpedX, warpedZ);

		float continentNoise = continent.GetNoise(warpedX, warpedZ);
		float continent01 = 0.5f * (continentNoise + 1.0f);
		float continentMask = smoothstep01((continent01 - 0.28f) / 0.58f);
		float mountainMask = smoothstep01((continent01 - 0.60f) / 0.35f);

		float hillNoise = hills.GetNoise(warpedX, warpedZ);
		float ridgeNoise = ridges.GetNoise(warpedX, warpedZ);
		float detailNoise = detail.GetNoise(wx, wz);

		float h = static_cast<float>(baseHeight);
		h += (continent01 - 0.5f) * continentAmp;
		h += hillNoise * hillAmp * (0.35f + 0.65f * continentMask);

		float ridge01 = 0.5f * (ridgeNoise + 1.0f);
		float ridgeShape = ridge01 * ridge01;
		h += ridgeShape * mountainAmp * mountainMask;

		h += detailNoise * detailAmp;

		// Applatit les plaines pour un look plus "Minecraft-like" tout en gardant
		// des zones montagneuses localisées.
		float plainMask = 1.0f - mountainMask;
		float terraceMix = 0.45f * plainMask;
		float terracedHeight = std::floor(h * 0.5f) * 2.0f;
		h = h * (1.0f - terraceMix) + terracedHeight * terraceMix;
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
	// RNG déterministe par position monde.
	// BetterSpades utilise ms_rand()%8 (séquentiel), mais ici on garde
	// un résultat stable quelle que soit l'ordre de génération des chunks.
	// ════════════════════════════════════════════════════════════════════
	static int posRand8(int x, int y, int z)
	{
		uint32_t ux = static_cast<uint32_t>(x);
		uint32_t uy = static_cast<uint32_t>(y);
		uint32_t uz = static_cast<uint32_t>(z);
		uint32_t h = ux * 73856093u ^ uy * 19349663u ^ uz * 83492791u;
		h ^= h >> 13;
		h *= 0x5bd1e995u;
		h ^= h >> 15;
		return static_cast<int>(h & 7u); // 0-7 comme ms_rand()%8
	}

	static int lerp(int a, int b, int amt)
	{
		return a + (b - a) * amt / 8;
	}

	static int terrainRand(int x, int y, int z)
	{
		// Grain léger (0..3) pour conserver du détail sans bruit agressif.
		return posRand8(x, y, z) / 2;
	}

	static constexpr int TERRAIN_WAVE_XZ = 1;
	static constexpr int TERRAIN_WAVE_Y = 2;
	static constexpr int GRASS_COLORS[3] = {
		0x336633, // ancien index 42 (herbe vive)
		0x2E592E, // ancien index 43 (herbe moyenne)
		0x294D29  // ancien index 44 (herbe sombre)
	};
	static constexpr int STONE_COLORS[3] = {
		0x66666B, // ancien index 45 (pierre claire)
		0x59595E, // ancien index 46 (pierre)
		0x4D4D52  // ancien index 47 (pierre sombre)
	};

	static uint32_t colorFromHex(int rgb)
	{
		int r = (rgb >> 16) & 0xFF;
		int g = (rgb >> 8) & 0xFF;
		int b = rgb & 0xFF;
		return Chunk2::makeColor(r, g, b);
	}

	// ════════════════════════════════════════════════════════════════════
	// map_dirt_color — style BetterSpades adouci pour terrain procédural
	// Onde triangulaire adoucie: X/Z léger + Y un peu plus marqué.
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

		int red = lerp(base & 0xFF0000, next & 0xFF0000, lerp_amt) >> 16;
		int green = lerp(base & 0x00FF00, next & 0x00FF00, lerp_amt) >> 8;
		int blue = lerp(base & 0x0000FF, next & 0x0000FF, lerp_amt);

		int rng = terrainRand(x, y, z);
		red += TERRAIN_WAVE_XZ * std::abs(mod8(x) - 4) + rng;
		green += TERRAIN_WAVE_XZ * std::abs(mod8(z) - 4) + rng;
		blue += TERRAIN_WAVE_Y * std::abs(mod8(invY) - 4) + rng;

		return Chunk2::makeColor(red, green, blue);
	}

	// ════════════════════════════════════════════════════════════════════
	// Couleur herbe — base palette "main" + variation biome
	// ════════════════════════════════════════════════════════════════════
	static uint32_t grassColor(int x, int y, int z, float biome01)
	{
		int grassVariant = 0;
		if (y < 15)
			grassVariant = 2;
		else if (y < 22)
			grassVariant = 1;

		uint32_t base = colorFromHex(GRASS_COLORS[grassVariant]);
		int r = Chunk2::colorR(base);
		int g = Chunk2::colorG(base);
		int b = Chunk2::colorB(base);

		int invY = CHUNK_SIZE_Y - 1 - y;
		int rng = terrainRand(x, y, z);
		int lush = static_cast<int>((biome01 - 0.5f) * 18.0f);

		r += TERRAIN_WAVE_XZ * std::abs(mod8(x) - 4) + rng + lush / 3;
		g += TERRAIN_WAVE_XZ * std::abs(mod8(z) - 4) + rng + lush;
		b += TERRAIN_WAVE_Y * std::abs(mod8(invY) - 4) + rng + lush / 4;
		return Chunk2::makeColor(r, g, b);
	}

	// ════════════════════════════════════════════════════════════════════
	// Couleur pierre — reprend les 3 teintes de la branche main
	// ════════════════════════════════════════════════════════════════════
	static uint32_t stoneColor(int x, int y, int z)
	{
		int variant = posRand8(x, y, z) % 3;
		uint32_t baseColor = colorFromHex(STONE_COLORS[variant]);
		int r = Chunk2::colorR(baseColor);
		int g = Chunk2::colorG(baseColor);
		int b = Chunk2::colorB(baseColor);
		int rng = terrainRand(x, y, z);
		int grain = std::abs(mod8(x + z) - 4);
		r += grain / 2 + rng;
		g += grain / 2 + rng;
		b += grain / 2 + rng + 1;
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
				float biome01 = 0.5f * (biome.GetNoise(
					static_cast<float>(worldX),
					static_cast<float>(worldZ)) + 1.0f);

				int eastH = getHeight(worldX + 1, worldZ);
				int westH = getHeight(worldX - 1, worldZ);
				int northH = getHeight(worldX, worldZ + 1);
				int southH = getHeight(worldX, worldZ - 1);
				int slope = std::abs(eastH - westH) + std::abs(northH - southH);

				bool rockySurface = false;
				if (slope >= 7)
					rockySurface = true;
				if (height >= 54)
					rockySurface = true;

				int dirtDepth = 3;
				if (biome01 > 0.65f)
					dirtDepth = 4;
				if (rockySurface)
					dirtDepth = 1;

				// Bedrock (y=0)
				chunk.blocks[x][0][z] = stoneColor(worldX, 0, worldZ);

				for (int y = 1; y <= height && y < CHUNK_SIZE_Y; y++)
				{
					if (y == height)
					{
						if (rockySurface)
							chunk.blocks[x][y][z] = stoneColor(worldX, y, worldZ);
						else
							chunk.blocks[x][y][z] = grassColor(worldX, y, worldZ, biome01);
					}
					else if (y > height - dirtDepth)
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
	FastNoiseLite hills;
	FastNoiseLite ridges;
	FastNoiseLite detail;
	FastNoiseLite biome;
	FastNoiseLite warp;
};

#endif // TERRAIN_GENERATOR_H
