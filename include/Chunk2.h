#ifndef CHUNK2_H
#define CHUNK2_H

#include <glad/glad.h>
#include <cstdint>
#include <vector>
#include <cstring>
#include <print>
// Chunks Params

constexpr uint8_t CHUNK_SIZE_X = 16;  // (0 - 255) all time
constexpr uint16_t CHUNK_SIZE_Y = 256; // Hauteur (0-255, 0 = bedrock)
constexpr uint8_t CHUNK_SIZE_Z = 16;
constexpr uint8_t BEDROCK_LAYER = 0;

// 16 * 16 * 64 = 16384 octets blocs par chunk
// 1 bloc = une couleur qui peut etre 16 ou 32 couleurs 1 (byte)

//  Chaques faces de chaque blocs sont composées de 4 points
// Liste de points : 4 * (3 float) = 12 floats par faces = 12 * 4 = 48 octets par faces
// Liste de triangles : 2 (triangles) * 3 int (indicies) = 6 int * 4 = 24 octets par faces
//  points * positions * tailles en octet
//  triangles * indices * tailles en octet
// 72 octets par faces dans ram et vram pour chaques faces + texture par point 4 * 2 float = 104 octets par faces

// 4

class Chunk2
{
public:
	// Position du chunk dans le monde
	int chunkX;
	int chunkZ;

	// Données des blocs 1-15 index de couleur 0 = air
	uint8_t blocks[CHUNK_SIZE_X][CHUNK_SIZE_Y][CHUNK_SIZE_Z];

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

		for (int x = 0; x < CHUNK_SIZE_X; x++)
		{
			for (int z = 0; z < CHUNK_SIZE_Z; z++)
			{
				blocks[x][BEDROCK_LAYER][z] = 29; /// couleur
			};
		};
	};
	// Acesseurs

	// Obtenir un bloc (verif des limits)
	uint8_t getBlock(int x, int y, int z) const
	{
		if (x < 0 || x >= CHUNK_SIZE_X ||
			y < 0 || y >= CHUNK_SIZE_Y ||
			z < 0 || z >= CHUNK_SIZE_Z)
		{
			return 0;
		}
		return blocks[x][y][z];
	};

	bool setBlock(int x, int y, int z, uint8_t blockType)
	{
		// Vérifier les limites
		if (x < 0 || x >= CHUNK_SIZE_X ||
			y < 0 || y >= CHUNK_SIZE_Y ||
			z < 0 || z >= CHUNK_SIZE_Z)
		{
			return false;
		}

		// Empêcher de casser la bedrock
		if (y == BEDROCK_LAYER && blockType == 0)
		{
			return false;
		}

		blocks[x][y][z] = blockType;
		needsMeshRebuild = true;
		isEmpty = false;
		return true;
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
		{{{-1,1,0}, {0,1,-1}, {-1,1,-1}},   // v0
		 {{ 1,1,0}, {0,1,-1}, { 1,1,-1}},   // v1
		 {{ 1,1,0}, {0,1, 1}, { 1,1, 1}},   // v2
		 {{-1,1,0}, {0,1, 1}, {-1,1, 1}}},  // v3

		// Face 1: BOTTOM (-Y) — check layer y-1
		// v0(0,0,1) v1(1,0,1) v2(1,0,0) v3(0,0,0)
		{{{-1,-1,0}, {0,-1, 1}, {-1,-1, 1}},
		 {{ 1,-1,0}, {0,-1, 1}, { 1,-1, 1}},
		 {{ 1,-1,0}, {0,-1,-1}, { 1,-1,-1}},
		 {{-1,-1,0}, {0,-1,-1}, {-1,-1,-1}}},

		// Face 2: NORTH (+Z) — check layer z+1
		// v0(0,0,1) v1(0,1,1) v2(1,1,1) v3(1,0,1)
		{{{-1,0,1}, {0,-1,1}, {-1,-1,1}},
		 {{-1,0,1}, {0, 1,1}, {-1, 1,1}},
		 {{ 1,0,1}, {0, 1,1}, { 1, 1,1}},
		 {{ 1,0,1}, {0,-1,1}, { 1,-1,1}}},

		// Face 3: SOUTH (-Z) — check layer z-1
		// v0(1,0,0) v1(1,1,0) v2(0,1,0) v3(0,0,0)
		{{{ 1,0,-1}, {0,-1,-1}, { 1,-1,-1}},
		 {{ 1,0,-1}, {0, 1,-1}, { 1, 1,-1}},
		 {{-1,0,-1}, {0, 1,-1}, {-1, 1,-1}},
		 {{-1,0,-1}, {0,-1,-1}, {-1,-1,-1}}},

		// Face 4: EAST (+X) — check layer x+1
		// v0(1,0,1) v1(1,1,1) v2(1,1,0) v3(1,0,0)
		{{{1,0, 1}, {1,-1,0}, {1,-1, 1}},
		 {{1,0, 1}, {1, 1,0}, {1, 1, 1}},
		 {{1,0,-1}, {1, 1,0}, {1, 1,-1}},
		 {{1,0,-1}, {1,-1,0}, {1,-1,-1}}},

		// Face 5: WEST (-X) — check layer x-1
		// v0(0,0,0) v1(0,1,0) v2(0,1,1) v3(0,0,1)
		{{{-1,0,-1}, {-1,-1,0}, {-1,-1,-1}},
		 {{-1,0,-1}, {-1, 1,0}, {-1, 1,-1}},
		 {{-1,0, 1}, {-1, 1,0}, {-1, 1, 1}},
		 {{-1,0, 1}, {-1,-1,0}, {-1,-1, 1}}}
	};

	int computeVertexAO(int bx, int by, int bz, int faceDir, int vertIdx,
	                    Chunk2* north, Chunk2* south, Chunk2* east, Chunk2* west) const
	{
		auto& off = AO_OFFSETS[faceDir][vertIdx];
		bool s1 = getBlockOrNeighbor(bx + off[0][0], by + off[0][1], bz + off[0][2],
		                             north, south, east, west) != 0;
		bool s2 = getBlockOrNeighbor(bx + off[1][0], by + off[1][1], bz + off[1][2],
		                             north, south, east, west) != 0;
		bool corner = getBlockOrNeighbor(bx + off[2][0], by + off[2][1], bz + off[2][2],
		                                 north, south, east, west) != 0;
		if (s1 && s2) return 0; // Coin entièrement occluded
		return 3 - (s1 + s2 + corner);
	}

	// north = +Z, south = -Z, east = +X, west = -X
	void meshGenerate(Chunk2 *north = nullptr, Chunk2 *south = nullptr,
					  Chunk2 *east = nullptr, Chunk2 *west = nullptr)
	{
		std::vector<uint32_t> packedFaces;
		/*
		NOUVEAU BIT LAYOUT (avec AO per-vertex) :

		Bits [0-3]   : X local (0-15)      → 4 bits
		Bits [4-11]  : Y local (0-255)      → 8 bits
		Bits [12-15] : Z local (0-15)       → 4 bits
		Bits [16-18] : Face Direction (0-5) → 3 bits
		Bits [19-23] : Color (color-1, 0-31)→ 5 bits   ← réduit de 6 à 5
		Bits [24-25] : AO vertex 0          → 2 bits
		Bits [26-27] : AO vertex 1          → 2 bits
		Bits [28-29] : AO vertex 2          → 2 bits
		Bits [30-31] : AO vertex 3          → 2 bits

		Total : 32/32 bits utilisés !
		*/

		auto packFace = [&](int x, int y, int z, int faceDir, uint8_t color) {
			int ao0 = computeVertexAO(x, y, z, faceDir, 0, north, south, east, west);
			int ao1 = computeVertexAO(x, y, z, faceDir, 1, north, south, east, west);
			int ao2 = computeVertexAO(x, y, z, faceDir, 2, north, south, east, west);
			int ao3 = computeVertexAO(x, y, z, faceDir, 3, north, south, east, west);

			uint32_t packed = (uint32_t)x
				| ((uint32_t)y << 4)
				| ((uint32_t)z << 12)
				| ((uint32_t)faceDir << 16)
				| ((uint32_t)(color - 1) << 19)  // color-1 car 0=air jamais rendu
				| ((uint32_t)ao0 << 24)
				| ((uint32_t)ao1 << 26)
				| ((uint32_t)ao2 << 28)
				| ((uint32_t)ao3 << 30);

			packedFaces.push_back(packed);
		};
		// Cache line , chache hit dans le l1 
		for (int x = 0; x < CHUNK_SIZE_X; x++)
		{
			for (int y = 0; y < CHUNK_SIZE_Y; y++)
			{
				for (int z = 0; z < CHUNK_SIZE_Z; z++)
				{
					uint8_t block = blocks[x][y][z];
					if (block == 0)
						continue; // air

					// Face 0 : TOP (+Y)
					if (getBlock(x, y + 1, z) == 0)
						packFace(x, y, z, 0, block);
					// Face 1 : BOTTOM (-Y)
					if (getBlock(x, y - 1, z) == 0)
						packFace(x, y, z, 1, block);
					// Face 2 : NORTH (+Z)
					if (getBlockOrNeighbor(x, y, z + 1, north, south, east, west) == 0)
						packFace(x, y, z, 2, block);
					// Face 3 : SOUTH (-Z)
					if (getBlockOrNeighbor(x, y, z - 1, north, south, east, west) == 0)
						packFace(x, y, z, 3, block);
					// Face 4 : EAST (+X)
					if (getBlockOrNeighbor(x + 1, y, z, north, south, east, west) == 0)
						packFace(x, y, z, 4, block);
					// Face 5 : WEST (-X)
					if (getBlockOrNeighbor(x - 1, y, z, north, south, east, west) == 0)
						packFace(x, y, z, 5, block);
				};
			};
		};
		/*
		push_back #1 : alloue [] -> 1 slot
		push_back #2 : Trop petit, alloue [][] copie l'ancien -> 2slot
		push_back #3 : Trop petit, alloue [][][] copie l'ancien -> 3slot

		avec .reserve()
		juste écrrie dans le buffer

		Chaque réallocation :
			malloc() -> memcpy() -> free()

		*/
		//printf("packedFaces.size() : %d\n", packedFaces.size());
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
	uint8_t getBlockOrNeighbor(int x, int y, int z,
							   Chunk2 *north, Chunk2 *south,
							   Chunk2 *east, Chunk2 *west) const
	{
		// Y hors limites → air (dessus/dessous du monde)
		if (y < 0 || y >= CHUNK_SIZE_Y)
			return 0;

		// X hors limites → chunk east/west
		if (x >= CHUNK_SIZE_X)
			return east ? east->blocks[0][y][z] : 0;
		if (x < 0)
			return west ? west->blocks[CHUNK_SIZE_X - 1][y][z] : 0;

		// Z hors limites → chunk north/south
		if (z >= CHUNK_SIZE_Z)
			return north ? north->blocks[x][y][0] : 0;
		if (z < 0)
			return south ? south->blocks[x][y][CHUNK_SIZE_Z - 1] : 0;

		// Dans les limites → bloc local
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

		faceCount = faces.size();
		needsMeshRebuild = false;
	};
};

#endif // CHUNK2_H