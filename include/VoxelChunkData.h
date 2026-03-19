#ifndef VOXEL_CHUNK_DATA_H
#define VOXEL_CHUNK_DATA_H

#include <algorithm>
#include <cstdint>
#include <cstring>

constexpr uint8_t CHUNK_SIZE_X = 16;
constexpr uint8_t CHUNK_SIZE_Y = 64;
constexpr uint8_t CHUNK_SIZE_Z = 16;
constexpr uint8_t BEDROCK_LAYER = 0;

constexpr size_t CHUNK_BLOCK_COUNT =
	static_cast<size_t>(CHUNK_SIZE_X) *
	static_cast<size_t>(CHUNK_SIZE_Y) *
	static_cast<size_t>(CHUNK_SIZE_Z);

struct ChunkCoord
{
	int x = 0;
	int z = 0;

	bool operator==(const ChunkCoord &other) const
	{
		return x == other.x && z == other.z;
	}
};

inline int64_t chunkKey(int cx, int cz)
{
	return (static_cast<int64_t>(cx) << 32) | (static_cast<int64_t>(cz) & 0xFFFFFFFFll);
}

inline int64_t chunkKey(const ChunkCoord &coord)
{
	return chunkKey(coord.x, coord.z);
}

inline int floorDiv(int value, int divisor)
{
	int quotient = value / divisor;
	int remainder = value % divisor;
	if (remainder < 0)
	{
		quotient--;
	}
	return quotient;
}

inline int floorMod(int value, int divisor)
{
	int remainder = value % divisor;
	if (remainder < 0)
	{
		remainder += divisor;
	}
	return remainder;
}

struct VoxelChunkData
{
	int chunkX = 0;
	int chunkZ = 0;
	uint64_t revision = 0;
	uint32_t blocks[CHUNK_SIZE_X][CHUNK_SIZE_Y][CHUNK_SIZE_Z];

	VoxelChunkData(int cx = 0, int cz = 0) : chunkX(cx), chunkZ(cz)
	{
		clearBlocks();
	}

	void clearBlocks()
	{
		std::memset(blocks, 0, sizeof(blocks));
	}

	ChunkCoord coord() const
	{
		ChunkCoord result;
		result.x = chunkX;
		result.z = chunkZ;
		return result;
	}

	void setChunkCoord(int cx, int cz)
	{
		chunkX = cx;
		chunkZ = cz;
	}

	uint32_t getBlock(int x, int y, int z) const
	{
		if (x < 0 || x >= CHUNK_SIZE_X)
		{
			return 0;
		}
		if (y < 0 || y >= CHUNK_SIZE_Y)
		{
			return 0;
		}
		if (z < 0 || z >= CHUNK_SIZE_Z)
		{
			return 0;
		}
		return blocks[x][y][z];
	}

	bool setBlockRaw(int x, int y, int z, uint32_t color)
	{
		if (x < 0 || x >= CHUNK_SIZE_X)
		{
			return false;
		}
		if (y < 0 || y >= CHUNK_SIZE_Y)
		{
			return false;
		}
		if (z < 0 || z >= CHUNK_SIZE_Z)
		{
			return false;
		}
		if (y == BEDROCK_LAYER && color == 0)
		{
			return false;
		}
		if (blocks[x][y][z] == color)
		{
			return false;
		}
		blocks[x][y][z] = color;
		revision++;
		return true;
	}

	bool isCompletelyEmpty() const
	{
		for (int x = 0; x < CHUNK_SIZE_X; x++)
		{
			for (int y = 0; y < CHUNK_SIZE_Y; y++)
			{
				for (int z = 0; z < CHUNK_SIZE_Z; z++)
				{
					if (blocks[x][y][z] != 0)
					{
						return false;
					}
				}
			}
		}
		return true;
	}

	static int colorR(uint32_t color)
	{
		return color & 0xFF;
	}

	static int colorG(uint32_t color)
	{
		return (color >> 8) & 0xFF;
	}

	static int colorB(uint32_t color)
	{
		return (color >> 16) & 0xFF;
	}

	static uint32_t makeColor(int red, int green, int blue)
	{
		red = std::clamp(red, 0, 255);
		green = std::clamp(green, 0, 255);
		blue = std::clamp(blue, 0, 255);
		return static_cast<uint32_t>(red)
			| (static_cast<uint32_t>(green) << 8)
			| (static_cast<uint32_t>(blue) << 16);
	}
};

#endif
