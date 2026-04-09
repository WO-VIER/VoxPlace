#ifndef CLIENT_WORLD_CHUNK_STREAMING_SYSTEM_H
#define CLIENT_WORLD_CHUNK_STREAMING_SYSTEM_H

#include <client/rendering/Camera.h>
#include <ClientChunk.h>
#include <Frustum.h>
#include <WorldClient.h>

#include <functional>
#include <unordered_map>
#include <unordered_set>

class ChunkStreamingSystem
{
public:
	static bool usesClassicStreaming(bool hasWorldFrontier, const WorldFrontier &frontier);
	static bool canStreamChunk(bool hasWorldFrontier, const WorldFrontier &frontier, int chunkX, int chunkZ);
	static size_t inflightChunkRequestCount(const std::unordered_set<int64_t> &streamedChunkKeys,
											const std::unordered_map<int64_t, ClientChunk *> &chunkMap);
	static void syncChunkStreaming(
		WorldClient &worldClient,
		const Camera &camera,
		const glm::vec3 &cameraVelocity,
		uint32_t roundTripTimeMs,
		float serverTerrainGenMsAvg,
		float serverSqliteLoadMsAvg,
		float deltaTime,
		const Frustum &streamFrustum,
		bool hasWorldFrontier,
		const WorldFrontier &frontier,
		int renderDistanceChunks,
		int classicStreamingPaddingChunks,
		size_t classicMaxInflightChunkRequests,
		size_t classicMaxChunkRequestsPerFrame,
		std::unordered_set<int64_t> &streamedChunkKeys,
		std::unordered_map<int64_t, ClientChunk *> &chunkMap,
		size_t &profileChunkRequestsWindow,
		size_t &profileChunkDropsWindow,
		const std::function<void(int64_t)> &dropChunkByKey);
};

#endif
