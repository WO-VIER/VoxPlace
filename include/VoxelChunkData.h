#ifndef VOXEL_CHUNK_DATA_H
#define VOXEL_CHUNK_DATA_H

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>

constexpr uint8_t CHUNK_SIZE_X = 16;
constexpr uint8_t CHUNK_SIZE_Y = 64;
constexpr uint8_t CHUNK_SIZE_Z = 16;
constexpr uint8_t BEDROCK_LAYER = 0;
constexpr uint8_t CHUNK_SECTION_HEIGHT = 16;
constexpr uint8_t CHUNK_SECTION_COUNT = CHUNK_SIZE_Y / CHUNK_SECTION_HEIGHT;

static_assert(CHUNK_SIZE_Y % CHUNK_SECTION_HEIGHT == 0,
			  "CHUNK_SIZE_Y must stay divisible by CHUNK_SECTION_HEIGHT");

constexpr size_t CHUNK_BLOCK_COUNT =
	static_cast<size_t>(CHUNK_SIZE_X) *
	static_cast<size_t>(CHUNK_SIZE_Y) *
	static_cast<size_t>(CHUNK_SIZE_Z);

constexpr size_t CHUNK_SECTION_BLOCK_COUNT =
	static_cast<size_t>(CHUNK_SIZE_X) *
	static_cast<size_t>(CHUNK_SECTION_HEIGHT) *
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
	uint8_t nonEmptySectionMask = 0;
	uint32_t blocks[CHUNK_SIZE_X][CHUNK_SIZE_Y][CHUNK_SIZE_Z];

	VoxelChunkData(int cx = 0, int cz = 0) : chunkX(cx), chunkZ(cz)
	{
		clearBlocks();
	}

	void clearBlocks()
	{
		std::memset(blocks, 0, sizeof(blocks));
		nonEmptySectionMask = 0;
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

	static int sectionIndexFromY(int y)
	{
		return y / CHUNK_SECTION_HEIGHT;
	}

	static int sectionYBegin(int sectionIndex)
	{
		return sectionIndex * CHUNK_SECTION_HEIGHT;
	}

	static int sectionYEndExclusive(int sectionIndex)
	{
		return sectionYBegin(sectionIndex) + CHUNK_SECTION_HEIGHT;
	}

	uint8_t sectionMask() const
	{
		return nonEmptySectionMask;
	}

	bool isSectionEmpty(int sectionIndex) const
	{
		if (sectionIndex < 0 || sectionIndex >= CHUNK_SECTION_COUNT)
		{
			return true;
		}
		uint8_t bit = static_cast<uint8_t>(1u << sectionIndex);
		if ((nonEmptySectionMask & bit) == 0)
		{
			return true;
		}
		return false;
	}

	size_t nonEmptySectionCount() const
	{
		return static_cast<size_t>(std::popcount(static_cast<unsigned int>(nonEmptySectionMask)));
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
		int sectionIndex = sectionIndexFromY(y);
		uint8_t sectionBit = static_cast<uint8_t>(1u << sectionIndex);
		if (color != 0)
		{
			nonEmptySectionMask |= sectionBit;
		}
		else if (!sectionHasAnyBlocks(sectionIndex))
		{
			nonEmptySectionMask &= static_cast<uint8_t>(~sectionBit);
		}
		revision++;
		return true;
	}

	bool isCompletelyEmpty() const
	{
		if (nonEmptySectionMask == 0)
		{
			return true;
		}
		return false;
	}

	void rebuildSectionMask()
	{
		uint8_t mask = 0;
		for (int sectionIndex = 0; sectionIndex < CHUNK_SECTION_COUNT; sectionIndex++)
		{
			if (sectionHasAnyBlocks(sectionIndex))
			{
				mask |= static_cast<uint8_t>(1u << sectionIndex);
			}
		}
		nonEmptySectionMask = mask;
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

private:
	bool sectionHasAnyBlocks(int sectionIndex) const
	{
		if (sectionIndex < 0 || sectionIndex >= CHUNK_SECTION_COUNT)
		{
			return false;
		}

		int yBegin = sectionYBegin(sectionIndex);
		int yEndExclusive = sectionYEndExclusive(sectionIndex);
		for (int x = 0; x < CHUNK_SIZE_X; x++)
		{
			for (int y = yBegin; y < yEndExclusive; y++)
			{
				for (int z = 0; z < CHUNK_SIZE_Z; z++)
				{
					if (blocks[x][y][z] != 0)
					{
						return true;
					}
				}
			}
		}

		return false;
	}
};

#endif
