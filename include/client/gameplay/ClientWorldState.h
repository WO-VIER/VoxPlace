#ifndef CLIENT_GAMEPLAY_CLIENT_WORLD_STATE_H
#define CLIENT_GAMEPLAY_CLIENT_WORLD_STATE_H

#include <Chunk2.h>
#include <WorldBounds.h>

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

struct ClientWorldState
{
	WorldFrontier frontier;
	bool hasWorldFrontier = false;
	std::unordered_map<int64_t, Chunk2 *> chunkMap;
	std::unordered_set<int64_t> streamedChunkKeys;
	std::unordered_map<int64_t, uint64_t> pendingMeshRevisions;
	size_t profileChunkRequestsWindow = 0;
	size_t profileChunkDropsWindow = 0;
	size_t profileChunkReceivesWindow = 0;
	size_t profileMeshedChunkCountWindow = 0;
	size_t profileMeshedSectionCountWindow = 0;
};

#endif
