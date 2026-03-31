#ifndef CLIENT_WORLD_MESH_BUILD_SYSTEM_H
#define CLIENT_WORLD_MESH_BUILD_SYSTEM_H

#include <ClientChunk.h>
#include <client/rendering/ChunkIndirectRenderer.h>
#include <client/rendering/ClientChunkMesher.h>
#include <client/rendering/WorldRenderer.h>

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

class MeshBuildSystem
{
public:
	static ClientChunk *getChunkAt(const std::unordered_map<int64_t, ClientChunk *> &chunkMap, int cx, int cz);
	static void markChunkNeighborhoodDirty(std::unordered_map<int64_t, ClientChunk *> &chunkMap, int cx, int cz);
	static bool removeClientChunkByKey(int64_t key,
									   std::unordered_map<int64_t, ClientChunk *> &chunkMap,
									   std::unordered_map<int64_t, uint64_t> &pendingMeshRevisions,
									   ChunkIndirectRenderer &indirectRenderer,
									   size_t &chunkUnloadCountWindow);
	static uint32_t getBlockWorld(const std::unordered_map<int64_t, ClientChunk *> &chunkMap,
								  int wx,
								  int wy,
								  int wz);
	static void applyBlockUpdateLocal(std::unordered_map<int64_t, ClientChunk *> &chunkMap,
									  int wx,
									  int wy,
									  int wz,
									  uint32_t color);
	static bool upsertChunkSnapshot(std::unordered_map<int64_t, ClientChunk *> &chunkMap,
									const std::unordered_set<int64_t> &streamedChunkKeys,
									const VoxelChunkData &snapshot);
	static void drainCompletedMeshBuilds(std::unordered_map<int64_t, ClientChunk *> &chunkMap,
										 std::unordered_map<int64_t, uint64_t> &pendingMeshRevisions,
										 ClientChunkMesher &chunkMesher,
										 ChunkIndirectRenderer &indirectRenderer,
										 size_t &meshedChunkCountWindow,
										 size_t &meshedSectionCountWindow);
	static void scheduleMeshBuilds(const WorldVisibilitySet &visibility,
								   std::unordered_map<int64_t, ClientChunk *> &chunkMap,
								   std::unordered_map<int64_t, uint64_t> &pendingMeshRevisions,
								   ClientChunkMesher &chunkMesher);

private:
	static bool isMeshBuildPendingForCurrentRevision(
		ClientChunk *chunk,
		std::unordered_map<int64_t, uint64_t> &pendingMeshRevisions);
	static bool buildMeshJobForChunk(
		ClientChunk *chunk,
		const std::unordered_map<int64_t, ClientChunk *> &chunkMap,
		ClientChunkMeshJob &outJob);
};

#endif
