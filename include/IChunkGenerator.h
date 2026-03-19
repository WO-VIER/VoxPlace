#ifndef I_CHUNK_GENERATOR_H
#define I_CHUNK_GENERATOR_H

#include <VoxelChunkData.h>

class IChunkGenerator
{
public:
	virtual ~IChunkGenerator() = default;
	virtual void fillChunk(VoxelChunkData &chunk) const = 0;
};

#endif
