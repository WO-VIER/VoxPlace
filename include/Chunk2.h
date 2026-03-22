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
	static constexpr int SUNBLOCK_TRACE_DEPTH = 9;

	struct MeshNeighborhood
	{
		uint32_t north[CHUNK_SIZE_X][CHUNK_SIZE_Y] = {};
		uint32_t south[CHUNK_SIZE_X][CHUNK_SIZE_Y][SUNBLOCK_TRACE_DEPTH] = {};
		uint32_t east[CHUNK_SIZE_Y][CHUNK_SIZE_Z] = {};
		uint32_t west[CHUNK_SIZE_Y][CHUNK_SIZE_Z] = {};
		uint32_t ne[CHUNK_SIZE_Y] = {};
		uint32_t nw[CHUNK_SIZE_Y] = {};
		uint32_t se[CHUNK_SIZE_Y] = {};
		uint32_t sw[CHUNK_SIZE_Y] = {};
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
		MeshNeighborhood neighbors;
		captureMeshNeighborhood(
			neighbors,
			north, south, east, west, ne, nw, se, sw);
		std::vector<uint32_t> packedFaces = buildPackedFaces(*this, neighbors);
		uploadMesh(packedFaces);
	}

	static void captureMeshNeighborhood(MeshNeighborhood &neighborhood,
										const VoxelChunkData *north,
										const VoxelChunkData *south,
										const VoxelChunkData *east,
										const VoxelChunkData *west,
										const VoxelChunkData *ne,
										const VoxelChunkData *nw,
										const VoxelChunkData *se,
										const VoxelChunkData *sw)
	{
		neighborhood = MeshNeighborhood();
		captureNorthEdge(neighborhood, north);
		captureSouthBand(neighborhood, south);
		captureEastEdge(neighborhood, east);
		captureWestEdge(neighborhood, west);
		captureNorthEastCorner(neighborhood, ne);
		captureNorthWestCorner(neighborhood, nw);
		captureSouthEastCorner(neighborhood, se);
		captureSouthWestCorner(neighborhood, sw);
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
	static void captureNorthEdge(MeshNeighborhood &neighborhood,
								 const VoxelChunkData *north)
	{
		if (north == nullptr)
		{
			return;
		}

		for (int x = 0; x < CHUNK_SIZE_X; x++)
		{
			for (int y = 0; y < CHUNK_SIZE_Y; y++)
			{
				neighborhood.north[x][y] = north->blocks[x][y][0];
			}
		}
	}

	static void captureSouthBand(MeshNeighborhood &neighborhood,
								 const VoxelChunkData *south)
	{
		if (south == nullptr)
		{
			return;
		}

		for (int x = 0; x < CHUNK_SIZE_X; x++)
		{
			for (int y = 0; y < CHUNK_SIZE_Y; y++)
			{
				for (int depth = 0; depth < SUNBLOCK_TRACE_DEPTH; depth++)
				{
					int sourceZ = CHUNK_SIZE_Z - 1 - depth;
					neighborhood.south[x][y][depth] = south->blocks[x][y][sourceZ];
				}
			}
		}
	}

	static void captureEastEdge(MeshNeighborhood &neighborhood,
								const VoxelChunkData *east)
	{
		if (east == nullptr)
		{
			return;
		}

		for (int y = 0; y < CHUNK_SIZE_Y; y++)
		{
			for (int z = 0; z < CHUNK_SIZE_Z; z++)
			{
				neighborhood.east[y][z] = east->blocks[0][y][z];
			}
		}
	}

	static void captureWestEdge(MeshNeighborhood &neighborhood,
								const VoxelChunkData *west)
	{
		if (west == nullptr)
		{
			return;
		}

		for (int y = 0; y < CHUNK_SIZE_Y; y++)
		{
			for (int z = 0; z < CHUNK_SIZE_Z; z++)
			{
				neighborhood.west[y][z] = west->blocks[CHUNK_SIZE_X - 1][y][z];
			}
		}
	}

	static void captureNorthEastCorner(MeshNeighborhood &neighborhood,
									   const VoxelChunkData *ne)
	{
		if (ne == nullptr)
		{
			return;
		}

		for (int y = 0; y < CHUNK_SIZE_Y; y++)
		{
			neighborhood.ne[y] = ne->blocks[0][y][0];
		}
	}

	static void captureNorthWestCorner(MeshNeighborhood &neighborhood,
									   const VoxelChunkData *nw)
	{
		if (nw == nullptr)
		{
			return;
		}

		for (int y = 0; y < CHUNK_SIZE_Y; y++)
		{
			neighborhood.nw[y] = nw->blocks[CHUNK_SIZE_X - 1][y][0];
		}
	}

	static void captureSouthEastCorner(MeshNeighborhood &neighborhood,
									   const VoxelChunkData *se)
	{
		if (se == nullptr)
		{
			return;
		}

		for (int y = 0; y < CHUNK_SIZE_Y; y++)
		{
			neighborhood.se[y] = se->blocks[0][y][CHUNK_SIZE_Z - 1];
		}
	}

	static void captureSouthWestCorner(MeshNeighborhood &neighborhood,
									   const VoxelChunkData *sw)
	{
		if (sw == nullptr)
		{
			return;
		}

		for (int y = 0; y < CHUNK_SIZE_Y; y++)
		{
			neighborhood.sw[y] = sw->blocks[CHUNK_SIZE_X - 1][y][CHUNK_SIZE_Z - 1];
		}
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
		int dec = SUNBLOCK_TRACE_DEPTH * 2;
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
			if (x == CHUNK_SIZE_X && z == CHUNK_SIZE_Z)
			{
				return neighbors.ne[y];
			}
			return 0;
		}
		if (x >= CHUNK_SIZE_X && z < 0)
		{
			if (x == CHUNK_SIZE_X && z == -1)
			{
				return neighbors.se[y];
			}
			return 0;
		}
		if (x < 0 && z >= CHUNK_SIZE_Z)
		{
			if (x == -1 && z == CHUNK_SIZE_Z)
			{
				return neighbors.nw[y];
			}
			return 0;
		}
		if (x < 0 && z < 0)
		{
			if (x == -1 && z == -1)
			{
				return neighbors.sw[y];
			}
			return 0;
		}

		if (x >= CHUNK_SIZE_X)
		{
			if (x == CHUNK_SIZE_X)
			{
				return neighbors.east[y][z];
			}
			return 0;
		}
		if (x < 0)
		{
			if (x == -1)
			{
				return neighbors.west[y][z];
			}
			return 0;
		}
		if (z >= CHUNK_SIZE_Z)
		{
			if (z == CHUNK_SIZE_Z)
			{
				return neighbors.north[x][y];
			}
			return 0;
		}
		if (z < 0)
		{
			int southDepth = -z - 1;
			if (southDepth < SUNBLOCK_TRACE_DEPTH)
			{
				return neighbors.south[x][y][southDepth];
			}
			return 0;
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
