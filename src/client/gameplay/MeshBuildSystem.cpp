#include <client/gameplay/MeshBuildSystem.h>

#include <unordered_set>

Chunk2 *MeshBuildSystem::getChunkAt(const std::unordered_map<int64_t, Chunk2 *> &chunkMap, int cx, int cz)
{
	auto it = chunkMap.find(chunkKey(cx, cz));
	if (it == chunkMap.end())
	{
		return nullptr;
	}
	return it->second;
}

void MeshBuildSystem::markChunkNeighborhoodDirty(std::unordered_map<int64_t, Chunk2 *> &chunkMap, int cx, int cz)
{
	for (int dz = -1; dz <= 1; dz++)
	{
		for (int dx = -1; dx <= 1; dx++)
		{
			Chunk2 *chunk = getChunkAt(chunkMap, cx + dx, cz + dz);
			if (chunk != nullptr)
			{
				chunk->needsMeshRebuild = true;
			}
		}
	}
}

bool MeshBuildSystem::removeClientChunkByKey(int64_t key,
											 std::unordered_map<int64_t, Chunk2 *> &chunkMap,
											 std::unordered_map<int64_t, uint64_t> &pendingMeshRevisions,
											 ChunkIndirectRenderer &indirectRenderer)
{
	pendingMeshRevisions.erase(key);
	indirectRenderer.removeChunk(key);

	auto chunkIt = chunkMap.find(key);
	if (chunkIt == chunkMap.end())
	{
		return false;
	}

	int cx = chunkIt->second->chunkX;
	int cz = chunkIt->second->chunkZ;
	delete chunkIt->second;
	chunkMap.erase(chunkIt);
	markChunkNeighborhoodDirty(chunkMap, cx, cz);
	return true;
}

uint32_t MeshBuildSystem::getBlockWorld(const std::unordered_map<int64_t, Chunk2 *> &chunkMap,
										int wx,
										int wy,
										int wz)
{
	if (wy < 0 || wy >= CHUNK_SIZE_Y)
	{
		return 0;
	}

	int cx = floorDiv(wx, CHUNK_SIZE_X);
	int cz = floorDiv(wz, CHUNK_SIZE_Z);
	int lx = floorMod(wx, CHUNK_SIZE_X);
	int lz = floorMod(wz, CHUNK_SIZE_Z);

	Chunk2 *chunk = getChunkAt(chunkMap, cx, cz);
	if (chunk == nullptr)
	{
		return 0;
	}
	return chunk->getBlock(lx, wy, lz);
}

void MeshBuildSystem::applyBlockUpdateLocal(std::unordered_map<int64_t, Chunk2 *> &chunkMap,
											int wx,
											int wy,
											int wz,
											uint32_t color)
{
	int cx = floorDiv(wx, CHUNK_SIZE_X);
	int cz = floorDiv(wz, CHUNK_SIZE_Z);
	int lx = floorMod(wx, CHUNK_SIZE_X);
	int lz = floorMod(wz, CHUNK_SIZE_Z);

	Chunk2 *chunk = getChunkAt(chunkMap, cx, cz);
	if (chunk == nullptr)
	{
		return;
	}

	if (!chunk->setBlock(lx, wy, lz, color))
	{
		return;
	}
	markChunkNeighborhoodDirty(chunkMap, cx, cz);
}

bool MeshBuildSystem::upsertChunkSnapshot(std::unordered_map<int64_t, Chunk2 *> &chunkMap,
										  const std::unordered_set<int64_t> &streamedChunkKeys,
										  const VoxelChunkData &snapshot)
{
	int64_t key = chunkKey(snapshot.chunkX, snapshot.chunkZ);
	if (streamedChunkKeys.find(key) == streamedChunkKeys.end())
	{
		return false;
	}

	Chunk2 *chunk = nullptr;
	auto it = chunkMap.find(key);
	if (it == chunkMap.end())
	{
		chunk = new Chunk2(snapshot.chunkX, snapshot.chunkZ);
		chunkMap[key] = chunk;
	}
	else
	{
		chunk = it->second;
	}

	chunk->copyFromData(snapshot);
	markChunkNeighborhoodDirty(chunkMap, snapshot.chunkX, snapshot.chunkZ);
	return true;
}

void MeshBuildSystem::drainCompletedMeshBuilds(std::unordered_map<int64_t, Chunk2 *> &chunkMap,
											   std::unordered_map<int64_t, uint64_t> &pendingMeshRevisions,
											   ClientChunkMesher &chunkMesher,
											   ChunkIndirectRenderer &indirectRenderer,
											   size_t &meshedChunkCountWindow,
											   size_t &meshedSectionCountWindow)
{
	std::vector<ClientChunkMeshResult> completedResults;
	chunkMesher.drainCompleted(completedResults);

	for (ClientChunkMeshResult &result : completedResults)
	{
		int64_t key = chunkKey(result.chunkX, result.chunkZ);
		auto pendingIt = pendingMeshRevisions.find(key);
		if (pendingIt != pendingMeshRevisions.end() &&
			pendingIt->second == result.revision)
		{
			pendingMeshRevisions.erase(pendingIt);
		}

		auto chunkIt = chunkMap.find(key);
		if (chunkIt == chunkMap.end())
		{
			continue;
		}

		Chunk2 *chunk = chunkIt->second;
		if (chunk == nullptr)
		{
			continue;
		}
		if (chunk->revision != result.revision)
		{
			continue;
		}

		chunk->uploadBuiltMesh(result.packedFaces);
		indirectRenderer.upsertChunk(*chunk);
		meshedChunkCountWindow++;
		meshedSectionCountWindow += result.nonEmptySectionCount;
	}
}

void MeshBuildSystem::scheduleMeshBuilds(const WorldVisibilitySet &visibility,
										 std::unordered_map<int64_t, Chunk2 *> &chunkMap,
										 std::unordered_map<int64_t, uint64_t> &pendingMeshRevisions,
										 ClientChunkMesher &chunkMesher)
{
	size_t meshWorkerCount = chunkMesher.workerCount();
	if (meshWorkerCount < 1)
	{
		meshWorkerCount = 1;
	}

	size_t maxPendingMeshJobs = meshWorkerCount * 2;
	if (maxPendingMeshJobs < 2)
	{
		maxPendingMeshJobs = 2;
	}

	size_t scheduleBudget = 0;
	if (pendingMeshRevisions.size() < maxPendingMeshJobs)
	{
		scheduleBudget = maxPendingMeshJobs - pendingMeshRevisions.size();
	}

	for (const ChunkRebuildCandidate &candidate : visibility.rebuildCandidates)
	{
		if (scheduleBudget == 0)
		{
			break;
		}
		if (isMeshBuildPendingForCurrentRevision(candidate.chunk, pendingMeshRevisions))
		{
			continue;
		}

		ClientChunkMeshJob job;
		if (!buildMeshJobForChunk(candidate.chunk, chunkMap, job))
		{
			continue;
		}

		int64_t key = chunkKey(candidate.chunk->chunkX, candidate.chunk->chunkZ);
		pendingMeshRevisions[key] = candidate.chunk->revision;
		chunkMesher.enqueue(std::move(job));
		scheduleBudget--;
	}
}

bool MeshBuildSystem::isMeshBuildPendingForCurrentRevision(
	Chunk2 *chunk,
	std::unordered_map<int64_t, uint64_t> &pendingMeshRevisions)
{
	int64_t key = chunkKey(chunk->chunkX, chunk->chunkZ);
	auto pendingIt = pendingMeshRevisions.find(key);
	if (pendingIt == pendingMeshRevisions.end())
	{
		return false;
	}
	if (pendingIt->second != chunk->revision)
	{
		pendingMeshRevisions.erase(pendingIt);
		return false;
	}
	return true;
}

bool MeshBuildSystem::buildMeshJobForChunk(
	Chunk2 *chunk,
	const std::unordered_map<int64_t, Chunk2 *> &chunkMap,
	ClientChunkMeshJob &outJob)
{
	if (chunk == nullptr)
	{
		return false;
	}

	int cx = chunk->chunkX;
	int cz = chunk->chunkZ;
	outJob.chunkX = cx;
	outJob.chunkZ = cz;
	outJob.revision = chunk->revision;
	outJob.center = static_cast<const VoxelChunkData &>(*chunk);
	Chunk2::captureMeshNeighborhood(
		outJob.neighbors,
		getChunkAt(chunkMap, cx, cz + 1),
		getChunkAt(chunkMap, cx, cz - 1),
		getChunkAt(chunkMap, cx + 1, cz),
		getChunkAt(chunkMap, cx - 1, cz),
		getChunkAt(chunkMap, cx + 1, cz + 1),
		getChunkAt(chunkMap, cx - 1, cz + 1),
		getChunkAt(chunkMap, cx + 1, cz - 1),
		getChunkAt(chunkMap, cx - 1, cz - 1));
	return true;
}
