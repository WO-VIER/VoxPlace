#ifndef WORLD_BOUNDS_H
#define WORLD_BOUNDS_H

#include <VoxelChunkData.h>

enum class WorldGenerationMode : uint8_t
{
	ActivityFrontier = 0,
	ClassicStreaming = 1
};

inline const char *worldGenerationModeName(WorldGenerationMode mode)
{
	switch (mode)
	{
	case WorldGenerationMode::ActivityFrontier:
		return "ActivityFrontier";
	case WorldGenerationMode::ClassicStreaming:
		return "ClassicStreaming";
	}
	return "Unknown";
}

struct ChunkBounds
{
	int minChunkX = 0;
	int maxChunkXExclusive = 0;
	int minChunkZ = 0;
	int maxChunkZExclusive = 0;

	int widthChunks() const
	{
		return maxChunkXExclusive - minChunkX;
	}

	int depthChunks() const
	{
		return maxChunkZExclusive - minChunkZ;
	}

	bool containsChunk(int cx, int cz) const
	{
		if (cx < minChunkX || cx >= maxChunkXExclusive)
		{
			return false;
		}
		if (cz < minChunkZ || cz >= maxChunkZExclusive)
		{
			return false;
		}
		return true;
	}

	bool containsChunk(const ChunkCoord &coord) const
	{
		return containsChunk(coord.x, coord.z);
	}

	bool containsWorldBlock(int wx, int wz) const
	{
		return containsChunk(floorDiv(wx, CHUNK_SIZE_X), floorDiv(wz, CHUNK_SIZE_Z));
	}

	ChunkBounds expanded(int ringWidth) const
	{
		ChunkBounds result = *this;
		result.minChunkX -= ringWidth;
		result.maxChunkXExclusive += ringWidth;
		result.minChunkZ -= ringWidth;
		result.maxChunkZExclusive += ringWidth;
		return result;
	}
};

struct WorldFrontier
{
	ChunkBounds playableBounds;
	ChunkBounds generatedBounds;
	int paddingChunks = 0;
	int activePlayableChunkCount = 0;
	int requiredActiveChunkCount = 0;
	WorldGenerationMode mode = WorldGenerationMode::ActivityFrontier;
};

inline int perimeterChunkCount(const ChunkBounds &bounds)
{
	int side = bounds.widthChunks();
	if (side <= 1)
	{
		return side;
	}
	return 4 * side - 4;
}

#endif
