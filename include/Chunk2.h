#ifndef CHUNK2_H
#define CHUNK2_H

#include <glad/glad.h>
#include <cstdint>
#include <vector>
#include <cstring>
#include <print>
// Chunks Params

constexpr uint8_t CHUNK_SIZE_X = 16;  // Largeur (0-15)
constexpr uint8_t CHUNK_SIZE_Y = 64;  // Hauteur (0-63, 0 = bedrock)
constexpr uint8_t CHUNK_SIZE_Z = 16;  // Profondeur (0-15)
constexpr uint8_t BEDROCK_LAYER = 0;



// Mémoire par chunk :
//   uint32_t blocks[16][64][16] = 65 536 octets = 64 KB
//   → Rentre dans le cache L2 (512 KB+), sort du L1 (32 KB)
//
// Chaque bloc stocke sa couleur RGB directe (0 = air)
// Format : 0x00BBGGRR (R bits 0-7, G bits 8-15, B bits 16-23)

class Chunk2
{
public:
	int chunkX;
	int chunkZ;

	// Chaque bloc = couleur RGB complète (0 = air)
	// 16 × 64 × 16 × 4 = 65 536 octets = 64 KB
	uint32_t blocks[CHUNK_SIZE_X][CHUNK_SIZE_Y][CHUNK_SIZE_Z];

	// Mesh Opengl
	GLuint ssbo = 0;
	GLuint vao = 0;
	uint32_t faceCount = 0;

	// Etats
	bool needsMeshRebuild = true;
	bool isEmpty = true;
	Chunk2(int cx = 0, int cz = 0) : chunkX(cx), chunkZ(cz)
	{
		memset(blocks, 0, sizeof(blocks));
	};
	// ════════════════════════════════════════════════════════════════════
	// Accesseurs
	// ════════════════════════════════════════════════════════════════════

	uint32_t getBlock(int x, int y, int z) const
	{
		if (x < 0 || x >= CHUNK_SIZE_X ||
			y < 0 || y >= CHUNK_SIZE_Y ||
			z < 0 || z >= CHUNK_SIZE_Z)
		{
			return 0;
		}
		return blocks[x][y][z];
	}

	bool setBlock(int x, int y, int z, uint32_t color)
	{
		if (x < 0 || x >= CHUNK_SIZE_X ||
			y < 0 || y >= CHUNK_SIZE_Y ||
			z < 0 || z >= CHUNK_SIZE_Z)
		{
			return false;
		}
		if (y == BEDROCK_LAYER && color == 0)
		{
			return false;
		}
		blocks[x][y][z] = color;
		needsMeshRebuild = true;
		isEmpty = false;
		return true;
	}

	// ════════════════════════════════════════════════════════════════════
	// Helpers couleur RGB
	// ════════════════════════════════════════════════════════════════════

	static int colorR(uint32_t c) { return c & 0xFF; }
	static int colorG(uint32_t c) { return (c >> 8) & 0xFF; }
	static int colorB(uint32_t c) { return (c >> 16) & 0xFF; }
	static uint32_t makeColor(int r, int g, int b)
	{
		if (r < 0) r = 0; if (r > 255) r = 255;
		if (g < 0) g = 0; if (g > 255) g = 255;
		if (b < 0) b = 0; if (b > 255) b = 255;
		return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16);
	}



	// north = +Z, south = -Z, east = +X, west = -X
	// ════════════════════════════════════════════════════════════════════
	// Ambient Occlusion — calcule l'AO pour un vertex d'une face
	//
	// Pour chaque vertex, on regarde 3 blocs voisins :
	//   side1, side2 (adjacents sur le plan de la face)
	//   corner (diagonal)
	//
	//        side1  corner
	//        ┌───┐┌───┐
	//        │ ? ││ ? │   Si side1 ET side2 solides → AO = 0 (max ombre)
	//        └───┘└───┘   Sinon AO = 3 - (side1 + side2 + corner)
	//   ┌───┐
	//   │ V │ ← vertex       AO = 3 : pas d'ombre (lumineux)
	//   └───┘                 AO = 0 : ombre maximale (coin sombre)
	//        ┌───┐
	//        │ ? │ side2
	//        └───┘
	//
	// Table de 72 offsets (6 faces × 4 vertices × 3 voisins)
	// Chaque entrée : {dx, dy, dz} relatif au bloc
	// ════════════════════════════════════════════════════════════════════

	// AO_OFFSETS[faceDir][vertexIdx][neighborIdx] = {dx, dy, dz}
	// neighborIdx: 0 = side1, 1 = side2, 2 = corner
	static constexpr int AO_OFFSETS[6][4][3][3] = {
		// Face 0: TOP (+Y) — check layer y+1
		// v0(0,1,0) v1(1,1,0) v2(1,1,1) v3(0,1,1)
		{{{-1, 1, 0}, {0, 1, -1}, {-1, 1, -1}}, // v0
		 {{1, 1, 0}, {0, 1, -1}, {1, 1, -1}},	// v1
		 {{1, 1, 0}, {0, 1, 1}, {1, 1, 1}},		// v2
		 {{-1, 1, 0}, {0, 1, 1}, {-1, 1, 1}}},	// v3

		// Face 1: BOTTOM (-Y) — check layer y-1
		// v0(0,0,1) v1(1,0,1) v2(1,0,0) v3(0,0,0)
		{{{-1, -1, 0}, {0, -1, 1}, {-1, -1, 1}},
		 {{1, -1, 0}, {0, -1, 1}, {1, -1, 1}},
		 {{1, -1, 0}, {0, -1, -1}, {1, -1, -1}},
		 {{-1, -1, 0}, {0, -1, -1}, {-1, -1, -1}}},

		// Face 2: NORTH (+Z) — check layer z+1
		// v0(0,0,1) v1(0,1,1) v2(1,1,1) v3(1,0,1)
		{{{-1, 0, 1}, {0, -1, 1}, {-1, -1, 1}},
		 {{-1, 0, 1}, {0, 1, 1}, {-1, 1, 1}},
		 {{1, 0, 1}, {0, 1, 1}, {1, 1, 1}},
		 {{1, 0, 1}, {0, -1, 1}, {1, -1, 1}}},

		// Face 3: SOUTH (-Z) — check layer z-1
		// v0(1,0,0) v1(1,1,0) v2(0,1,0) v3(0,0,0)
		{{{1, 0, -1}, {0, -1, -1}, {1, -1, -1}},
		 {{1, 0, -1}, {0, 1, -1}, {1, 1, -1}},
		 {{-1, 0, -1}, {0, 1, -1}, {-1, 1, -1}},
		 {{-1, 0, -1}, {0, -1, -1}, {-1, -1, -1}}},

		// Face 4: EAST (+X) — check layer x+1
		// v0(1,0,1) v1(1,1,1) v2(1,1,0) v3(1,0,0)
		{{{1, 0, 1}, {1, -1, 0}, {1, -1, 1}},
		 {{1, 0, 1}, {1, 1, 0}, {1, 1, 1}},
		 {{1, 0, -1}, {1, 1, 0}, {1, 1, -1}},
		 {{1, 0, -1}, {1, -1, 0}, {1, -1, -1}}},

		// Face 5: WEST (-X) — check layer x-1
		// v0(0,0,0) v1(0,1,0) v2(0,1,1) v3(0,0,1)
		{{{-1, 0, -1}, {-1, -1, 0}, {-1, -1, -1}},
		 {{-1, 0, -1}, {-1, 1, 0}, {-1, 1, -1}},
		 {{-1, 0, 1}, {-1, 1, 0}, {-1, 1, 1}},
		 {{-1, 0, 1}, {-1, -1, 0}, {-1, -1, 1}}}};

	int computeVertexAO(int bx, int by, int bz, int faceDir, int vertIdx,
						Chunk2 *n, Chunk2 *s, Chunk2 *e, Chunk2 *w,
						Chunk2 *ne, Chunk2 *nw, Chunk2 *se, Chunk2 *sw) const
	{
		auto &off = AO_OFFSETS[faceDir][vertIdx];
		bool s1 = getBlockOrNeighbor(bx + off[0][0], by + off[0][1], bz + off[0][2],
									 n, s, e, w, ne, nw, se, sw) != 0;
		bool s2 = getBlockOrNeighbor(bx + off[1][0], by + off[1][1], bz + off[1][2],
									 n, s, e, w, ne, nw, se, sw) != 0;
		bool corner = getBlockOrNeighbor(bx + off[2][0], by + off[2][1], bz + off[2][2],
										 n, s, e, w, ne, nw, se, sw) != 0;
		if (s1 && s2)
			return 0;
		return 3 - (s1 + s2 + corner);
	}

	// ════════════════════════════════════════════════════════════════════
	// Sunblock — retourne un float (0.29 à 1.0) comme BetterSpades
	//
	// Rayon diagonal (y+1, z-1) vérifie 9 blocs max.
	// La valeur est stockée sur 6 bits (0-63) dans word0.
	// ════════════════════════════════════════════════════════════════════
	float computeSunblock(int bx, int by, int bz,
						Chunk2 *n, Chunk2 *s, Chunk2 *e, Chunk2 *w,
						Chunk2 *ne, Chunk2 *nw, Chunk2 *se, Chunk2 *sw) const
	{
		int dec = 18;
		int i = 127;
		int cy = by;
		int cz = bz;

		while (dec > 0 && cy < CHUNK_SIZE_Y - 1)
		{
			cy++;
			cz--;
			if (cy >= CHUNK_SIZE_Y)
				break;
			if (getBlockOrNeighbor(bx, cy, cz, n, s, e, w, ne, nw, se, sw) != 0)
			{
				i -= dec;
			}
			dec -= 2;
		}

		// Clamp minimum à 37 (= 37/127 ≈ 0.29, comme BetterSpades)
		if (i < 37) i = 37;
		return (float)i / 127.0f;
	}

	// north = +Z, south = -Z, east = +X, west = -X
	void meshGenerate(Chunk2 *north = nullptr, Chunk2 *south = nullptr,
					  Chunk2 *east = nullptr, Chunk2 *west = nullptr,
					  Chunk2 *ne = nullptr, Chunk2 *nw = nullptr,
					  Chunk2 *se = nullptr, Chunk2 *sw = nullptr)
	{
		std::vector<uint32_t> packedFaces;
		packedFaces.reserve(8192); // 2 uints par face
		/*
		BIT LAYOUT uint32 — 2 words par face (8 bytes/face) :

		Word 0:
		  Bits [0-3]   : X local (0-15)       → 4 bits
		  Bits [4-9]   : Y local (0-63)       → 6 bits
		  Bits [10-13] : Z local (0-15)       → 4 bits
		  Bits [14-16] : Face Direction (0-5) → 3 bits
		  Bits [17]    : Shade (sunblock)     → 1 bit
		  Bits [18-23] : unused               → 6 bits
		  Bits [24-25] : AO vertex 0          → 2 bits
		  Bits [26-27] : AO vertex 1          → 2 bits
		  Bits [28-29] : AO vertex 2          → 2 bits
		  Bits [30-31] : AO vertex 3          → 2 bits

		Word 1:
		  Bits [0-7]   : R                    → 8 bits
		  Bits [8-15]  : G                    → 8 bits
		  Bits [16-23] : B                    → 8 bits
		  Bits [24-31] : unused               → 8 bits
		*/

		auto packFace = [&](int x, int y, int z, int faceDir, uint32_t blockColor)
		{
			int ao0 = computeVertexAO(x, y, z, faceDir, 0, north, south, east, west, ne, nw, se, sw);
			int ao1 = computeVertexAO(x, y, z, faceDir, 1, north, south, east, west, ne, nw, se, sw);
			int ao2 = computeVertexAO(x, y, z, faceDir, 2, north, south, east, west, ne, nw, se, sw);
			int ao3 = computeVertexAO(x, y, z, faceDir, 3, north, south, east, west, ne, nw, se, sw);
			float sunF = computeSunblock(x, y, z, north, south, east, west, ne, nw, se, sw);

			// Quantize sunblock float (0.29-1.0) → 6 bits (0-63)
			int sunQ = (int)(sunF * 63.0f);
			if (sunQ > 63) sunQ = 63;
			if (sunQ < 0) sunQ = 0;

			// Word 0 : position + face + shade(1-bit legacy) + sun(6-bit) + AO
			uint32_t word0 = (uint32_t)x
				| ((uint32_t)y << 4)
				| ((uint32_t)z << 10)
				| ((uint32_t)faceDir << 14)
				| ((uint32_t)(sunF < 0.79f ? 0 : 1) << 17)  // legacy 1-bit
				| ((uint32_t)sunQ << 18)                     // 6-bit gradient
				| ((uint32_t)ao0 << 24)
				| ((uint32_t)ao1 << 26)
				| ((uint32_t)ao2 << 28)
				| ((uint32_t)ao3 << 30);

			// Word 1 : couleur RGB directe
			uint32_t word1 = blockColor;

			packedFaces.push_back(word0);
			packedFaces.push_back(word1);
		};
		// Cache line , chache hit dans le l1
		for (int x = 0; x < CHUNK_SIZE_X; x++)
		{
			for (int y = 0; y < CHUNK_SIZE_Y; y++)
			{
				for (int z = 0; z < CHUNK_SIZE_Z; z++)
				{
					uint32_t block = blocks[x][y][z];
					if (block == 0)
						continue;

					if (getBlock(x, y + 1, z) == 0)
						packFace(x, y, z, 0, block);
					// Skip BOTTOM face au bedrock (y==0) : jamais visible par le joueur
					if (y > 0 && getBlock(x, y - 1, z) == 0)
						packFace(x, y, z, 1, block);
					if (getBlockOrNeighbor(x, y, z + 1, north, south, east, west, ne, nw, se, sw) == 0)
						packFace(x, y, z, 2, block);
					if (getBlockOrNeighbor(x, y, z - 1, north, south, east, west, ne, nw, se, sw) == 0)
						packFace(x, y, z, 3, block);
					if (getBlockOrNeighbor(x + 1, y, z, north, south, east, west, ne, nw, se, sw) == 0)
						packFace(x, y, z, 4, block);
					if (getBlockOrNeighbor(x - 1, y, z, north, south, east, west, ne, nw, se, sw) == 0)
						packFace(x, y, z, 5, block);
				}
			}
		}
		/*
		push_back #1 : alloue [] -> 1 slot
		push_back #2 : Trop petit, alloue [][] copie l'ancien -> 2slot
		push_back #3 : Trop petit, alloue [][][] copie l'ancien -> 3slot

		avec .reserve()
		juste écrrie dans le buffer

		Chaque réallocation :
			malloc() -> memcpy() -> free()

		*/

		packedFaces.reserve(4096);
		// printf("packedFaces.size() : %d\n", packedFaces.size());
		uploadMesh(packedFaces);
	};

	void render() const
	{
		if (faceCount == 0)
			return;

		// Bind le ssbo au binding point 0
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo);
		// on bind vao pour éviter crash mais sert a rien
		glBindVertexArray(vao);
		// 1faces = 6 sommets (2triangles)
		glDrawArrays(GL_TRIANGLES, 0, faceCount * 6);
	};

	void cleanup()
	{
		if (ssbo)
			glDeleteBuffers(1, &ssbo);
		if (vao)
			glDeleteVertexArrays(1, &vao);
		ssbo = vao = 0;
	};

	// ============================================================================
	// STATS & PROFILING
	// ============================================================================

	struct ChunkStats
	{
		int cx, cz;
		uint32_t totalBlocks;
		uint32_t solidBlocks;
		uint32_t airBlocks;
		uint32_t faces;
		size_t ramBytes;
		size_t vramBytes;
		float fillPercent;
	};

	ChunkStats getStats() const
	{
		ChunkStats s{};
		s.cx = chunkX;
		s.cz = chunkZ;
		s.totalBlocks = CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z;
		s.solidBlocks = 0;

		for (int x = 0; x < CHUNK_SIZE_X; x++)
			for (int y = 0; y < CHUNK_SIZE_Y; y++)
				for (int z = 0; z < CHUNK_SIZE_Z; z++)
					if (blocks[x][y][z] != 0)
						s.solidBlocks++;

		s.airBlocks = s.totalBlocks - s.solidBlocks;
		s.faces = faceCount;
		s.fillPercent = (s.totalBlocks > 0)
							? (float)s.solidBlocks / s.totalBlocks * 100.0f
							: 0.0f;

		// RAM : le tableau blocks[] + champs struct
		s.ramBytes = sizeof(blocks) + sizeof(chunkX) + sizeof(chunkZ) + sizeof(ssbo) + sizeof(vao) + sizeof(faceCount) + sizeof(needsMeshRebuild) + sizeof(isEmpty);

		// VRAM : le SSBO sur le GPU
		s.vramBytes = faceCount * sizeof(uint32_t);

		return s;
	}

	~Chunk2()
	{
		cleanup();
	};

private:
	// ──────────────────────────────────────────────────────────────────
	// Vérifie un bloc aux coordonnées (x, y, z) locales.
	// Si hors limites en X ou Z, regarde dans le chunk voisin.
	// Si pas de voisin → retourne 0 (air) = face visible au bord du monde.
	//
	//  Chunk West    This Chunk     Chunk East
	//  ┌────────┐   ┌────────────┐   ┌────────┐
	//  │        │   │ x=0 ... 15 │   │        │
	//  │  [15]  │←──│  getBlock  │──→│  [0]   │
	//  │        │   │  (-1,y,z)  │   │(16,y,z)│
	//  └────────┘   └────────────┘   └────────┘
	// ──────────────────────────────────────────────────────────────────
	uint32_t getBlockOrNeighbor(int x, int y, int z,
							   Chunk2 *north, Chunk2 *south,
							   Chunk2 *east, Chunk2 *west,
							   Chunk2 *ne, Chunk2 *nw,
							   Chunk2 *se, Chunk2 *sw) const
	{
		if (y < 0 || y >= CHUNK_SIZE_Y)
			return 0;

		// Diagonal : X ET Z hors limites
		if (x >= CHUNK_SIZE_X && z >= CHUNK_SIZE_Z)
			return ne ? ne->blocks[x - CHUNK_SIZE_X][y][z - CHUNK_SIZE_Z] : 0;
		if (x >= CHUNK_SIZE_X && z < 0)
			return se ? se->blocks[x - CHUNK_SIZE_X][y][CHUNK_SIZE_Z + z] : 0;
		if (x < 0 && z >= CHUNK_SIZE_Z)
			return nw ? nw->blocks[CHUNK_SIZE_X + x][y][z - CHUNK_SIZE_Z] : 0;
		if (x < 0 && z < 0)
			return sw ? sw->blocks[CHUNK_SIZE_X + x][y][CHUNK_SIZE_Z + z] : 0;

		// Cardinal
		if (x >= CHUNK_SIZE_X)
			return east ? east->blocks[x - CHUNK_SIZE_X][y][z] : 0;
		if (x < 0)
			return west ? west->blocks[CHUNK_SIZE_X + x][y][z] : 0;
		if (z >= CHUNK_SIZE_Z)
			return north ? north->blocks[x][y][z - CHUNK_SIZE_Z] : 0;
		if (z < 0)
			return south ? south->blocks[x][y][CHUNK_SIZE_Z + z] : 0;

		return blocks[x][y][z];
	}

	void uploadMesh(const std::vector<uint32_t> &faces)
	{
		if (ssbo)
			glDeleteBuffers(1, &ssbo);

		if (faces.empty())
		{
			faceCount = 0;
			needsMeshRebuild = false;
			return;
		};

		glGenBuffers(1, &ssbo);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
		glBufferData(GL_SHADER_STORAGE_BUFFER,
					 faces.size() * sizeof(uint32_t),
					 faces.data(), GL_STATIC_DRAW);

		if (!vao)
			glGenVertexArrays(1, &vao);

		faceCount = faces.size() / 2; // 2 uints par face
		needsMeshRebuild = false;
	};
};

#endif // CHUNK2_H