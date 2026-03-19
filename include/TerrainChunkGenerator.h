#ifndef TERRAIN_CHUNK_GENERATOR_H
#define TERRAIN_CHUNK_GENERATOR_H

#include <IChunkGenerator.h>
#include <TerrainGenerator.h>

class TerrainChunkGenerator : public IChunkGenerator
{
public:
	explicit TerrainChunkGenerator(int seed) : m_generator(seed)
	{
	}

	void fillChunk(VoxelChunkData &chunk) const override
	{
		m_generator.fillChunk(chunk);
	}

private:
	TerrainGenerator m_generator;
};

#endif
