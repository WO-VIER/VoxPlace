#ifndef CHUNK2_H
#define CHUNK2_H

#include <glad/glad.h>
#include <cstdint>
#include <vector>
#include <cstring>

// Chunks Params

constexpr uint8_t CHUNK_SIZE_X = 16;  // (0 - 255) all time
constexpr uint8_t CHUNK_SIZE_Y = 255; // Hauteur
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

		for (uint8_t x = 0; x < CHUNK_SIZE_X; x++)
		{
			for (uint8_t z = 0; z < CHUNK_SIZE_Z; z++)
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
	void meshGenerate(Chunk2* north = nullptr, Chunk2* south = nullptr,
	                  Chunk2* east = nullptr, Chunk2* west = nullptr)
	{
		std::vector<uint32_t> packedFaces;
		/*
		Bits [0-3] ; X local (0 - 15) -> 4 bits
		Bits [4-11] ; y local (0 - 256) -> 8 bits
		Bits [12-15] ; z local (0 - 15) -> 4 bits
		Bits [16-18] ; Face Direction   -> 3 bits
		(0=top, 1=bottom, 2=north, 3=south, 4=east, 5=west)

		Bits [19-25] : Color Index (0 - 64) -> 6 bits mais on utilise 5bits
		Bits [26-28] : AO apr faces 		-> 2bits (opt, 0-3)

		Total : 28 / 32 bits du uint_32

		//uint32_t packed = (x) | (y << 4) | (z << 12) | (faceDir << 16) | (color << 19);
		*/
		for (uint8_t x = 0; x < CHUNK_SIZE_X; x++)
		{
			for (uint16_t y = 0; y < CHUNK_SIZE_Y; y++)
			{
				for (uint8_t z = 0; z < CHUNK_SIZE_Z; z++)
				{
					uint8_t block = blocks[x][y][z];
					if (block == 0)
						continue;

					// Face 0 : TOP (+Y)
					if (getBlock(x, y + 1, z) == 0)
					{
						uint32_t packed = x | (y << 4) | (z << 12) | (0 << 16) | (block << 19);
						packedFaces.push_back(packed);
					}
					// Face 1 : BOTTOM (-Y)
					if (getBlock(x, y - 1, z) == 0)
					{
						uint32_t packed = x | (y << 4) | (z << 12) | (1 << 16) | (block << 19);
						packedFaces.push_back(packed);
					}
					// Face 2 : NORTH (+Z) — vérifie le chunk voisin si z == 15
					if (getBlockOrNeighbor(x, y, z + 1, north, south, east, west) == 0)
					{
						uint32_t packed = x | (y << 4) | (z << 12) | (2 << 16) | (block << 19);
						packedFaces.push_back(packed);
					}
					// Face 3 : SOUTH (-Z) — vérifie le chunk voisin si z == 0
					if (getBlockOrNeighbor(x, y, z - 1, north, south, east, west) == 0)
					{
						uint32_t packed = x | (y << 4) | (z << 12) | (3 << 16) | (block << 19);
						packedFaces.push_back(packed);
					}
					// Face 4 : EAST (+X) — vérifie le chunk voisin si x == 15
					if (getBlockOrNeighbor(x + 1, y, z, north, south, east, west) == 0)
					{
						uint32_t packed = x | (y << 4) | (z << 12) | (4 << 16) | (block << 19);
						packedFaces.push_back(packed);
					}
					// Face 5 : WEST (-X) — vérifie le chunk voisin si x == 0
					if (getBlockOrNeighbor(x - 1, y, z, north, south, east, west) == 0)
					{
						uint32_t packed = x | (y << 4) | (z << 12) | (5 << 16) | (block << 19);
						packedFaces.push_back(packed);
					}
				};
			};
		};
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

		for (uint8_t x = 0; x < CHUNK_SIZE_X; x++)
			for (uint16_t y = 0; y < CHUNK_SIZE_Y; y++)
				for (uint8_t z = 0; z < CHUNK_SIZE_Z; z++)
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
	//  │  [15]  │←──│  getBlock   │──→│  [0]   │
	//  │        │   │  (-1,y,z)   │   │(16,y,z)│
	//  └────────┘   └────────────┘   └────────┘
	// ──────────────────────────────────────────────────────────────────
	uint8_t getBlockOrNeighbor(int x, int y, int z,
		Chunk2* north, Chunk2* south,
		Chunk2* east, Chunk2* west) const
	{
		// Y hors limites → air (dessus/dessous du monde)
		if (y < 0 || y >= CHUNK_SIZE_Y) return 0;

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