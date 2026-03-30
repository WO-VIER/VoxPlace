#include <client/gameplay/MeshBuildSystem.h>

#include <unordered_set>

ClientChunk *MeshBuildSystem::getChunkAt(const std::unordered_map<int64_t, ClientChunk *> &chunkMap, int cx, int cz)
{
	auto it = chunkMap.find(chunkKey(cx, cz));
	if (it == chunkMap.end())
	{
		return nullptr;
	}
	return it->second;
}

void MeshBuildSystem::markChunkNeighborhoodDirty(std::unordered_map<int64_t, ClientChunk *> &chunkMap, int cx, int cz)
{
	for (int dz = -1; dz <= 1; dz++)
	{
		for (int dx = -1; dx <= 1; dx++)
		{
			ClientChunk *chunk = getChunkAt(chunkMap, cx + dx, cz + dz);
			if (chunk != nullptr)
			{
				chunk->renderState.needsMeshRebuild = true;
			}
		}
	}
}

bool MeshBuildSystem::removeClientChunkByKey(int64_t key,
											 std::unordered_map<int64_t, ClientChunk *> &chunkMap,
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

	int cx = chunkIt->second->storage.chunkX;
	int cz = chunkIt->second->storage.chunkZ;
	delete chunkIt->second;
	chunkMap.erase(chunkIt);
	markChunkNeighborhoodDirty(chunkMap, cx, cz);
	return true;
}

uint32_t MeshBuildSystem::getBlockWorld(const std::unordered_map<int64_t, ClientChunk *> &chunkMap,
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

	ClientChunk *chunk = getChunkAt(chunkMap, cx, cz);
	if (chunk == nullptr)
	{
		return 0;
	}
	return chunk->storage.getBlock(lx, wy, lz);
}

void MeshBuildSystem::applyBlockUpdateLocal(std::unordered_map<int64_t, ClientChunk *> &chunkMap,
											int wx,
											int wy,
											int wz,
											uint32_t color)
{
	int cx = floorDiv(wx, CHUNK_SIZE_X);
	int cz = floorDiv(wz, CHUNK_SIZE_Z);
	int lx = floorMod(wx, CHUNK_SIZE_X);
	int lz = floorMod(wz, CHUNK_SIZE_Z);

	ClientChunk *chunk = getChunkAt(chunkMap, cx, cz);
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

bool MeshBuildSystem::upsertChunkSnapshot(std::unordered_map<int64_t, ClientChunk *> &chunkMap,
										  const std::unordered_set<int64_t> &streamedChunkKeys,
										  const VoxelChunkData &snapshot)
{
	int64_t key = chunkKey(snapshot.chunkX, snapshot.chunkZ);
	if (streamedChunkKeys.find(key) == streamedChunkKeys.end())
	{
		return false;
	}

	ClientChunk *chunk = nullptr;
	auto it = chunkMap.find(key);
	if (it == chunkMap.end())
	{
		chunk = new ClientChunk(snapshot.chunkX, snapshot.chunkZ);
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

void MeshBuildSystem::drainCompletedMeshBuilds(std::unordered_map<int64_t, ClientChunk *> &chunkMap,
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

		ClientChunk *chunk = chunkIt->second;
		if (chunk == nullptr)
		{
			continue;
		}
		if (chunk->storage.revision != result.revision)
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
										 std::unordered_map<int64_t, ClientChunk *> &chunkMap,
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

		int64_t key = chunkKey(candidate.chunk->storage.chunkX, candidate.chunk->storage.chunkZ);
		pendingMeshRevisions[key] = candidate.chunk->storage.revision;
		chunkMesher.enqueue(std::move(job));
		scheduleBudget--;
	}
}

bool MeshBuildSystem::isMeshBuildPendingForCurrentRevision(
	ClientChunk *chunk,
	std::unordered_map<int64_t, uint64_t> &pendingMeshRevisions)
{
	int64_t key = chunkKey(chunk->storage.chunkX, chunk->storage.chunkZ);
	auto pendingIt = pendingMeshRevisions.find(key);
	if (pendingIt == pendingMeshRevisions.end())
	{
		return false;
	}
	if (pendingIt->second != chunk->storage.revision)
	{
		pendingMeshRevisions.erase(pendingIt);
		return false;
	}
	return true;
}

bool MeshBuildSystem::buildMeshJobForChunk(
	ClientChunk *chunk,
	const std::unordered_map<int64_t, ClientChunk *> &chunkMap,
	ClientChunkMeshJob &outJob)
{
	if (chunk == nullptr)
	{
		return false;
	}

	int cx = chunk->storage.chunkX;
	int cz = chunk->storage.chunkZ;
	outJob.chunkX = cx;
	outJob.chunkZ = cz;
	outJob.revision = chunk->storage.revision;
	outJob.center = chunk->storage;
	ClientChunk::captureMeshNeighborhood(
		outJob.neighbors,
		getChunkAt(chunkMap, cx, cz + 1) != nullptr ? &getChunkAt(chunkMap, cx, cz + 1)->storage : nullptr,
		getChunkAt(chunkMap, cx, cz - 1) != nullptr ? &getChunkAt(chunkMap, cx, cz - 1)->storage : nullptr,
		getChunkAt(chunkMap, cx + 1, cz) != nullptr ? &getChunkAt(chunkMap, cx + 1, cz)->storage : nullptr,
		getChunkAt(chunkMap, cx - 1, cz) != nullptr ? &getChunkAt(chunkMap, cx - 1, cz)->storage : nullptr,
		getChunkAt(chunkMap, cx + 1, cz + 1) != nullptr ? &getChunkAt(chunkMap, cx + 1, cz + 1)->storage : nullptr,
		getChunkAt(chunkMap, cx - 1, cz + 1) != nullptr ? &getChunkAt(chunkMap, cx - 1, cz + 1)->storage : nullptr,
		getChunkAt(chunkMap, cx + 1, cz - 1) != nullptr ? &getChunkAt(chunkMap, cx + 1, cz - 1)->storage : nullptr,
		getChunkAt(chunkMap, cx - 1, cz - 1) != nullptr ? &getChunkAt(chunkMap, cx - 1, cz - 1)->storage : nullptr);
	return true;
}
