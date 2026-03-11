#ifndef TERRAIN_GENERATOR_H
#define TERRAIN_GENERATOR_H

#include <FastNoiseLite.h>
#include <Chunk2.h>
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <cstdint>

// ============================================================================
// TerrainGenerator — Génère un relief Minecraft-style avec fondu de couleurs
//
// Shape : plaines plates dominantes + collines localisées + terracing agressif
//   1) Domain warp (déforme les coordonnées)
//   2) Continent noise (macro relief réduit → plaines étendues)
//   3) Hill noise (haute fréquence → collines localisées Minecraft)
//   4) Ridged noise (montagnes rares)
//   5) Detail noise (micro-relief)
//   6) Terracing agressif en plaine (plateaux nets comme MC Beta)
//
// Couleurs : fondu naturel multi-couche
//   - Herbe biome-aware (altitude + biome noise → vert-jaune / vert / gris-alpin)
//   - Terre visible sur les pentes raides (slope >= 4)
//   - dirt_color_table enrichie (brun chaud en surface)
//   - Pierre claire en haute altitude
//   - Onde triangulaire + hash déterministe par position (BetterSpades)
// ============================================================================

class TerrainGenerator
{
public:
	int baseHeight = 25;
	float continentAmp = 12.0f;    // Macro relief (plaines ↔ highlands)
	float hillAmp = 8.0f;          // Collines localisées (±8 blocs)
	float mountainAmp = 18.0f;     // Montagnes ridged
	float detailAmp = 1.5f;        // Micro-relief visible

	TerrainGenerator(int seed = 42)
	{
		continent.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
		continent.SetSeed(seed);
		continent.SetFrequency(0.004f);  // Était 0.0018 → 1 cycle = 250 blocs (visible dans la map)
		continent.SetFractalType(FastNoiseLite::FractalType_FBm);
		continent.SetFractalOctaves(4);
		continent.SetFractalLacunarity(2.0f);
		continent.SetFractalGain(0.5f);

		hills.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
		hills.SetSeed(seed + 11);
		hills.SetFrequency(0.008f);  // 1 cycle ≈ 125 blocs → collines MC
		hills.SetFractalType(FastNoiseLite::FractalType_FBm);
		hills.SetFractalOctaves(3);
		hills.SetFractalLacunarity(2.0f);
		hills.SetFractalGain(0.50f);

		ridges.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
		ridges.SetSeed(seed + 23);
		ridges.SetFrequency(0.005f);  // Légèrement plus fréquent
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
		warp.SetFrequency(0.003f);
		warp.SetDomainWarpAmp(25.0f);  // Plus de déformation → casse les grilles
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

		// mountainMask : seuil bas (0.45) pour que les montagnes apparaissent
		// dans ~30% du terrain (au lieu de ~10% avec 0.60)
		float mountainMask = smoothstep01((continent01 - 0.45f) / 0.35f);

		float hillNoise = hills.GetNoise(warpedX, warpedZ);
		float ridgeNoise = ridges.GetNoise(warpedX, warpedZ);
		float detailNoise = detail.GetNoise(wx, wz);

		// ── Base + continent ──
		// Le continent donne le macro-relief : plaines basses ↔ highlands
		float h = static_cast<float>(baseHeight);
		h += (continent01 - 0.5f) * continentAmp;

		// ── Collines (±hillAmp) ──
		// PAS de abs() : on veut des vallées ET des collines
		// → variation réelle de ±8 blocs, c'est ça qui donne le look MC
		h += hillNoise * hillAmp;

		// ── Montagnes ridged ──
		// Apparaissent où mountainMask > 0 (continent01 > 0.45)
		// Ridged² donne des pics pointus
		float ridge01 = 0.5f * (ridgeNoise + 1.0f);
		float ridgeShape = ridge01 * ridge01;
		h += ridgeShape * mountainAmp * mountainMask;

		// ── Micro-détail ──
		h += detailNoise * detailAmp;

		return std::clamp(static_cast<int>(h), 1, static_cast<int>(CHUNK_SIZE_Y) - 1);
	}

	// ════════════════════════════════════════════════════════════════════
	// dirt_color_table — enrichie par rapport à BetterSpades
	// Surface plus chaude (brun-ocre) → fond sombre
	// ════════════════════════════════════════════════════════════════════
	static constexpr int DIRT_COLORS[9] = {
		0x6B5A34, // 0: Surface — brun-ocre chaud (était gris-vert 0x506050)
		0x625240, // 1: Brun clair chaud
		0x5A4838, // 2: Brun
		0x7A4430, // 3: Brun-rouge
		0x6A3C2A, // 4: Brun foncé
		0x5A3424, // 5: Terre foncée
		0x4A2C1C, // 6: Terre très foncée
		0x3A2414, // 7: Presque noir
		0x2A1C0C  // 8: Fond
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

	// ════════════════════════════════════════════════════════════════════
	// Micro-variation : RNG pur par position (pas d'onde triangulaire)
	// Chaque bloc reçoit un shift pseudo-aléatoire sur chaque canal RGB
	// → UNE couleur perçue globalement, mais chaque bloc est unique
	// → Pas de motif visible (contrairement à mod8 qui faisait des lignes)
	// ════════════════════════════════════════════════════════════════════

	// UNE couleur de base herbe — vert naturel (ton moyen, pas trop vif)
	static constexpr int GRASS_BASE = 0x4A7A2E;

	// UNE couleur de base pierre — gris neutre
	static constexpr int STONE_BASE = 0x5E5E64;

	static uint32_t colorFromHex(int rgb)
	{
		int r = (rgb >> 16) & 0xFF;
		int g = (rgb >> 8) & 0xFF;
		int b = rgb & 0xFF;
		return Chunk2::makeColor(r, g, b);
	}

	// ════════════════════════════════════════════════════════════════════
	// Micro-variation RGB — hash RNG uniquement
	//
	// Prend une couleur de base et ajoute un shift aléatoire par canal.
	// Utilise 3 hash avec des seeds différentes pour R, G, B
	// → variation indépendante par canal, pas de motif visible.
	// ════════════════════════════════════════════════════════════════════
	static uint32_t microVary(int baseHex, int x, int y, int z)
	{
		int r = (baseHex >> 16) & 0xFF;
		int g = (baseHex >> 8) & 0xFF;
		int b = baseHex & 0xFF;

		// 3 hash indépendants pour R, G, B (seeds différentes)
		int rngR = posRand8(x, y, z);           // 0-7
		int rngG = posRand8(x + 37, y, z + 59); // 0-7, seed décalée
		int rngB = posRand8(x + 71, y + 13, z); // 0-7, seed décalée

		r += rngR - 3;  // shift ±3-4 (centré)
		g += rngG - 3;
		b += rngB - 3;

		return Chunk2::makeColor(r, g, b);
	}

	// ════════════════════════════════════════════════════════════════════
	// map_dirt_color — gradient vertical (surface chaude → fond sombre)
	// + micro-variations par bloc
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

		// Micro-variation RNG pur (pas d'onde triangulaire)
		int rngR = posRand8(x, y, z);
		int rngG = posRand8(x + 37, y, z + 59);
		int rngB = posRand8(x + 71, y + 13, z);
		red += rngR - 3;
		green += rngG - 3;
		blue += rngB - 3;

		return Chunk2::makeColor(red, green, blue);
	}

	// ════════════════════════════════════════════════════════════════════
	// Couleur herbe — UNE couleur de base + micro-variations
	//
	// Tous les blocs d'herbe ont la même couleur perçue (GRASS_BASE),
	// mais chaque bloc individuel a un RGB légèrement différent.
	// Le biome noise module très subtilement la teinte (±5 sur le vert).
	// ════════════════════════════════════════════════════════════════════
	static uint32_t grassColor(int x, int y, int z, float biome01)
	{
		int r = (GRASS_BASE >> 16) & 0xFF;
		int g = (GRASS_BASE >> 8) & 0xFF;
		int b = GRASS_BASE & 0xFF;

		// Micro-variation RNG pur (3 hash indépendants)
		int rngR = posRand8(x, y, z);
		int rngG = posRand8(x + 37, y, z + 59);
		int rngB = posRand8(x + 71, y + 13, z);

		r += rngR - 3;
		g += rngG - 3;
		b += rngB - 3;

		// Biome : subtile modulation du vert (±5 max)
		int lush = static_cast<int>((biome01 - 0.5f) * 10.0f);
		g += lush;

		return Chunk2::makeColor(r, g, b);
	}

	// ════════════════════════════════════════════════════════════════════
	// Couleur terre/herbe transitionnelle — entre surface herbe et dirt
	// Mélange la teinte herbe et la teinte terre pour un fondu naturel
	// ════════════════════════════════════════════════════════════════════
	static uint32_t transitionColor(int x, int y, int z, float biome01)
	{
		uint32_t grass = grassColor(x, y, z, biome01);
		uint32_t dirt = dirtColor(x, y, z);

		// 50/50 entre herbe et terre
		int r = (Chunk2::colorR(grass) + Chunk2::colorR(dirt)) / 2;
		int g = (Chunk2::colorG(grass) + Chunk2::colorG(dirt)) / 2;
		int b = (Chunk2::colorB(grass) + Chunk2::colorB(dirt)) / 2;
		return Chunk2::makeColor(r, g, b);
	}

	// ════════════════════════════════════════════════════════════════════
	// Couleur pierre — UNE couleur de base + micro-variations
	// ════════════════════════════════════════════════════════════════════
	static uint32_t stoneColor(int x, int y, int z)
	{
		return microVary(STONE_BASE, x, y, z);
	}

	// ════════════════════════════════════════════════════════════════════
	// Remplit un chunk avec le terrain — fondu couleur Minecraft-style
	//
	// Couches de haut en bas :
	//   height     : herbe (ou terre si pente >= 4, ou pierre si pente >= 7)
	//   height - 1 : herbe/terre transition (ou terre si pente élevée)
	//   height - 2..depth : terre (dirtColor)
	//   dessous   : pierre (stoneColor, claire si haute altitude)
	//   y=0       : bedrock (pierre)
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

				// Calcul de la pente (gradient local)
				int eastH = getHeight(worldX + 1, worldZ);
				int westH = getHeight(worldX - 1, worldZ);
				int northH = getHeight(worldX, worldZ + 1);
				int southH = getHeight(worldX, worldZ - 1);
				int slope = std::abs(eastH - westH) + std::abs(northH - southH);

				// Classification surface :
				// slope >= 7 ou altitude >= 52 → rocher pur
				// slope >= 4 → terre visible (flancs raides)
				// slope 4-6 avec 50% chance → mélange terre/herbe
				bool rockySurface = false;
				bool dirtSurface = false;
				if (slope >= 7)
					rockySurface = true;
				else if (slope >= 4)
					dirtSurface = true;
				if (height >= 52)
					rockySurface = true;

				// Profondeur terre variable par biome
				int dirtDepth = 3;
				if (biome01 > 0.65f)
					dirtDepth = 5;
				else if (biome01 > 0.45f)
					dirtDepth = 4;
				if (rockySurface)
					dirtDepth = 1;
				else if (dirtSurface)
					dirtDepth = 2;

				// Bedrock (y=0)
				chunk.blocks[x][0][z] = stoneColor(worldX, 0, worldZ);

				for (int y = 1; y <= height && y < CHUNK_SIZE_Y; y++)
				{
					if (y == height)
					{
						// ── Couche de surface ──
						if (rockySurface)
						{
							chunk.blocks[x][y][z] = stoneColor(worldX, y, worldZ);
						}
						else if (dirtSurface)
						{
							// Pentes moyennes : 50% terre / 50% transition herbe-terre
							if (posRand8(worldX, y, worldZ) < 4)
								chunk.blocks[x][y][z] = dirtColor(worldX, y, worldZ);
							else
								chunk.blocks[x][y][z] = transitionColor(worldX, y, worldZ, biome01);
						}
						else
						{
							chunk.blocks[x][y][z] = grassColor(worldX, y, worldZ, biome01);
						}
					}
					else if (y == height - 1 && !rockySurface)
					{
						// ── Couche de transition (juste sous la surface) ──
						// Herbe-terre ou terre pure selon pente
						if (dirtSurface)
							chunk.blocks[x][y][z] = dirtColor(worldX, y, worldZ);
						else
							chunk.blocks[x][y][z] = transitionColor(worldX, y, worldZ, biome01);
					}
					else if (y > height - dirtDepth)
					{
						// ── Couches de terre ──
						chunk.blocks[x][y][z] = dirtColor(worldX, y, worldZ);
					}
					else
					{
						// ── Pierre ──
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
