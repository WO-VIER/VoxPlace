#ifndef SKYBLOCK_GENERATOR_H
#define SKYBLOCK_GENERATOR_H

#include <IChunkGenerator.h>
#include <cmath>

class SkyblockGenerator : public IChunkGenerator
{
public:
	void fillChunk(VoxelChunkData &chunk) const override
	{
		chunk.clearBlocks();
		chunk.revision = 1;

		const uint32_t grassColor = VoxelChunkData::makeColor(108, 162, 76);
		const uint32_t dirtColor = VoxelChunkData::makeColor(107, 90, 52);
		const uint32_t stoneColor = VoxelChunkData::makeColor(94, 94, 100);
		const int islandTopY = 32;
		const float topRadius = 5.5f;
		const float dirtRadius = 6.5f;

		for (int x = 0; x < CHUNK_SIZE_X; x++)
		{
			for (int z = 0; z < CHUNK_SIZE_Z; z++)
			{
				int worldX = chunk.chunkX * CHUNK_SIZE_X + x;
				int worldZ = chunk.chunkZ * CHUNK_SIZE_Z + z;
				float dx = static_cast<float>(worldX);
				float dz = static_cast<float>(worldZ);
				float dist = std::sqrt(dx * dx + dz * dz);

				if (dist > dirtRadius)
				{
					continue;
				}

				chunk.blocks[x][BEDROCK_LAYER][z] = stoneColor;

				if (dist <= topRadius)
				{
					chunk.blocks[x][islandTopY][z] = grassColor;
					chunk.blocks[x][islandTopY - 1][z] = dirtColor;
					chunk.blocks[x][islandTopY - 2][z] = dirtColor;
					chunk.blocks[x][islandTopY - 3][z] = stoneColor;
				}
				else
				{
					chunk.blocks[x][islandTopY - 1][z] = dirtColor;
					chunk.blocks[x][islandTopY - 2][z] = stoneColor;
				}
			}
		}
	}
};

#endif
