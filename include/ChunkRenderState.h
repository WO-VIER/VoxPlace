#ifndef CHUNK_RENDER_STATE_H
#define CHUNK_RENDER_STATE_H

#include <cstdint>

struct ChunkRenderState
{
	uint32_t faceCount = 0;
	bool needsMeshRebuild = true;
	bool isEmpty = true;
};

#endif
