#ifndef CHUNK2_H
#define CHUNK2_H

#include <VoxelChunkData.h>

#include <glad/glad.h>

#include <cstdint>
#include <cstring>
#include <vector>

class Chunk2 : public VoxelChunkData
{
public:
	struct MeshNeighborhood
	{
		const VoxelChunkData *north = nullptr;
		const VoxelChunkData *south = nullptr;
		const VoxelChunkData *east = nullptr;
		const VoxelChunkData *west = nullptr;
		const VoxelChunkData *ne = nullptr;
		const VoxelChunkData *nw = nullptr;
		const VoxelChunkData *se = nullptr;
		const VoxelChunkData *sw = nullptr;
	};

	GLuint ssbo = 0;
	GLuint vao = 0;
	uint32_t faceCount = 0;
	bool needsMeshRebuild = true;
	bool isEmpty = true;
	std::vector<uint32_t> packedFacesCpu;

	Chunk2(int cx = 0, int cz = 0) : VoxelChunkData(cx, cz)
	{
	}

	void copyFromData(const VoxelChunkData &data)
	{
		chunkX = data.chunkX;
		chunkZ = data.chunkZ;
		revision = data.revision;
		std::memcpy(blocks, data.blocks, sizeof(blocks));
		isEmpty = data.isCompletelyEmpty();
		needsMeshRebuild = true;
	}

	bool setBlock(int x, int y, int z, uint32_t color)
	{
		if (!setBlockRaw(x, y, z, color))
		{
			return false;
		}
		needsMeshRebuild = true;
		if (color != 0)
		{
			isEmpty = false;
		}
		else
		{
			isEmpty = isCompletelyEmpty();
		}
		return true;
	}

	static std::vector<uint32_t> buildPackedFaces(const VoxelChunkData &chunk,
												  const MeshNeighborhood &neighbors)
	{
		std::vector<uint32_t> packedFaces;
		packedFaces.reserve(8192);

		auto packFace = [&](int x, int y, int z, int faceDir, uint32_t blockColor)
		{
			int ao0 = computeVertexAO(chunk, neighbors, x, y, z, faceDir, 0);
			int ao1 = computeVertexAO(chunk, neighbors, x, y, z, faceDir, 1);
			int ao2 = computeVertexAO(chunk, neighbors, x, y, z, faceDir, 2);
			int ao3 = computeVertexAO(chunk, neighbors, x, y, z, faceDir, 3);
			int sunQ = computeSunblockQ127(chunk, neighbors, x, y, z);

			uint32_t word0 = static_cast<uint32_t>(x)
				| (static_cast<uint32_t>(y) << 4)
				| (static_cast<uint32_t>(z) << 10)
				| (static_cast<uint32_t>(faceDir) << 14)
				| (static_cast<uint32_t>(sunQ) << 17)
				| (static_cast<uint32_t>(ao0) << 24)
				| (static_cast<uint32_t>(ao1) << 26)
				| (static_cast<uint32_t>(ao2) << 28)
				| (static_cast<uint32_t>(ao3) << 30);

			packedFaces.push_back(word0);
			packedFaces.push_back(blockColor);
		};

		for (int x = 0; x < CHUNK_SIZE_X; x++)
		{
			for (int y = 0; y < CHUNK_SIZE_Y; y++)
			{
				for (int z = 0; z < CHUNK_SIZE_Z; z++)
				{
					uint32_t block = chunk.blocks[x][y][z];
					if (block == 0)
					{
						continue;
					}

					if (chunk.getBlock(x, y + 1, z) == 0)
					{
						packFace(x, y, z, 0, block);
					}
					if (y > 0 && chunk.getBlock(x, y - 1, z) == 0)
					{
						packFace(x, y, z, 1, block);
					}
					if (getBlockOrNeighbor(chunk, neighbors, x, y, z + 1) == 0)
					{
						packFace(x, y, z, 2, block);
					}
					if (getBlockOrNeighbor(chunk, neighbors, x, y, z - 1) == 0)
					{
						packFace(x, y, z, 3, block);
					}
					if (getBlockOrNeighbor(chunk, neighbors, x + 1, y, z) == 0)
					{
						packFace(x, y, z, 4, block);
					}
					if (getBlockOrNeighbor(chunk, neighbors, x - 1, y, z) == 0)
					{
						packFace(x, y, z, 5, block);
					}
				}
			}
		}

		return packedFaces;
	}

	void uploadBuiltMesh(const std::vector<uint32_t> &faces)
	{
		uploadMesh(faces);
	}

	static constexpr int AO_OFFSETS[6][4][3][3] = {
		{{{-1, 1, 0}, {0, 1, -1}, {-1, 1, -1}},
		 {{1, 1, 0}, {0, 1, -1}, {1, 1, -1}},
		 {{1, 1, 0}, {0, 1, 1}, {1, 1, 1}},
		 {{-1, 1, 0}, {0, 1, 1}, {-1, 1, 1}}},

		{{{-1, -1, 0}, {0, -1, 1}, {-1, -1, 1}},
		 {{1, -1, 0}, {0, -1, 1}, {1, -1, 1}},
		 {{1, -1, 0}, {0, -1, -1}, {1, -1, -1}},
		 {{-1, -1, 0}, {0, -1, -1}, {-1, -1, -1}}},

		{{{-1, 0, 1}, {0, -1, 1}, {-1, -1, 1}},
		 {{-1, 0, 1}, {0, 1, 1}, {-1, 1, 1}},
		 {{1, 0, 1}, {0, 1, 1}, {1, 1, 1}},
		 {{1, 0, 1}, {0, -1, 1}, {1, -1, 1}}},

		{{{1, 0, -1}, {0, -1, -1}, {1, -1, -1}},
		 {{1, 0, -1}, {0, 1, -1}, {1, 1, -1}},
		 {{-1, 0, -1}, {0, 1, -1}, {-1, 1, -1}},
		 {{-1, 0, -1}, {0, -1, -1}, {-1, -1, -1}}},

		{{{1, 0, 1}, {1, -1, 0}, {1, -1, 1}},
		 {{1, 0, 1}, {1, 1, 0}, {1, 1, 1}},
		 {{1, 0, -1}, {1, 1, 0}, {1, 1, -1}},
		 {{1, 0, -1}, {1, -1, 0}, {1, -1, -1}}},

		{{{-1, 0, -1}, {-1, -1, 0}, {-1, -1, -1}},
		 {{-1, 0, -1}, {-1, 1, 0}, {-1, 1, -1}},
		 {{-1, 0, 1}, {-1, 1, 0}, {-1, 1, 1}},
		 {{-1, 0, 1}, {-1, -1, 0}, {-1, -1, 1}}}};

	void meshGenerate(Chunk2 *north = nullptr, Chunk2 *south = nullptr,
					  Chunk2 *east = nullptr, Chunk2 *west = nullptr,
					  Chunk2 *ne = nullptr, Chunk2 *nw = nullptr,
					  Chunk2 *se = nullptr, Chunk2 *sw = nullptr)
	{
		MeshNeighborhood neighbors = makeNeighborhood(
			north, south, east, west, ne, nw, se, sw);
		std::vector<uint32_t> packedFaces = buildPackedFaces(*this, neighbors);
		uploadMesh(packedFaces);
	}

	void render() const
	{
		if (faceCount == 0)
		{
			return;
		}

		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo);
		glBindVertexArray(vao);
		glDrawArrays(GL_TRIANGLES, 0, faceCount * 6);
	}

	const std::vector<uint32_t> &packedFaces() const
	{
		return packedFacesCpu;
	}

	void cleanup()
	{
		if (ssbo != 0)
		{
			glDeleteBuffers(1, &ssbo);
		}
		if (vao != 0)
		{
			glDeleteVertexArrays(1, &vao);
		}
		ssbo = 0;
		vao = 0;
	}

	struct ChunkStats
	{
		int cx = 0;
		int cz = 0;
		uint32_t totalBlocks = 0;
		uint32_t solidBlocks = 0;
		uint32_t airBlocks = 0;
		uint32_t faces = 0;
		size_t ramBytes = 0;
		size_t vramBytes = 0;
		float fillPercent = 0.0f;
	};

	ChunkStats getStats() const
	{
		ChunkStats stats;
		stats.cx = chunkX;
		stats.cz = chunkZ;
		stats.totalBlocks = CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z;
		for (int x = 0; x < CHUNK_SIZE_X; x++)
		{
			for (int y = 0; y < CHUNK_SIZE_Y; y++)
			{
				for (int z = 0; z < CHUNK_SIZE_Z; z++)
				{
					if (blocks[x][y][z] != 0)
					{
						stats.solidBlocks++;
					}
				}
			}
		}
		stats.airBlocks = stats.totalBlocks - stats.solidBlocks;
		stats.faces = faceCount;
		if (stats.totalBlocks > 0)
		{
			stats.fillPercent = static_cast<float>(stats.solidBlocks) / static_cast<float>(stats.totalBlocks) * 100.0f;
		}
		stats.ramBytes = sizeof(VoxelChunkData) + sizeof(ssbo) + sizeof(vao) + sizeof(faceCount) + sizeof(needsMeshRebuild) + sizeof(isEmpty);
		stats.vramBytes = static_cast<size_t>(faceCount) * sizeof(uint32_t) * 2;
		return stats;
	}

	~Chunk2()
	{
		cleanup();
	}

private:
	static MeshNeighborhood makeNeighborhood(const Chunk2 *north,
											 const Chunk2 *south,
											 const Chunk2 *east,
											 const Chunk2 *west,
											 const Chunk2 *ne,
											 const Chunk2 *nw,
											 const Chunk2 *se,
											 const Chunk2 *sw)
	{
		MeshNeighborhood neighborhood;
		neighborhood.north = north;
		neighborhood.south = south;
		neighborhood.east = east;
		neighborhood.west = west;
		neighborhood.ne = ne;
		neighborhood.nw = nw;
		neighborhood.se = se;
		neighborhood.sw = sw;
		return neighborhood;
	}

	static int computeVertexAO(const VoxelChunkData &chunk,
							   const MeshNeighborhood &neighbors,
							   int bx,
							   int by,
							   int bz,
							   int faceDir,
							   int vertIdx)
	{
		const int (*offsets)[3] = AO_OFFSETS[faceDir][vertIdx];
		bool side1 = getBlockOrNeighbor(
						 chunk,
						 neighbors,
						 bx + offsets[0][0],
						 by + offsets[0][1],
						 bz + offsets[0][2]) != 0;
		bool side2 = getBlockOrNeighbor(
						 chunk,
						 neighbors,
						 bx + offsets[1][0],
						 by + offsets[1][1],
						 bz + offsets[1][2]) != 0;
		bool corner = getBlockOrNeighbor(
						  chunk,
						  neighbors,
						  bx + offsets[2][0],
						  by + offsets[2][1],
						  bz + offsets[2][2]) != 0;
		if (side1 && side2)
		{
			return 0;
		}
		return 3 - (side1 + side2 + corner);
	}

	static int computeSunblockQ127(const VoxelChunkData &chunk,
								   const MeshNeighborhood &neighbors,
								   int bx,
								   int by,
								   int bz)
	{
		int dec = 18;
		int value = 127;
		int cy = by;
		int cz = bz;

		while (dec > 0 && cy < CHUNK_SIZE_Y - 1)
		{
			cy++;
			cz--;
			if (cy >= CHUNK_SIZE_Y)
			{
				break;
			}
			if (getBlockOrNeighbor(chunk, neighbors, bx, cy, cz) != 0)
			{
				value -= dec;
			}
			dec -= 2;
		}

		if (value < 37)
		{
			value = 37;
		}
		if (value > 127)
		{
			value = 127;
		}
		return value;
	}

	static uint32_t getBlockOrNeighbor(const VoxelChunkData &chunk,
									   const MeshNeighborhood &neighbors,
									   int x,
									   int y,
									   int z)
	{
		if (y < 0 || y >= CHUNK_SIZE_Y)
		{
			return 0;
		}

		if (x >= CHUNK_SIZE_X && z >= CHUNK_SIZE_Z)
		{
			if (neighbors.ne == nullptr)
			{
				return 0;
			}
			return neighbors.ne->blocks[x - CHUNK_SIZE_X][y][z - CHUNK_SIZE_Z];
		}
		if (x >= CHUNK_SIZE_X && z < 0)
		{
			if (neighbors.se == nullptr)
			{
				return 0;
			}
			return neighbors.se->blocks[x - CHUNK_SIZE_X][y][CHUNK_SIZE_Z + z];
		}
		if (x < 0 && z >= CHUNK_SIZE_Z)
		{
			if (neighbors.nw == nullptr)
			{
				return 0;
			}
			return neighbors.nw->blocks[CHUNK_SIZE_X + x][y][z - CHUNK_SIZE_Z];
		}
		if (x < 0 && z < 0)
		{
			if (neighbors.sw == nullptr)
			{
				return 0;
			}
			return neighbors.sw->blocks[CHUNK_SIZE_X + x][y][CHUNK_SIZE_Z + z];
		}

		if (x >= CHUNK_SIZE_X)
		{
			if (neighbors.east == nullptr)
			{
				return 0;
			}
			return neighbors.east->blocks[x - CHUNK_SIZE_X][y][z];
		}
		if (x < 0)
		{
			if (neighbors.west == nullptr)
			{
				return 0;
			}
			return neighbors.west->blocks[CHUNK_SIZE_X + x][y][z];
		}
		if (z >= CHUNK_SIZE_Z)
		{
			if (neighbors.north == nullptr)
			{
				return 0;
			}
			return neighbors.north->blocks[x][y][z - CHUNK_SIZE_Z];
		}
		if (z < 0)
		{
			if (neighbors.south == nullptr)
			{
				return 0;
			}
			return neighbors.south->blocks[x][y][CHUNK_SIZE_Z + z];
		}

		return chunk.blocks[x][y][z];
	}

	void uploadMesh(const std::vector<uint32_t> &faces)
	{
		if (ssbo != 0)
		{
			glDeleteBuffers(1, &ssbo);
		}

		if (faces.empty())
		{
			packedFacesCpu.clear();
			faceCount = 0;
			needsMeshRebuild = false;
			return;
		}

		packedFacesCpu = faces;

		glGenBuffers(1, &ssbo);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
		glBufferData(GL_SHADER_STORAGE_BUFFER,
					 faces.size() * sizeof(uint32_t),
					 faces.data(), GL_STATIC_DRAW);

		if (vao == 0)
		{
			glGenVertexArrays(1, &vao);
		}

		faceCount = static_cast<uint32_t>(faces.size() / 2);
		needsMeshRebuild = false;
	}
};

#endif
