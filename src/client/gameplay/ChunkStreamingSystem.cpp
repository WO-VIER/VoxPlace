#include <client/gameplay/ChunkStreamingSystem.h>

#include <algorithm>

namespace
{
	float chunkCenterDistanceSqToCamera(const Camera &camera, int chunkX, int chunkZ)
	{
		float centerX = chunkX * CHUNK_SIZE_X + CHUNK_SIZE_X * 0.5f;
		float centerZ = chunkZ * CHUNK_SIZE_Z + CHUNK_SIZE_Z * 0.5f;
		float dx = centerX - camera.Position.x;
		float dz = centerZ - camera.Position.z;
		return dx * dx + dz * dz;
	}
}

bool ChunkStreamingSystem::usesClassicStreaming(bool hasWorldFrontier, const WorldFrontier &frontier)
{
	if (!hasWorldFrontier)
	{
		return false;
	}
	return frontier.mode == WorldGenerationMode::ClassicStreaming;
}

bool ChunkStreamingSystem::canStreamChunk(bool hasWorldFrontier,
										  const WorldFrontier &frontier,
										  int chunkX,
										  int chunkZ)
{
	if (usesClassicStreaming(hasWorldFrontier, frontier))
	{
		return true;
	}
	return frontier.generatedBounds.containsChunk(chunkX, chunkZ);
}

size_t ChunkStreamingSystem::inflightChunkRequestCount(
	const std::unordered_set<int64_t> &streamedChunkKeys,
	const std::unordered_map<int64_t, ClientChunk *> &chunkMap)
{
	size_t inflightCount = 0;
	for (int64_t key : streamedChunkKeys)
	{
		if (chunkMap.find(key) == chunkMap.end())
		{
			inflightCount++;
		}
	}
	return inflightCount;
}

void ChunkStreamingSystem::syncChunkStreaming(
	WorldClient &worldClient,
	const Camera &camera,
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
	const std::function<void(int64_t)> &dropChunkByKey)
{
	if (!worldClient.isConnected() || !hasWorldFrontier)
	{
		return;
	}

	int streamDistanceChunks = renderDistanceChunks;
	if (usesClassicStreaming(hasWorldFrontier, frontier))
	{
		streamDistanceChunks += classicStreamingPaddingChunks;
	}

	int cameraChunkX = floorDiv(static_cast<int>(std::floor(camera.Position.x)), CHUNK_SIZE_X);
	int cameraChunkZ = floorDiv(static_cast<int>(std::floor(camera.Position.z)), CHUNK_SIZE_Z);
	int radiusSq = streamDistanceChunks * streamDistanceChunks;

	std::unordered_set<int64_t> desiredKeys;
	struct ChunkRequestCandidate
	{
		int chunkX = 0;
		int chunkZ = 0;
		float distSq = 0.0f;
		bool inFrustum = false;
	};
	std::vector<ChunkRequestCandidate> requestCandidates;

	for (int dz = -streamDistanceChunks; dz <= streamDistanceChunks; dz++)
	{
		for (int dx = -streamDistanceChunks; dx <= streamDistanceChunks; dx++)
		{
			if (dx * dx + dz * dz > radiusSq)
			{
				continue;
			}

			int cx = cameraChunkX + dx;
			int cz = cameraChunkZ + dz;
			if (!canStreamChunk(hasWorldFrontier, frontier, cx, cz))
			{
				continue;
			}

			int64_t key = chunkKey(cx, cz);
			desiredKeys.insert(key);
			if (streamedChunkKeys.find(key) == streamedChunkKeys.end())
			{
				ChunkRequestCandidate candidate;
				candidate.chunkX = cx;
				candidate.chunkZ = cz;
				candidate.distSq = chunkCenterDistanceSqToCamera(camera, cx, cz);
				candidate.inFrustum = streamFrustum.isChunkVisible(cx, cz);
				requestCandidates.push_back(candidate);
			}
		}
	}

	std::sort(requestCandidates.begin(), requestCandidates.end(),
			  [](const ChunkRequestCandidate &left, const ChunkRequestCandidate &right)
			  {
				  if (left.inFrustum != right.inFrustum)
				  {
					  return left.inFrustum > right.inFrustum;
				  }
				  return left.distSq < right.distSq;
			  });

	size_t maxNewRequestsThisFrame = requestCandidates.size();
	if (usesClassicStreaming(hasWorldFrontier, frontier))
	{
		size_t inflightRequests = inflightChunkRequestCount(streamedChunkKeys, chunkMap);
		size_t inflightBudget = 0;
		if (inflightRequests < classicMaxInflightChunkRequests)
		{
			inflightBudget = classicMaxInflightChunkRequests - inflightRequests;
		}

		maxNewRequestsThisFrame = std::min(maxNewRequestsThisFrame, inflightBudget);
		maxNewRequestsThisFrame = std::min(maxNewRequestsThisFrame, classicMaxChunkRequestsPerFrame);
	}

	size_t sentRequestsThisFrame = 0;
	for (const ChunkRequestCandidate &candidate : requestCandidates)
	{
		if (sentRequestsThisFrame >= maxNewRequestsThisFrame)
		{
			break;
		}
		worldClient.sendChunkRequest(candidate.chunkX, candidate.chunkZ);
		streamedChunkKeys.insert(chunkKey(candidate.chunkX, candidate.chunkZ));
		profileChunkRequestsWindow++;
		sentRequestsThisFrame++;
	}

	int dropDistanceChunks = streamDistanceChunks + 4;
	int dropRadiusSq = dropDistanceChunks * dropDistanceChunks;

	std::vector<int64_t> keysToDrop;
	for (int64_t key : streamedChunkKeys)
	{
		int cx = static_cast<int>(key >> 32);
		int cz = static_cast<int>(key & 0xFFFFFFFF);
		int dx = cx - cameraChunkX;
		int dz = cz - cameraChunkZ;
		
		if (dx * dx + dz * dz > dropRadiusSq || !canStreamChunk(hasWorldFrontier, frontier, cx, cz))
		{
			keysToDrop.push_back(key);
		}
	}

	for (int64_t key : keysToDrop)
	{
		int cx = static_cast<int>(key >> 32);
		int cz = static_cast<int>(key & 0xFFFFFFFF);
		worldClient.sendChunkDrop(cx, cz);
		streamedChunkKeys.erase(key);
		profileChunkDropsWindow++;
		dropChunkByKey(key);
	}
}
