#ifndef TERRAIN_GENERATOR_H
#define TERRAIN_GENERATOR_H

#include <FastNoiseLite.h>
#include <Chunk2.h>
#include <algorithm>

// ============================================================================
// TerrainGenerator — Génère un terrain naturel avec Simplex Noise
//
// Utilise 2 couches de bruit combinées :
//
//   Hauteur(x,z) = baseHeight
//                + continent(x,z) × continentAmp   ← collines douces
//                + detail(x,z)    × detailAmp       ← micro-variations
//
//     continent seul           + detail              = résultat
//    ╭───────────────╮     ╭─~─~─~─~─~─~╮     ╭──~──~──~─~──╮
//    │   ╱╲          │     │ ╱╲╱╲╱╲╱╲╱╲ │     │  ╱╲╱╲       │
//    │  ╱  ╲   ╱╲   │  +  │            │  =  │ ╱    ╲╱╲╱╲  │
//    │ ╱    ╲ ╱  ╲  │     │            │     │╱          ╲ │
//    ╰───────────────╯     ╰────────────╯     ╰─────────────╯
//    freq = 0.005            freq = 0.02          naturel !
//
// Le bruit est évalué en coordonnées MONDE (pas locales au chunk),
// donc les transitions entre chunks sont automatiquement continues.
// ============================================================================

class TerrainGenerator
{
public:
	// Paramètres ajustables (adapté pour Y=64)
	int baseHeight = 20;         // Hauteur de base du sol
	float continentAmp = 10.0f;  // Amplitude des grandes collines (±10 blocs)
	float detailAmp = 3.0f;      // Amplitude des micro-variations (±3 blocs)

	TerrainGenerator(int seed = 42)
	{
		// Couche 1 : continent — grandes collines douces
		continent.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
		continent.SetSeed(seed);
		continent.SetFrequency(0.005f); // Basse fréquence = formes larges

		// Couche 2 : détail — petites bosses sur les collines
		detail.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
		detail.SetSeed(seed + 1); // Seed différente pour éviter la corrélation
		detail.SetFrequency(0.02f); // Haute fréquence = détails fins
	}

	// ════════════════════════════════════════════════════════════════════
	// Calcule la hauteur du terrain pour une position monde (wx, wz)
	// Retourne une valeur entre 1 et 63 (CHUNK_SIZE_Y - 1)
	// ════════════════════════════════════════════════════════════════════
	int getHeight(int worldX, int worldZ) const
	{
		float wx = static_cast<float>(worldX);
		float wz = static_cast<float>(worldZ);

		float h = static_cast<float>(baseHeight);
		h += continent.GetNoise(wx, wz) * continentAmp;  // [-1,1] × 10
		h += detail.GetNoise(wx, wz) * detailAmp;        // [-1,1] × 3

		return std::clamp(static_cast<int>(h), 1, static_cast<int>(CHUNK_SIZE_Y) - 1);
	}

	// ════════════════════════════════════════════════════════════════════
	// Remplit un chunk avec le terrain généré
	//
	// Pour chaque colonne (x, z) :
	//   y = 0           → Bedrock (incassable)
	//   y = 1..height-3 → Pierre (palette 45-47)
	//   y = height-2..height-1 → Terre (palette 33-41 basé sur profondeur Y)
	//   y = height      → Herbe (palette 42-44)
	//
	// Les indices 33-64 sont les nuances terrain 
	// ════════════════════════════════════════════════════════════════════
	void fillChunk(Chunk2& chunk) const
	{
		for (int x = 0; x < CHUNK_SIZE_X; x++)
		{
			for (int z = 0; z < CHUNK_SIZE_Z; z++)
			{
				// Position monde = position chunk × taille + position locale
				int worldX = chunk.chunkX * CHUNK_SIZE_X + x;
				int worldZ = chunk.chunkZ * CHUNK_SIZE_Z + z;
				int height = getHeight(worldX, worldZ);

				// Bedrock (y = 0) — déjà dans le constructeur, on force
				chunk.blocks[x][0][z] = 28; // Noir (bedrock)

				// Remplir de y=1 jusqu'à height
				for (int y = 1; y <= height && y < CHUNK_SIZE_Y; y++)
				{
					if (y == height)
					{
						// Surface : herbe — nuance basée sur la hauteur
						// Plus haut = plus clair (indice 42), plus bas = plus sombre (44)
						int herbeIdx = 42;
						if (height < 15)
							herbeIdx = 44; // Herbe sombre en bas
						else if (height < 22)
							herbeIdx = 43; // Herbe moyenne
						chunk.blocks[x][y][z] = herbeIdx;
					}
					else if (y > height - 3)
					{
						// Terre : gradient basé sur la profondeur Y
						// dirt_color_table : indices 33 (surface) à 41 (fond)
						int slice = (CHUNK_SIZE_Y - 1 - y) / 8;
						int dirtIdx = 33 + std::clamp(slice, 0, 8);
						chunk.blocks[x][y][z] = dirtIdx;
					}
					else
					{
						// Pierre : 3 nuances
						int pierreIdx = 45 + (y % 3); // 45, 46, 47 en alternance
						chunk.blocks[x][y][z] = pierreIdx;
					}
				}
			}
		}
		chunk.needsMeshRebuild = true;
		chunk.isEmpty = false;
	}

	void fillChunkBench(Chunk2& chunk) const
{
    // On s'assure que la couche Y=0 est bien de la bedrock
	/*    
	for (int x = 0; x < CHUNK_SIZE_X; x++) 
    {
        for (int z = 0; z < CHUNK_SIZE_Z; z++)
        {
            chunk.blocks[x][0][z] = 28; // Bedrock
        }
    }
	*/
    // Remplissage du reste (1 à 255) avec le motif damier 3D
    for (int y = 0; y < CHUNK_SIZE_Y; y++) 
    {
        for (int x = 0; x < CHUNK_SIZE_X; x++) 
        {
            for (int z = 0; z < CHUNK_SIZE_Z; z++)
            {
                // La magie du damier : on additionne les coordonnées.
                // Si c'est pair, on met un bloc. Si c'est impair, on met de l'air.
                if ((x + y + z) % 2 == 0)
                {
                    chunk.blocks[x][y][z] = 1; // Bloc (ex: herbe)
                }
                else
                {
                    chunk.blocks[x][y][z] = 0; // Air
                }
            }
        }
    }

    chunk.needsMeshRebuild = true;
    chunk.isEmpty = false;
}

private:
	FastNoiseLite continent; // Basse fréquence — grandes collines
	FastNoiseLite detail;    // Haute fréquence — détails
};

#endif // TERRAIN_GENERATOR_H
